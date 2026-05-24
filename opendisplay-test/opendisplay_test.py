#!/usr/bin/env python3
"""
Push an image to a HomePlate device running the OpenDisplay (Flex profile,
WiFi LAN) activity. Useful for testing without a full Home Assistant /
py-opendisplay setup.

Wire format (verified from py-opendisplay + the OpenDisplay reference
firmware):
  - WiFi LAN frame:  [length:u16 LE][payload bytes]
  - Inside payload, command code is u16 BIG-endian, followed by
    command-specific bytes.
  - ACK responses echo the command with the high bit set (CMD | 0x8000).

Usage:
  ./opendisplay_test.py inkplate10.png
  ./opendisplay_test.py inkplate10.png --ip 192.168.1.42
  ./opendisplay_test.py inkplate10.png --port 2446 --uncompressed
"""

import argparse
import socket
import struct
import sys
import time
import zlib
from pathlib import Path

from PIL import Image

# Pull protocol primitives + image pipeline from py-opendisplay (the
# MIT-licensed reference implementation). Same code path Home Assistant's
# OpenDisplay integration uses — if our firmware works with these helpers,
# it should work with HA. Submodule imports only.
from opendisplay.protocol.commands import (
    CHUNK_SIZE,
    CommandCode,
    MAX_START_PAYLOAD,
    RESPONSE_HIGH_BIT_FLAG as ACK_FLAG,
    build_direct_write_data_command,
    build_direct_write_end_command,
    build_direct_write_start_compressed,
    build_direct_write_start_uncompressed,
    build_read_config_command,
)
from opendisplay.protocol.config_parser import parse_config_response
from opendisplay.protocol.responses import validate_ack_response
from opendisplay.encoding.images import encode_image, fit_image
from opendisplay.models.enums import FitMode
from epaper_dithering import ColorScheme, dither_image

DEFAULT_HOST = "homeplate.local"
DEFAULT_PORT = 2446

# Command codes kept as ints for the legacy log lines; everything else
# uses the library's CommandCode enum directly.
CMD_READ_CONFIG        = int(CommandCode.READ_CONFIG)
CMD_DIRECT_WRITE_START = int(CommandCode.DIRECT_WRITE_START)
CMD_DIRECT_WRITE_DATA  = int(CommandCode.DIRECT_WRITE_DATA)
CMD_DIRECT_WRITE_END   = int(CommandCode.DIRECT_WRITE_END)


# ---------------------------------------------------------------------------
# Logging helpers
# ---------------------------------------------------------------------------

def log(msg: str) -> None:
    ts = time.strftime("%H:%M:%S")
    print(f"[{ts}] {msg}", flush=True)


def hexdump(data: bytes, prefix: str = "  ", limit: int = 32) -> str:
    take = data[:limit]
    hexs = " ".join(f"{b:02x}" for b in take)
    suffix = f" ... (+{len(data) - limit} bytes)" if len(data) > limit else ""
    return f"{prefix}{hexs}{suffix}"


# ---------------------------------------------------------------------------
# Reachability + connection
# ---------------------------------------------------------------------------

def wait_for_device(host: str, port: int, total_timeout: float) -> socket.socket:
    """Poll-connect to (host, port) until success or total_timeout elapses.

    Returns a connected socket. HomePlate may be asleep when this script
    starts, then wake on schedule, advertise mDNS, and accept connections.
    """
    log(f"connecting to {host}:{port} (max wait {int(total_timeout)}s)")
    deadline = time.monotonic() + total_timeout
    attempt = 0
    last_err: Exception | None = None
    while time.monotonic() < deadline:
        attempt += 1
        try:
            sock = socket.create_connection((host, port), timeout=3)
            sock.settimeout(15)
            log(f"connected on attempt {attempt} (remote={sock.getpeername()})")
            return sock
        except (OSError, socket.gaierror) as e:
            last_err = e
            remaining = int(deadline - time.monotonic())
            log(f"attempt {attempt} failed: {type(e).__name__}: {e} (retry, {remaining}s left)")
            time.sleep(2)
    raise SystemExit(f"device unreachable after {int(total_timeout)}s: {last_err}")


# ---------------------------------------------------------------------------
# Frame I/O
# ---------------------------------------------------------------------------

