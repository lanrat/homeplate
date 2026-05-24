# OpenDisplay Test Script

`opendisplay_test.py` pushes an image to a HomePlate running the OpenDisplay activity, without needing Home Assistant or any other controller. Useful for verifying the firmware end-to-end and as a known-good reference for the wire protocol.

## Setup

Requires Python 3.10+ and [`py-opendisplay`](https://github.com/OpenDisplay/py-opendisplay) (which transitively pulls in Pillow + `epaper-dithering` — the same library Home Assistant's OpenDisplay integration uses for protocol constants, dithering, and the `READ_CONFIG` TLV parser):

```sh
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

## Usage

```sh
./opendisplay_test.py <image>                              # default: auto-detect format from device
./opendisplay_test.py inkplate10.png                       # uses READ_CONFIG to pick format
./opendisplay_test.py inkplate6-color.png --ip 192.168.1.42
./opendisplay_test.py inkplate10.png --format mono         # force mono (skip auto-detect)
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
| `--format` | `auto` | Wire format selection. `auto` (default) queries the device via `READ_CONFIG (0x0040)` and picks the format that matches its advertised color scheme — this is the recommended path and matches how a real controller (HA, py-opendisplay) would work. Override with `mono` (1bpp B/W; safe fallback for any board), `gray16` (4bpp 16-level grayscale; Inkplate B&W boards in 3-bit mode), or `color6` (4bpp 6-color BWGBRY; Inkplate 6 COLOR). The firmware also auto-detects mono uploads by byte count, so `mono` is always rendered correctly even on 4bpp-advertising panels. |
| `--wait` | `180` | Max seconds to keep retrying TCP connect while the device is asleep / not yet advertising. |
| `--uncompressed` | off | Skip zlib compression — useful for isolating "is the framing right?" from "is decompression right?". |
| `--no-wait-done` | off | Don't wait for the device's `0x73` refresh-complete notification after `END`. |
| `--fit` | `contain` | How to fit the source image to the panel when sizes differ. `contain` scales to fit (pads with white), `cover` scales to fill (crops overflow), `stretch` distorts to exact size, `crop` doesn't scale (center-crops). Only applied when `--format=auto` (when we know the panel size from `READ_CONFIG`). |

## Image sizing

In `--format=auto` mode the script queries the device for its panel dimensions and uses py-opendisplay's `fit_image()` to resize the source to match — so you can hand it any image and it'll render. Adjust the fit strategy with `--fit`.

If you override with `--format mono|gray16|color6`, no fitting happens — the source image must already match panel dimensions, otherwise the firmware will render a partial or misaligned image.

Two sample images are included:

- `inkplate10.png` — 1200×825 (Inkplate 10 / 10v2)
- `inkplate6-color.png` — 600×448 (Inkplate 6 COLOR)

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
