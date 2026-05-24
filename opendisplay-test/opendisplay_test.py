#!/usr/bin/env python3
"""
Push an image to a HomePlate device running the OpenDisplay (Flex profile)
activity. Supports both WiFi LAN (default) and BLE transports. Useful for
testing without a full Home Assistant / py-opendisplay setup.

WiFi wire format (verified from py-opendisplay + OpenDisplay reference):
  - LAN frame: [length:u16 LE][payload bytes]
  - payload starts with u16 BIG-endian command code, followed by args
  - ACK responses echo the command, optionally with the high bit set
    (CMD | 0x8000). validate_ack_response accepts either form.

BLE transport delegates to py-opendisplay's OpenDisplayDevice — the same
code path Home Assistant's OpenDisplay integration takes.

Usage:
  ./opendisplay_test.py inkplate10.png
  ./opendisplay_test.py inkplate10.png --ip 192.168.1.42 --uncompressed
  ./opendisplay_test.py inkplate10.png --transport ble
  ./opendisplay_test.py inkplate10.png --transport ble --mac AA:BB:CC:DD:EE:FF
"""

import argparse
import asyncio
import socket
import struct
import sys
import time
from pathlib import Path

from PIL import Image

# Lean on py-opendisplay for everything we can: command builders, response
# validators, config parsing, and the image pipeline (fit/dither/encode/
# compress). Same code path HA's OpenDisplay integration uses.
from opendisplay.device import prepare_image
from opendisplay.discovery import discover_devices_with_adv
from opendisplay.models.enums import FitMode
from opendisplay.protocol.commands import (
    CHUNK_SIZE,
    CommandCode,
    MAX_START_PAYLOAD,
    build_direct_write_data_command,
    build_direct_write_end_command,
    build_direct_write_start_compressed,
    build_direct_write_start_uncompressed,
    build_read_config_command,
)
from opendisplay.protocol.config_parser import parse_config_response
from opendisplay.protocol.responses import validate_ack_response

DEFAULT_HOST = "homeplate.local"
DEFAULT_PORT = 2446


def log(msg: str) -> None:
    """Print a wall-clock-timestamped line, flushed immediately.

    flush=True matters when piping output (e.g. `| tee`) — otherwise stdout
    buffers and you can't tell whether the script is stuck or making progress.
    """
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


# ---------------------------------------------------------------------------
# WiFi transport
# ---------------------------------------------------------------------------

def _recv_exact(sock: socket.socket, n: int) -> bytes:
    """Block until exactly n bytes have been received from sock.

    socket.recv() may return fewer bytes than requested (short reads are
    legal whenever the kernel's receive buffer is partial), so a single
    recv() can't be trusted for protocol framing. Loops until satisfied or
    the peer closes the connection mid-frame (raised as OSError so callers
    can distinguish "partial response" from "valid bare ACK").
    """
    out = bytearray()
    while len(out) < n:
        chunk = sock.recv(n - len(out))
        if not chunk:
            raise OSError("connection closed mid-frame")
        out.extend(chunk)
    return bytes(out)


def _send(sock: socket.socket, payload: bytes, label: str) -> None:
    """Wrap `payload` in the LAN [u16 LE length][payload] frame and send.

    payload is the OpenDisplay command body, starting with a u16-BE command
    code. `label` is a human-readable tag (e.g. "READ_CONFIG", "DATA #3/12")
    used only for logging — picking a good label here makes serial output
    much easier to correlate with the device's log.
    """
    sock.sendall(struct.pack("<H", len(payload)) + payload)
    cmd = struct.unpack(">H", payload[:2])[0]
    log(f"-> {label} (cmd=0x{cmd:04x}, {len(payload)}B)")


def _recv_response(sock: socket.socket) -> bytes:
    """Read one LAN response frame ([len LE][payload]) and return the payload.

    Bounds: at minimum 2 bytes (a bare ACK is just a 2-byte command echo);
    at most 4096 (sanity cap to refuse runaway sizes that might indicate
    a framing-corruption bug rather than a real response).
    """
    (flen,) = struct.unpack("<H", _recv_exact(sock, 2))
    if flen < 2 or flen > 4096:
        raise OSError(f"bad response length {flen}")
    return _recv_exact(sock, flen)