def wrap_frame(payload: bytes) -> bytes:
    return struct.pack("<H", len(payload)) + payload


def send_frame(sock: socket.socket, payload: bytes, label: str) -> None:
    frame = wrap_frame(payload)
    sock.sendall(frame)
    cmd = struct.unpack(">H", payload[:2])[0] if len(payload) >= 2 else 0
    log(f"-> {label}: cmd=0x{cmd:04x} frame={len(frame)}B payload={len(payload)}B")


def recv_ack(sock: socket.socket, expected_cmd: int) -> bool:
    """Read one [len LE][payload] response and validate via py-opendisplay.

    Defers acceptance criteria (echo vs echo|ACK_FLAG) to the library so
    we stay aligned with what HA's integration expects.
    """
    try:
        hdr = recv_exact(sock, 2)
        (flen,) = struct.unpack("<H", hdr)
        if flen == 0 or flen > 4096:
            log(f"<- bad response length {flen}")
            return False
        body = recv_exact(sock, flen)
    except OSError as e:
        log(f"<- ack read failed: {e}")
        return False
    try:
        validate_ack_response(body, expected_cmd)
    except Exception as e:
        log(f"<- ack mismatch ({len(body)}B): {e}")
        log(hexdump(body))
        return False
    code = struct.unpack(">H", body[:2])[0]
    log(f"<- ack ok: 0x{code:04x}{' (ack-flag)' if code & ACK_FLAG else ''} ({len(body)}B body)")
    return True


def recv_exact(sock: socket.socket, n: int) -> bytes:
    out = bytearray()
    while len(out) < n:
        chunk = sock.recv(n - len(out))
        if not chunk:
            raise OSError("connection closed mid-frame")
        out.extend(chunk)
    return bytes(out)


# ---------------------------------------------------------------------------
# READ_CONFIG (0x0040) — ask the device what it is
# ---------------------------------------------------------------------------

def query_config(sock: socket.socket) -> dict | None:
    """Send READ_CONFIG and return the parsed DisplayConfig fields, or None.

    Returns a dict with keys: width, height, color_scheme, partial_update,
    trans_modes. Returns None on bare ACK / parse error / timeout (the
    caller can fall back to defaults / explicit --format).
    """
    send_frame(sock, build_read_config_command(), "READ_CONFIG")

    # Read the raw frame; we need both the echo header and the TLV
    # wrapper to hand to py-opendisplay's parser.
    try:
        hdr = recv_exact(sock, 2)
    except OSError as e:
        log(f"<- READ_CONFIG: read failed: {e}")
        return None
    (flen,) = struct.unpack("<H", hdr)
    if flen < 2 or flen > 4096:
        log(f"<- READ_CONFIG: invalid frame length {flen}")
        return None
    body = recv_exact(sock, flen)

    # Bare ACK (2 bytes echo, nothing else) = device doesn't expose TLV.
    if len(body) <= 2:
        log(f"<- READ_CONFIG: bare ACK ({len(body)}B), device doesn't expose config")
        return None

    code = struct.unpack(">H", body[:2])[0]
    log(f"<- READ_CONFIG: echo=0x{code:04x} body={len(body)}B")

    # Hand the wrapper bytes (after the 2-byte echo) to py-opendisplay's
    # parser. parse_config_response handles the [length][version][...][crc]
    # envelope + walks every TLV packet for us. Same code path HA's
    # OpenDisplay integration uses, so if we make this happy, HA should
    # be happy too.
    try:
        cfg = parse_config_response(body[2:])
    except Exception as e:
        log(f"<- READ_CONFIG: parse failed: {type(e).__name__}: {e}")
        return None

    if not cfg.displays:
        log("<- READ_CONFIG: parsed OK but no DISPLAY packet present")
        return None

    d = cfg.displays[0]
    out = {
        "width": d.pixel_width,
        "height": d.pixel_height,
        "color_scheme": d.color_scheme,
        "partial_update": bool(d.partial_update_support),
        "trans_modes": d.transmission_modes,
    }
    log(f"   DISPLAY: {out['width']}x{out['height']} "
        f"color_scheme={out['color_scheme']} "
        f"partial={out['partial_update']} trans_modes=0x{out['trans_modes']:02x}")
    return out


