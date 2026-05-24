# Slim BT prebuilt for HomePlate (classic ESP32)

## Why this exists

HomePlate's OpenDisplay BLE transport requires linking a BLE GATT server into the firmware. On classic ESP32, the stock pioarduino prebuilt `libbt.a` overflows the 128 KB IRAM region by ~4 KB because it ships with everything turned on: Classic BT, A2DP, AVRCP, SPP, HFP, BLE Mesh, BLUFI, GATT client, BLE+Classic combined controller mode.

We don't need any of that. HomePlate only wants:

- BLE controller in **BLE-only** mode (drops the BR/EDR controller code — biggest IRAM win)
- BLE GATT **server** (no client, no Mesh, no BLUFI)
- No Classic BT or any Classic profiles

This directory contains the recipe to rebuild a slim `libbt.a` (plus its sibling controller/host libs) using [esp32-arduino-lib-builder](https://github.com/espressif/esp32-arduino-lib-builder), and then vendor the result into the pioarduino framework directory.

## What's here

```text
configs/defconfig.bleslim   # sdkconfig overlay applied during rebuild
build.sh                    # one-shot: docker rebuild → prebuilt/ + manifest
scons_hook.py               # platformio.ini pre-build hook (link against prebuilt/)
prebuilt/                   # git-tracked: ready-to-link artifacts
  ├── lib/libbt.a           # ~2.5 MB slim library
  ├── include/bt/...        # matching headers
  └── manifest.txt          # build provenance + target framework version
out/                        # docker scratch dir (gitignored, large)
```

## Normal use (no install step needed)

The prebuilt artifacts are checked into git AND `platformio.ini` wires up [scons_hook.py](scons_hook.py) so the build links against `prebuilt/lib/libbt.a` and prepends matching headers from `prebuilt/include/bt/` directly. The pioarduino framework directory stays pristine — nothing is copied or overwritten.

Just build:

```bash
pio run -e inkplate10
```

On the first build line you'll see:

```text
[esp32-bt-slim] linking against vendored libbt.a (framework 5.5.0+sha.87912cd291, built 2026-05-23T20:05:00Z)
[esp32-bt-slim] LIBPATH prepended: .../vendor/esp32-bt-slim/prebuilt/lib
[esp32-bt-slim] CPPPATH prepended: 12 dir(s) under .../vendor/esp32-bt-slim/prebuilt/include
```

The hook strictly refuses to proceed if `prebuilt/manifest.txt`'s `target_framework_version` doesn't match the installed pioarduino framework's `package.json` version — that mismatch means the prebuilt is stale and you need to rebuild (next section).

## Rebuild procedure (only when stale)

Required when:

- pioarduino framework version was bumped and the hook aborts the build.
- You edited `configs/defconfig.bleslim`.
- You're verifying the prebuilt is reproducible.

Prerequisites: Docker.

```bash
./vendor/esp32-bt-slim/build.sh        # 30-60 min first time, faster on rebuilds (ccache)
git add vendor/esp32-bt-slim/prebuilt  # commit the refreshed libbt.a + manifest
pio run -e inkplate10
```

`build.sh` runs the lib-builder container, copies the slim artifacts into `prebuilt/`, and rewrites `prebuilt/manifest.txt` with the current build's provenance (lib-builder commit, IDF branch, defconfig SHA, etc.).

## How version checking works

`prebuilt/manifest.txt` records the pioarduino framework version this `libbt.a` was built against (e.g. `5.5.0+sha.87912cd291`). The SCons hook reads it and compares against:

```bash
cat ~/.platformio/packages/framework-arduinoespressif32-libs/package.json | grep version
```

Exact-string match required. ABI drift between `libbt.a` and the rest of the framework (lwip, esp_event, freertos) can corrupt at runtime in non-obvious ways, so we prefer the false positive of "force a rebuild on any version bump" over silent breakage.

## What the overlay disables (cheat sheet)

| Flag flipped off | Why we don't need it |
| --- | --- |
| `CONFIG_BT_CLASSIC_ENABLED` | We only need BLE. |
| `CONFIG_BT_A2DP_ENABLE` | Audio profile — no audio. |
| `CONFIG_BT_AVRCP_ENABLED` | Audio remote control — no audio. |
| `CONFIG_BT_SPP_ENABLED` | Serial port profile over Classic — unused. |
| `CONFIG_BT_HFP_*` | Hands-free phone profile — unused. |
| `CONFIG_BT_GOEPC_ENABLED` | Generic OBEX client — unused. |
| `CONFIG_BT_GATTC_ENABLE` | GATT *client* — we're server-only. |
| `CONFIG_BLE_MESH` | BLE Mesh networking — unused. |
| `CONFIG_BT_BLE_BLUFI_ENABLE` | BLE-based WiFi provisioning — we use WiFiManager. |
| `CONFIG_BTDM_CTRL_MODE_BTDM` | Replaced with `CONFIG_BTDM_CTRL_MODE_BLE_ONLY` — biggest IRAM saving. |

We deliberately keep the Bluedroid host (not NimBLE) so pioarduino's hardcoded `-lbt` link line in `pioarduino-build.py` keeps working unchanged.