def _expect_ack(sock: socket.socket, cmd: CommandCode, label: str) -> None:
    """Read the next frame and verify it's an ACK for `cmd`, or raise.

    Delegates the echo-vs-echo|ACK_FLAG decision to py-opendisplay's
    validate_ack_response so we stay aligned with HA's tolerance. `label`
    is only for logging; the actual matching is on `cmd`.
    """
    body = _recv_response(sock)
    validate_ack_response(body, cmd)  # raises InvalidResponseError on mismatch
    log(f"<- ACK {label} (0x{struct.unpack('>H', body[:2])[0]:04x})")


def wait_for_device(host: str, port: int, total_timeout: float) -> socket.socket:
    """Poll-connect to (host, port) until success, or SystemExit on total_timeout.

    HomePlate spends most of its time in deep sleep — when the user runs
    this script they typically don't know whether the device is currently
    awake. Polling lets you run the script and walk over to press the wake
    button. 3s per attempt gives slow DNS time to resolve; 2s between
    attempts keeps the loop responsive without hammering. Per-socket
    timeout bumps to 15s after connect so the subsequent protocol I/O has
    headroom for slow LAN + chunked transfers.
    """
    log(f"connecting to {host}:{port} (max {int(total_timeout)}s)")
    deadline = time.monotonic() + total_timeout
    last_err: Exception | None = None
    while time.monotonic() < deadline:
        try:
            sock = socket.create_connection((host, port), timeout=3)
            sock.settimeout(15)
            log(f"connected ({sock.getpeername()})")
            return sock
        except OSError as e:
            last_err = e
            time.sleep(2)
    raise SystemExit(f"device unreachable after {int(total_timeout)}s: {last_err}")


def query_config(sock: socket.socket):
    """Send READ_CONFIG (0x0040), parse the TLV response, return GlobalConfig.

    Returns None if the device replies with a bare 2-byte ACK — that means
    the firmware doesn't expose READ_CONFIG and the caller has no info to
    drive prepare_image() with. Older HomePlate firmware did this; current
    builds always send a full TLV.

    The 2-byte echo prefix is stripped before handing the body to
    parse_config_response, which expects the wrapper bytes directly. The
    summary log line is the easy-to-spot success signal when triaging.
    """
    _send(sock, build_read_config_command(), "READ_CONFIG")
    body = _recv_response(sock)
    if len(body) <= 2:
        log(f"<- READ_CONFIG: bare ACK, device doesn't expose config")
        return None
    cfg = parse_config_response(body[2:])
    d = cfg.displays[0]
    log(f"<- READ_CONFIG: {d.pixel_width}x{d.pixel_height} "
        f"color_scheme={d.color_scheme} trans_modes=0x{d.transmission_modes:02x}")
    return cfg


def upload_wifi(sock: socket.socket, image_path: Path, compress: bool,
                fit_mode: FitMode) -> None:
    """End-to-end WiFi upload: READ_CONFIG → prepare → START → DATA × N → END.

    Flow mirrors what py-opendisplay's OpenDisplayDevice.upload_image() does
    over BLE — same command codes, same chunk size, same ACK-per-chunk
    pipelining — just on a TCP socket with the [u16 LE len][payload] LAN
    framing wrapping each command.

    The image pipeline (fit / dither / encode / compress) is delegated to
    prepare_image() so a tweak in py-opendisplay (e.g. dither algorithm
    bump) automatically flows through here too. We pass the GlobalConfig
    from query_config() so prepare_image picks the right color scheme and
    panel dimensions without us hard-coding anything.

    If compress=True but prepare_image returns no compressed payload (e.g.
    image already smaller than the compression header overhead), falls
    back to uncompressed transparently.

    Raises SystemExit if READ_CONFIG didn't expose a usable config (no way
    to encode without knowing panel format) or InvalidResponseError on any
    unexpected ACK.
    """
    cfg = query_config(sock)
    if cfg is None:
        raise SystemExit("device didn't expose config; can't pick wire format")

    img = Image.open(image_path)
    log(f"loaded {image_path.name}: mode={img.mode} size={img.size}")

    # py-opendisplay does fit + dither + encode + compress for us.
    raw, compressed, _ = prepare_image(img, config=cfg, compress=compress,
                                       fit=fit_mode)
    if compress and compressed:
        log(f"encoded: {len(raw)}B raw / {len(compressed)}B compressed "
            f"(ratio {len(compressed)/len(raw):.1%})")
        start, rest = build_direct_write_start_compressed(
            len(raw), compressed, MAX_START_PAYLOAD)
        _send(sock, start, "START (compressed)")
    else:
        log(f"encoded: {len(raw)}B raw, uncompressed")
        _send(sock, build_direct_write_start_uncompressed(), "START")
        rest = raw

    _expect_ack(sock, CommandCode.DIRECT_WRITE_START, "START")

    nchunks = (len(rest) + CHUNK_SIZE - 1) // CHUNK_SIZE
    for i in range(nchunks):
        chunk = rest[i * CHUNK_SIZE : (i + 1) * CHUNK_SIZE]
        _send(sock, build_direct_write_data_command(chunk),
              f"DATA #{i+1}/{nchunks}")
        _expect_ack(sock, CommandCode.DIRECT_WRITE_DATA, f"DATA #{i+1}")
    log(f"streamed {nchunks} DATA chunks ({len(rest)}B)")

    _send(sock, build_direct_write_end_command(refresh_mode=0), "END")
    _expect_ack(sock, CommandCode.DIRECT_WRITE_END, "END")