def format_for_color_scheme(scheme: int) -> str:
    """Map device-reported color_scheme byte to our --format string."""
    return {
        int(ColorScheme.MONO.value):         "mono",
        int(ColorScheme.BWGBRY.value):       "color6",
        int(ColorScheme.GRAYSCALE_16.value): "gray16",
    }.get(scheme, "mono")


# ---------------------------------------------------------------------------
# Image prep
# ---------------------------------------------------------------------------

_FORMAT_TO_SCHEME = {
    "mono":   ColorScheme.MONO,
    "gray16": ColorScheme.GRAYSCALE_16,
    "color6": ColorScheme.BWGBRY,
}


def encode_for_wire(path: Path,
                    fmt: str,
                    panel_size: tuple[int, int] | None,
                    fit_mode: FitMode) -> tuple[bytes, tuple[int, int]]:
    """Load + dither + encode the source image for the given wire format.

    Pipeline (all from py-opendisplay so it matches HA's path):
        PIL.Image.open -> fit_image (optional) -> dither_image -> encode_image

    Returns (encoded_bytes, (width, height)).
    """
    if not path.exists():
        raise SystemExit(f"image not found: {path}")
    img = Image.open(path)
    log(f"loaded {path.name}: mode={img.mode} size={img.size}")

    if panel_size is not None and img.size != panel_size:
        log(f"fitting {img.size} -> {panel_size} (mode={fit_mode.name})")
        img = fit_image(img.convert("RGB"), panel_size, fit_mode)
    else:
        img = img.convert("RGB")

    scheme = _FORMAT_TO_SCHEME[fmt]
    dithered = dither_image(img, scheme)
    raw = encode_image(dithered, scheme)
    return raw, img.size


# ---------------------------------------------------------------------------
# Upload flows
# ---------------------------------------------------------------------------

def _stream_data_chunks(sock: socket.socket, payload: bytes) -> None:
    """Send `payload` in CHUNK_SIZE-sized 0x71 DATA frames with per-chunk ACK."""
    total = (len(payload) + CHUNK_SIZE - 1) // CHUNK_SIZE
    sent = 0
    nchunks = 0
    rest = payload
    while rest:
        chunk, rest = rest[:CHUNK_SIZE], rest[CHUNK_SIZE:]
        nchunks += 1
        send_frame(sock, build_direct_write_data_command(chunk),
                   f"DATA #{nchunks}/{total}")
        if not recv_ack(sock, CMD_DIRECT_WRITE_DATA):
            raise SystemExit(f"device did not ACK DATA chunk #{nchunks}/{total}")
        sent += len(chunk)
    log(f"streamed {nchunks} DATA chunks ({sent}B)")


def upload_compressed(sock: socket.socket, raw: bytes) -> None:
    compressed = zlib.compress(raw, level=9)
    log(f"compressed {len(raw)}B -> {len(compressed)}B "
        f"(ratio {len(compressed) / max(1, len(raw)):.1%})")

    # py-opendisplay packs uncompressed_size + as much of the first chunk
    # as fits inside MAX_START_PAYLOAD, returns the leftover for streaming.
    start_packet, rest = build_direct_write_start_compressed(
        len(raw), compressed, MAX_START_PAYLOAD)
    send_frame(sock, start_packet, "START (compressed)")
    if not recv_ack(sock, CMD_DIRECT_WRITE_START):
        raise SystemExit("device did not ACK START")

    _stream_data_chunks(sock, rest)

    send_frame(sock, build_direct_write_end_command(refresh_mode=0), "END")
    if not recv_ack(sock, CMD_DIRECT_WRITE_END):
        raise SystemExit("device did not ACK END")


def upload_uncompressed(sock: socket.socket, raw: bytes) -> None:
    send_frame(sock, build_direct_write_start_uncompressed(), "START (uncompressed)")
    if not recv_ack(sock, CMD_DIRECT_WRITE_START):
        raise SystemExit("device did not ACK START")

    _stream_data_chunks(sock, raw)

    send_frame(sock, build_direct_write_end_command(refresh_mode=0), "END")
    if not recv_ack(sock, CMD_DIRECT_WRITE_END):
        raise SystemExit("device did not ACK END")


# ---------------------------------------------------------------------------
# Optional: listen for device-initiated 0x73 REFRESH_DONE / 0x74 TIMEOUT
# ---------------------------------------------------------------------------

