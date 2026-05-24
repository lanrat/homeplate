"""PlatformIO pre-build hook: link HomePlate against the slim libbt.a from
vendor/esp32-bt-slim/prebuilt/ instead of the stock pioarduino one.

Why this exists
---------------
The stock pioarduino libbt.a overflows classic ESP32's 128 KB IRAM region
(Classic BT + every profile baked in). vendor/esp32-bt-slim/prebuilt/lib/
holds a rebuilt slim variant — see vendor/esp32-bt-slim/README.md.

How it works
------------
Rather than copying our libbt.a over the framework's (which leaves state
outside the repo), we prepend our paths to SCons's LIBPATH/CPPPATH so the
linker finds OUR libbt.a first and the compiler finds OUR matching headers
first. The framework dir stays pristine; `pio pkg update` doesn't clobber
anything; multiple HomePlate checkouts with different defconfigs coexist.

The headers DO genuinely differ between slim and stock (BLUFI structs,
esp_ble_iso_api.h, ble_log, pkt_queue, etc.), so we have to use the
matching ones — linking the slim lib against stock headers would invite
ABI corruption. We auto-discover every dir under prebuilt/include/ that
contains a .h and prepend it to CPPPATH; that mirrors the structure
pioarduino-build.py adds for stock without needing a hardcoded path list.

Strict version check: aborts the build if prebuilt/manifest.txt's
target_framework_version doesn't equal the installed pioarduino
framework's package.json version. Mismatch means the slim libbt.a was
built against different framework headers, and ABI drift between BT host
code and lwip/esp_event/freertos can silently corrupt at runtime. Force
the rebuild rather than ship a flaky binary.

Wired up via `extra_scripts = vendor/esp32-bt-slim/scons_hook.py` in
platformio.ini.
"""

import json
import os
import sys

# PlatformIO injects the SCons environment as a global named `env`.
Import("env")  # noqa: F821  (provided by SCons at runtime)

# SCons exec()s this script without setting __file__, so derive our location
# from the project root (which PlatformIO exposes as env["PROJECT_DIR"]).
# If this file gets relocated, update the hardcoded subpath accordingly.
THIS_DIR = os.path.join(env["PROJECT_DIR"], "vendor", "esp32-bt-slim")  # noqa: F821
PREBUILT_DIR = os.path.join(THIS_DIR, "prebuilt")
MANIFEST = os.path.join(PREBUILT_DIR, "manifest.txt")
PREBUILT_LIB_DIR = os.path.join(PREBUILT_DIR, "lib")
PREBUILT_INC_ROOT = os.path.join(PREBUILT_DIR, "include")

PIOARDUINO_LIBS_PKG = os.path.expanduser(
    "~/.platformio/packages/framework-arduinoespressif32-libs/package.json")


def _abort(msg: str) -> None:
    # SCons's Exit() halts the build with a clean error rather than a Python
    # traceback. The printed message is the only thing the user sees, so we
    # make it actionable.
    print("=" * 72, file=sys.stderr)
    print("[esp32-bt-slim] ABORT:", file=sys.stderr)
    print(msg, file=sys.stderr)
    print("=" * 72, file=sys.stderr)
    env.Exit(1)  # noqa: F821


def _read_manifest_field(field: str) -> str | None:
    if not os.path.isfile(MANIFEST):
        return None
    with open(MANIFEST) as f:
        for line in f:
            if line.startswith(field + ":"):
                return line.split(":", 1)[1].strip()
    return None


def _installed_framework_version() -> str | None:
    if not os.path.isfile(PIOARDUINO_LIBS_PKG):
        return None
    with open(PIOARDUINO_LIBS_PKG) as f:
        return json.load(f).get("version")


def _gate() -> None:
    """Pre-flight checks before mutating the build env. Returns silently on
    success; calls _abort() on any failure."""
    if not os.path.isfile(os.path.join(PREBUILT_LIB_DIR, "libbt.a")):
        _abort(
            f"prebuilt libbt.a not found at {PREBUILT_LIB_DIR}/libbt.a\n"
            "Run: ./vendor/esp32-bt-slim/build.sh")

    target_fw = _read_manifest_field("target_framework_version")
    if not target_fw:
        _abort(
            f"target_framework_version missing from {MANIFEST}\n"
            "Run: ./vendor/esp32-bt-slim/build.sh")

    installed_fw = _installed_framework_version()
    if not installed_fw:
        _abort(
            "pioarduino framework not installed yet "
            f"(no {PIOARDUINO_LIBS_PKG}). "
            "Run `pio run` once to install it, then re-run.")

    # Exact-string match required. Prefer false positives (force rebuild on
    # any version bump) over silent ABI corruption.
    if target_fw != installed_fw:
        _abort(
            f"vendored libbt.a was built against a different framework.\n"
            f"  prebuilt manifest target_framework_version: {target_fw}\n"
            f"  installed pioarduino framework version:     {installed_fw}\n\n"
            "Resolutions:\n"
            "  - Rebuild against the installed framework:\n"
            "        ./vendor/esp32-bt-slim/build.sh\n"
            "  - Or pin pioarduino to the version the prebuilt expects.")


def _discover_include_dirs(root: str) -> list[str]:
    """Walk `root` and return every directory containing at least one .h file.

    Mirrors what pioarduino-build.py does with explicit `join()` calls for the
    stock bt/ tree, but auto-derived from the prebuilt's actual structure.
    No hardcoded path list to drift out of sync with future rebuilds.
    """
    dirs: list[str] = []
    for dirpath, _dirnames, filenames in os.walk(root):
        if any(f.endswith(".h") for f in filenames):
            dirs.append(dirpath)
    return dirs


def _install_paths() -> None:
    """Prepend our prebuilt paths to LIBPATH / CPPPATH."""
    # LIBPATH: linker resolves `-lbt` against this dir first → finds OUR
    # libbt.a instead of the framework's.
    env.Prepend(LIBPATH=[PREBUILT_LIB_DIR])  # noqa: F821

    # CPPPATH: each header-bearing subdir under prebuilt/include/ prepended,
    # so #include <esp_bt.h> etc. resolve to OUR slim headers. Stock
    # framework includes still cover everything else.
    inc_dirs = _discover_include_dirs(PREBUILT_INC_ROOT)
    # Prepend each so they all land before stock dirs; the relative order
    # among ours doesn't matter (no header-name collisions inside our tree).
    env.Prepend(CPPPATH=inc_dirs)  # noqa: F821

    print(f"[esp32-bt-slim] linking against vendored libbt.a "
          f"(framework {_read_manifest_field('target_framework_version')}, "
          f"built {_read_manifest_field('built')})")
    print(f"[esp32-bt-slim] LIBPATH prepended: {PREBUILT_LIB_DIR}")
    print(f"[esp32-bt-slim] CPPPATH prepended: {len(inc_dirs)} dir(s) "
          f"under {PREBUILT_INC_ROOT}")


_gate()
_install_paths()