def wait_for_refresh_done(sock: socket.socket, timeout: float) -> None:
    """Block waiting for the device's spontaneous post-render notification.

    After END (0x72), the device drives the e-ink panel. When the refresh
    finishes, the firmware emits an unprompted 0x73 (REFRESH_DONE) or 0x74
    (REFRESH_TIMEOUT) frame on the same connection. This is useful for
    timing benchmarks; not essential for the upload itself (the panel
    refreshes whether we wait or not).

    Mask `& 0x7FFF` strips the ACK flag if present so the dispatch table
    matches either echo form. Swallows timeout/EOF as a non-fatal "no
    notification" log line — this function is best-effort.
    """
    log(f"waiting up to {int(timeout)}s for refresh completion...")
    sock.settimeout(timeout)
    try:
        body = _recv_response(sock)
        code = struct.unpack(">H", body[:2])[0] & 0x7FFF
        kind = {0x73: "DONE", 0x74: "TIMEOUT"}.get(code, f"unexpected 0x{code:04x}")
        log(f"<- refresh {kind}")
    except (OSError, socket.timeout) as e:
        log(f"no refresh notification: {type(e).__name__}: {e}")


# ---------------------------------------------------------------------------
# BLE transport (delegates to py-opendisplay's OpenDisplayDevice)
# ---------------------------------------------------------------------------

async def _ble_pick_mac(scan_timeout: float) -> str:
    """Scan for advertising OpenDisplay devices and return the chosen MAC.

    Filtering is by manufacturer ID 0x2446 (the OpenDisplay company ID we
    embed in the BLE advertisement), NOT by device name — name is just a
    user-facing label and isn't load-bearing for discovery.

    Behavior by match count:
      0 → SystemExit with hint to extend the scan window or wake the device
      1 → auto-select, log and return the MAC
      N → list all matches with `--mac` snippets, SystemExit(2) so the
          user picks one explicitly. Silently picking the strongest RSSI
          would target the wrong device in a multi-Inkplate household.
    """
    log(f"scanning for OpenDisplay BLE devices ({int(scan_timeout)}s)...")
    found = await discover_devices_with_adv(timeout=scan_timeout)
    if not found:
        raise SystemExit(
            "no OpenDisplay BLE devices found. Confirm the device is in its "
            "OpenDisplay listen window, then retry with a larger --scan-timeout.")
    if len(found) == 1:
        name, (mac, _) = next(iter(found.items()))
        log(f"auto-selected {name} @ {mac}")
        return mac
    print("multiple OpenDisplay devices found; re-run with --mac:", file=sys.stderr)
    for name, (mac, _) in sorted(found.items()):
        print(f"  --mac {mac}   # {name}", file=sys.stderr)
    raise SystemExit(2)


