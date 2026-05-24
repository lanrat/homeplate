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

DEFAULT_HOST = "homeplate.local"
DEFAULT_PORT = 2446

CMD_DIRECT_WRITE_START = 0x0070
CMD_DIRECT_WRITE_DATA  = 0x0071
CMD_DIRECT_WRITE_END   = 0x0072
ACK_FLAG               = 0x8000

CHUNK_SIZE     = 230   # per OpenDisplay spec (BLE-derived; LAN tolerates more)
START_PAYLOAD  = 200   # max bytes in the 0x70 START packet (controller-side cap)

# Color scheme integer values (firmware convention; verified from
# epaper_dithering.palettes.ColorScheme).
SCHEME_MONO         = 0
SCHEME_BWGBRY       = 4   # 6-color (Inkplate 6 COLOR)
SCHEME_GRAYSCALE_16 = 6   # 4bpp grayscale (B&W boards in 3-bit mode)

# OpenDisplay BWGBRY firmware palette: which 4-bit value encodes which color.
# Source: py-opendisplay/encoding/images.py BWGBRY_MAP.
BWGBRY_BLACK  = 0
BWGBRY_WHITE  = 1
BWGBRY_YELLOW = 2
BWGBRY_RED    = 3
BWGBRY_BLUE   = 5   # note: 4 is reserved
BWGBRY_GREEN  = 6


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
    """Read one [len LE][cmd BE] response. Returns True on matching ACK."""
    try:
        hdr = recv_exact(sock, 2)
    except OSError as e:
        log(f"<- ack read failed: {e}")
        return False
    (flen,) = struct.unpack("<H", hdr)
    if flen == 0 or flen > 4096:
        log(f"<- bad response length {flen}")
        return False
    body = recv_exact(sock, flen)
    if len(body) < 2:
        log(f"<- short ack body: {hexdump(body)}")
        return False
    code = struct.unpack(">H", body[:2])[0]
    wanted = expected_cmd | ACK_FLAG
    if code == wanted or code == expected_cmd:
        log(f"<- ack ok: 0x{code:04x}{' (ack-flag)' if code & ACK_FLAG else ''} ({len(body)}B body)")
        return True
    log(f"<- ack mismatch: got 0x{code:04x}, expected 0x{wanted:04x} or 0x{expected_cmd:04x}")
    log(hexdump(body))
    return False


def recv_exact(sock: socket.socket, n: int) -> bytes:
    out = bytearray()
    while len(out) < n:
        chunk = sock.recv(n - len(out))
        if not chunk:
            raise OSError("connection closed mid-frame")
        out.extend(chunk)
    return bytes(out)


# ---------------------------------------------------------------------------
# Image prep
# ---------------------------------------------------------------------------

def load_mono_bitplane(path: Path) -> tuple[bytes, tuple[int, int]]:
    """Return (raw_bytes, (w, h)) packed as 1bpp MSB-first row-major."""
    img = _open(path)
    img1 = img.convert("1")  # PIL packs 1-bit as MSB-first, row-major
    raw = img1.tobytes()
    w, h = img1.size
    expected = (w * h) // 8
    if len(raw) != expected:
        log(f"WARN: PIL produced {len(raw)} bytes; expected {expected} for {w}x{h}")
    return raw, img1.size


