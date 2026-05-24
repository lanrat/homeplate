# OpenDisplay Test Script

`opendisplay_test.py` pushes an image to a HomePlate running the OpenDisplay activity, without needing Home Assistant or any other controller. Useful for verifying the firmware end-to-end and as a known-good reference for the wire protocol.

## Setup

Requires Python 3.10+, `Pillow`. A `.venv` is already provisioned in this directory:

```sh
source .venv/bin/activate
pip install pillow            # if not already installed
```

## Usage

```sh
./opendisplay_test.py <image>                              # default: homeplate.local:2446, mono, compressed
./opendisplay_test.py inkplate10.png --format gray16       # 4bpp grayscale for B&W boards
./opendisplay_test.py inkplate6-color.png --format color6  # 4bpp 6-color for Inkplate 6 COLOR
./opendisplay_test.py inkplate10.png --ip 192.168.1.42
./opendisplay_test.py inkplate10.png --uncompressed
./opendisplay_test.py inkplate10.png --wait 300
./opendisplay_test.py inkplate10.png --no-wait-done
```

### Flags

| Flag | Default | Purpose |
| --- | --- | --- |
| `image` (positional) | — | Path to the source image. Any PIL-supported format. Re-encoded as a bitplane sized for the panel per `--format`. |
| `--ip` / `--host` | `homeplate.local` | Device hostname or IP. |
| `--port` | `2446` | OpenDisplay TCP port. Must match the device's `OpenDisplay Port` setting. |
| `--format` | `mono` | Wire format: `mono` (1bpp B/W, works on any board), `gray16` (4bpp 16-level grayscale, B&W boards in 3-bit mode), `color6` (4bpp 6-color BWGBRY, Inkplate 6 COLOR). The firmware auto-detects mono vs 4bpp from byte count, so `mono` is always safe; the higher-bit formats match what the firmware advertises and give better fidelity. |
| `--wait` | `180` | Max seconds to keep retrying TCP connect while the device is asleep / not yet advertising. |
| `--uncompressed` | off | Skip zlib compression — useful for isolating "is the framing right?" from "is decompression right?". |
| `--no-wait-done` | off | Don't wait for the device's `0x73` refresh-complete notification after `END`. |

## Image sizing

The script encodes the image as a 1-bit-per-pixel bitplane at the resolution of the source PNG, packed MSB-first row-major. **The image dimensions must match the panel exactly** — the firmware allocates a buffer sized to its own panel and an oversized or undersized image will produce a misaligned or partial render.

Two sample images are included:

- `inkplate10.png` — 1200×825 (for Inkplate 10 / 10v2)
- `inkplate6-color.png` — 600×448 (for Inkplate 6 COLOR)

Add your own sized to your board's panel.

## What success looks like

```text
[15:50:50] target: homeplate.local:2446
[15:50:50] image:  inkplate10.png
[15:50:50] mode:   compressed (zlib)
[15:50:50] loaded inkplate10.png: mode=L size=(1200, 825)
[15:50:50] bitplane: 123750B (1200x825, 1bpp MSB-first)
[15:50:50] connecting to homeplate.local:2446 (max wait 180s)
[15:50:50] attempt 1 failed: ConnectionRefusedError: ... (retry, 178s left)
[15:50:57] connected on attempt 4 (remote=('192.168.2.189', 2446))
[15:50:57] compressed 123750B -> 62238B (ratio 50.3%)
[15:50:57] -> START (compressed): cmd=0x0070 frame=202B payload=200B
[15:50:57] <- ack ok: 0x8070 (ack-flag) (2B body)
[15:50:57] -> DATA #1/270: cmd=0x0071 frame=234B payload=232B
[15:50:57] <- ack ok: 0x8071 (ack-flag) (2B body)
...
[15:50:59] -> DATA #270/270: cmd=0x0071 frame=178B payload=176B
[15:50:59] <- ack ok: 0x8071 (ack-flag) (2B body)
[15:50:59] streamed 270 DATA chunks (62044B)
[15:50:59] -> END: cmd=0x0072 frame=5B payload=3B
[15:50:59] <- ack ok: 0x8072 (ack-flag) (2B body)
[15:50:59] waiting up to 60s for refresh completion notice...
[15:51:00] <- refresh done (0x8073)
[15:51:00] connection closed
```

Cross-check the device serial output (`pio device monitor -e <board>` from the repo root) — the `[OD]` lines should mirror the START/DATA/END timeline, decompress, and render.

## Troubleshooting

- **"device unreachable after Ns"** — device is asleep past `--wait`, or the firmware isn't running OpenDisplay activity. Set Default Activity = `OpenDisplay` in the portal, and bump `--wait` to cover a full sleep cycle (default Sleep Minutes = 20 → `--wait 1300` to be safe).
- **`<- ack mismatch`** — the device responded with something other than the expected ACK. Capture the hexdump from the next line and the device serial log; this usually means the firmware took an unexpected protocol path.
- **All ACKs OK, no image renders** — check device serial for `[OD] decompress failed` or `[OD] short image`. The image dimensions probably don't match the panel; see "Image sizing" above.
- **Compressed path fails, uncompressed works** — re-run with `--uncompressed` to confirm the framing is fine, then check device serial for the decompress log line.