async def _ble_upload(mac: str, image_path: Path, compress: bool,
                      fit_mode: FitMode, scan_timeout: float) -> None:
    """Connect to `mac` via BLE GATT, upload `image_path`, then disconnect.

    Everything protocol-level (interrogation, fit/dither/encode/compress,
    chunking, ACK pipelining) is delegated to OpenDisplayDevice — same
    code path Home Assistant's OpenDisplay integration takes. The async
    context manager handles connect on __aenter__ (which interrogates and
    populates dev.width / dev.height / dev.color_scheme) and disconnect
    on __aexit__ even if upload_image raises.

    Local import of OpenDisplayDevice keeps the bleak dependency out of
    the import path when the user runs --transport=wifi (bleak pulls in
    dbus / bluez bindings that aren't needed there).
    """
    from opendisplay.device import OpenDisplayDevice

    img = Image.open(image_path)
    log(f"loaded {image_path.name}: mode={img.mode} size={img.size}")

    log(f"connecting BLE to {mac} ...")
    async with OpenDisplayDevice(mac_address=mac,
                                  discovery_timeout=scan_timeout) as dev:
        log(f"connected ({dev.width}x{dev.height}, {dev.color_scheme.name})")

        # Throttled progress reporter: OpenDisplayDevice calls this after
        # every DATA chunk ACK (~hundreds of times for a full image), so
        # we rate-limit to once per 5% of total to keep the log readable.
        # last_pct is wrapped in a list because Python closures can't
        # rebind a plain int from the enclosing scope.
        last_pct = [-1]
        start = time.monotonic()
        def progress(sent: int, total: int) -> None:
            pct = (sent * 100) // total if total else 0
            if pct >= last_pct[0] + 5 or sent == total:
                rate_kb = sent / max(0.001, time.monotonic() - start) / 1024
                log(f"upload: {pct:3d}%  {sent}/{total} B  ({rate_kb:.1f} KB/s)")
                last_pct[0] = pct

        await dev.upload_image(img, compress=compress, fit=fit_mode,
                               progress_callback=progress)
        log("upload complete")


def run_ble(args: argparse.Namespace) -> int:
    """Sync entry point for the BLE path: resolve MAC, run upload, return 0.

    Wraps the async pipeline in asyncio.run() so main() stays plain
    blocking code — the WiFi path is socket I/O and shouldn't pay an
    event-loop tax. Uncaught async exceptions surface as a normal Python
    traceback, which is the right UX for a CLI utility.
    """
    async def _run() -> None:
        mac = args.mac or await _ble_pick_mac(args.scan_timeout)
        await _ble_upload(mac, Path(args.image),
                          compress=not args.uncompressed,
                          fit_mode=FitMode[args.fit.upper()],
                          scan_timeout=args.scan_timeout)
    asyncio.run(_run())
    return 0


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    """Parse CLI args. Defaults match common HomePlate usage: WiFi to the
    canonical hostname/port, compressed upload, contain-fit, 180s wake wait.
    BLE-specific flags (`--mac`, `--scan-timeout`) are ignored when
    `--transport=wifi`, and vice versa for `--ip`/`--port`/`--wait`."""
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", help="Path to image file (PNG/JPG/etc.)")
    ap.add_argument("--transport", choices=("wifi", "ble"), default="wifi",
                    help="Transport to use (default: wifi). BLE requires bluetooth perms.")
    ap.add_argument("--mac", default=None,
                    help="BLE MAC (e.g. AA:BB:CC:DD:EE:FF). Omit to auto-scan.")
    ap.add_argument("--scan-timeout", type=float, default=8.0,
                    help="BLE scan window in seconds (default: 8).")
    ap.add_argument("--ip", "--host", default=DEFAULT_HOST,
                    help=f"HomePlate hostname/IP for WiFi (default: {DEFAULT_HOST}).")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT,
                    help=f"OpenDisplay TCP port (default: {DEFAULT_PORT}).")
    ap.add_argument("--wait", type=int, default=180,
                    help="Max seconds to wait for the WiFi device to come online (default: 180).")
    ap.add_argument("--uncompressed", action="store_true",
                    help="Skip zlib compression (useful for diagnosing decompress issues).")
    ap.add_argument("--no-wait-done", action="store_true",
                    help="Skip waiting for the 0x73 REFRESH_DONE notification on WiFi.")
    ap.add_argument("--fit", choices=("contain", "cover", "stretch", "crop"),
                    default="contain",
                    help="How to fit the source image to the panel (default: contain).")
    return ap.parse_args()


def main() -> int:
    """CLI dispatcher: route to BLE or WiFi path based on --transport.

    WiFi path uses try/finally so the socket is closed even if upload
    fails — partially-uploaded sessions on the device side end on a
    15-second per-frame timeout. BLE path manages its own connection
    lifecycle via OpenDisplayDevice's async context manager.

    Returns 0 on success. Any error path raises SystemExit (preserves
    exit code) or lets the traceback propagate naturally.
    """
    args = parse_args()
    log(f"transport: {args.transport}, image: {args.image}, "
        f"mode: {'uncompressed' if args.uncompressed else 'compressed'}")

    if args.transport == "ble":
        return run_ble(args)

    sock = wait_for_device(args.ip, args.port, args.wait)
    try:
        upload_wifi(sock, Path(args.image),
                    compress=not args.uncompressed,
                    fit_mode=FitMode[args.fit.upper()])
        if not args.no_wait_done:
            wait_for_refresh_done(sock, timeout=60)
    finally:
        sock.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