def load_grayscale16_bitplane(path: Path) -> tuple[bytes, tuple[int, int]]:
    """Return (raw_bytes, (w, h)) packed as 4bpp grayscale (16 levels).

    Format matches OpenDisplay GRAYSCALE_16: 2 pixels per byte, high
    nibble = leftmost pixel, value 0=black .. 15=white (linear).
    """
    img = _open(path).convert("L")  # 8-bit grayscale
    w, h = img.size
    pixels = img.tobytes()
    # Quantize 0..255 -> 0..15 by right-shift; round-up by adding 8 first.
    # 255 -> 15, 0 -> 0, midpoints clean.
    nibbles = bytes((min(p + 8, 255) >> 4) for p in pixels)
    # Pack two nibbles per byte, high nibble first.
    out = bytearray((w * h + 1) // 2)
    for i in range(0, len(nibbles), 2):
        hi = nibbles[i]
        lo = nibbles[i + 1] if i + 1 < len(nibbles) else 0
        out[i // 2] = (hi << 4) | lo
    return bytes(out), (w, h)


# 6-color BWGBRY palette in RGB; ordering doesn't matter for PIL's quantizer
# but the index in this list must equal the wire BWGBRY_* value used below.
_BWGBRY_PIL_PALETTE = [
    (0, 0, 0),       # idx 0 -> BLACK
    (255, 255, 255), # idx 1 -> WHITE
    (255, 255, 0),   # idx 2 -> YELLOW
    (255, 0, 0),     # idx 3 -> RED
    (0, 0, 0),       # idx 4 -> unused (mapped to black; should never appear)
    (0, 0, 255),     # idx 5 -> BLUE
    (0, 255, 0),     # idx 6 -> GREEN
]


def load_bwgbry_bitplane(path: Path) -> tuple[bytes, tuple[int, int]]:
    """Return (raw_bytes, (w, h)) packed as 4bpp 6-color BWGBRY.

    Format matches OpenDisplay BWGBRY: 2 pixels per byte, high nibble =
    leftmost pixel, nibble value is the firmware palette index per
    BWGBRY_* constants above.
    """
    img = _open(path).convert("RGB")
    w, h = img.size
    # Build a PIL palette image with the 6 OpenDisplay BWGBRY colors so
    # PIL's quantize() does perceptual nearest-color mapping for us.
    # PIL palette is flat RGB triples padded to 256 entries.
    pal_img = Image.new("P", (1, 1))
    flat = []
    for rgb in _BWGBRY_PIL_PALETTE:
        flat += list(rgb)
    flat += [0] * (768 - len(flat))
    pal_img.putpalette(flat)
    quant = img.quantize(palette=pal_img, dither=Image.FLOYDSTEINBERG)
    # PIL palette indices are 0..6 matching _BWGBRY_PIL_PALETTE order, which
    # we deliberately chose to equal the BWGBRY firmware wire values.
    indices = quant.tobytes()
    out = bytearray((w * h + 1) // 2)
    for i in range(0, len(indices), 2):
        hi = indices[i] & 0x0F
        lo = (indices[i + 1] & 0x0F) if i + 1 < len(indices) else 0
        out[i // 2] = (hi << 4) | lo
    return bytes(out), (w, h)


def _open(path: Path) -> Image.Image:
    if not path.exists():
        raise SystemExit(f"image not found: {path}")
    img = Image.open(path)
    log(f"loaded {path.name}: mode={img.mode} size={img.size}")
    return img


# ---------------------------------------------------------------------------
# Upload flows
# ---------------------------------------------------------------------------

def upload_compressed(sock: socket.socket, raw: bytes) -> None:
    compressed = zlib.compress(raw, level=9)
    log(f"compressed {len(raw)}B -> {len(compressed)}B "
        f"(ratio {len(compressed) / max(1, len(raw)):.1%})")

    # 0x70 START with uncompressed_size + first chunk of compressed data
    head = struct.pack(">H", CMD_DIRECT_WRITE_START) + struct.pack("<I", len(raw))
    first_chunk_cap = max(0, START_PAYLOAD - len(head))
    first_chunk = compressed[:first_chunk_cap]
    rest = compressed[first_chunk_cap:]
    send_frame(sock, head + first_chunk, "START (compressed)")
    if not recv_ack(sock, CMD_DIRECT_WRITE_START):
        raise SystemExit("device did not ACK START")

    total = (len(rest) + CHUNK_SIZE - 1) // CHUNK_SIZE
    sent = 0
    nchunks = 0
    while rest:
        chunk, rest = rest[:CHUNK_SIZE], rest[CHUNK_SIZE:]
        nchunks += 1
        send_frame(sock, struct.pack(">H", CMD_DIRECT_WRITE_DATA) + chunk,
                   f"DATA #{nchunks}/{total}")
        if not recv_ack(sock, CMD_DIRECT_WRITE_DATA):
            raise SystemExit(f"device did not ACK DATA chunk #{nchunks}/{total}")
        sent += len(chunk)
    log(f"streamed {nchunks} DATA chunks ({sent}B)")

    send_frame(sock, struct.pack(">H", CMD_DIRECT_WRITE_END) + b"\x00", "END")
    if not recv_ack(sock, CMD_DIRECT_WRITE_END):
        raise SystemExit("device did not ACK END")


def upload_uncompressed(sock: socket.socket, raw: bytes) -> None:
    send_frame(sock, struct.pack(">H", CMD_DIRECT_WRITE_START), "START (uncompressed)")
    if not recv_ack(sock, CMD_DIRECT_WRITE_START):
        raise SystemExit("device did not ACK START")

    rest = raw
    total = (len(rest) + CHUNK_SIZE - 1) // CHUNK_SIZE
    sent = 0
    nchunks = 0
    while rest:
        chunk, rest = rest[:CHUNK_SIZE], rest[CHUNK_SIZE:]
        nchunks += 1
        send_frame(sock, struct.pack(">H", CMD_DIRECT_WRITE_DATA) + chunk,
                   f"DATA #{nchunks}/{total}")
        if not recv_ack(sock, CMD_DIRECT_WRITE_DATA):
            raise SystemExit(f"device did not ACK DATA chunk #{nchunks}/{total}")
        sent += len(chunk)
    log(f"streamed {nchunks} DATA chunks ({sent}B)")

    send_frame(sock, struct.pack(">H", CMD_DIRECT_WRITE_END) + b"\x00", "END")
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
    ap.add_argument("--format", choices=("mono", "gray16", "color6"), default="mono",
                    help="Wire format to encode the image as. "
                         "mono = 1bpp B/W (default; safe for any board), "
                         "gray16 = 4bpp 16-level grayscale (Inkplate 3-bit B&W boards), "
                         "color6 = 4bpp 6-color BWGBRY (Inkplate 6 COLOR)")
    return ap.parse_args()


def main() -> int:
    args = parse_args()
    log(f"target: {args.ip}:{args.port}")
    log(f"image:  {args.image}")
    log(f"format: {args.format}")
    log(f"mode:   {'uncompressed' if args.uncompressed else 'compressed (zlib)'}")

    if args.format == "mono":
        raw, (w, h) = load_mono_bitplane(Path(args.image))
        desc = "1bpp MSB-first"
    elif args.format == "gray16":
        raw, (w, h) = load_grayscale16_bitplane(Path(args.image))
        desc = "4bpp grayscale, 16 levels"
    elif args.format == "color6":
        raw, (w, h) = load_bwgbry_bitplane(Path(args.image))
        desc = "4bpp BWGBRY 6-color"
    else:
        raise SystemExit(f"unknown format: {args.format}")
    log(f"bitplane: {len(raw)}B ({w}x{h}, {desc})")

    sock = wait_for_device(args.ip, args.port, args.wait)
    try:
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