def wait_for_refresh_done(sock: socket.socket, timeout: float) -> None:
    """Poll for a 0x73 (done) or 0x74 (timeout) frame from the device."""
    log(f"waiting up to {int(timeout)}s for refresh completion notice...")
    sock.settimeout(timeout)
    try:
        hdr = recv_exact(sock, 2)
        (flen,) = struct.unpack("<H", hdr)
        body = recv_exact(sock, flen)
        code = struct.unpack(">H", body[:2])[0]
        masked = code & ~ACK_FLAG
        if masked == 0x0073:
            log(f"<- refresh done (0x{code:04x})")
        elif masked == 0x0074:
            log(f"<- refresh TIMEOUT (0x{code:04x})")
        else:
            log(f"<- unexpected device frame: 0x{code:04x}")
    except (OSError, socket.timeout) as e:
        log(f"no refresh notification: {type(e).__name__}: {e}")


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", help="Path to image file (PNG/JPG/etc.)")
    ap.add_argument("--ip", "--host", default=DEFAULT_HOST,
                    help=f"HomePlate hostname or IP (default: {DEFAULT_HOST})")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT,
                    help=f"OpenDisplay TCP port (default: {DEFAULT_PORT})")
    ap.add_argument("--wait", type=int, default=180,
                    help="Max seconds to wait for the device to come online "
                         "(default: 180)")
    ap.add_argument("--uncompressed", action="store_true",
                    help="Skip zlib compression (useful for diagnosing decompress issues)")
    ap.add_argument("--no-wait-done", action="store_true",
                    help="Skip waiting for the 0x73 REFRESH_DONE notification")
    ap.add_argument("--format", choices=("auto", "mono", "gray16", "color6"), default="auto",
                    help="Wire format to encode the image as. "
                         "auto = query the device via READ_CONFIG and pick the format that "
                         "matches its advertised color scheme (default; recommended). "
                         "mono = 1bpp B/W (safe fallback for any board), "
                         "gray16 = 4bpp 16-level grayscale (Inkplate 3-bit B&W boards), "
                         "color6 = 4bpp 6-color BWGBRY (Inkplate 6 COLOR)")
    ap.add_argument("--fit", choices=("contain", "cover", "stretch", "crop"), default="contain",
                    help="How to fit the source image to the panel when sizes differ. "
                         "contain = scale to fit, pad with white (default), "
                         "cover = scale to fill, crop overflow, "
                         "stretch = distort to exact size, "
                         "crop = no scale, center-crop at native resolution. "
                         "Only applied when --format=auto (which queries panel dims).")
    return ap.parse_args()


def main() -> int:
    args = parse_args()
    log(f"target: {args.ip}:{args.port}")
    log(f"image:  {args.image}")
    log(f"format: {args.format}")
    log(f"mode:   {'uncompressed' if args.uncompressed else 'compressed (zlib)'}")

    sock = wait_for_device(args.ip, args.port, args.wait)
    try:
        # Auto-detect: query the device for its panel dimensions + color
        # scheme via READ_CONFIG, then pick a matching wire format. Falls
        # back to mono if the device doesn't expose config (bare ACK).
        fmt = args.format
        cfg = None
        if fmt == "auto":
            cfg = query_config(sock)
            if cfg is None:
                log("auto-detect: no device config, falling back to mono")
                fmt = "mono"
            else:
                fmt = format_for_color_scheme(cfg["color_scheme"])
                log(f"auto-detect: device color_scheme={cfg['color_scheme']} -> format={fmt}")

        # Encode the image. When we know the panel size from READ_CONFIG,
        # fit the source to it first so users don't have to hand-size every
        # image. Otherwise pass image through as-is (caller's responsibility).
        panel_size = (cfg["width"], cfg["height"]) if cfg else None
        fit_mode = FitMode[args.fit.upper()]
        raw, (w, h) = encode_for_wire(Path(args.image), fmt, panel_size, fit_mode)
        log(f"bitplane: {len(raw)}B ({w}x{h}, format={fmt})")

        if args.uncompressed:
            upload_uncompressed(sock, raw)
        else:
            upload_compressed(sock, raw)
        if not args.no_wait_done:
            wait_for_refresh_done(sock, timeout=60)
    finally:
        sock.close()
        log("connection closed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
