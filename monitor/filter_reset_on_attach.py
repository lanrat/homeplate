# Custom PlatformIO monitor filter: reboot the board right after the serial
# monitor attaches, so the boot log is captured from the first line.
#
# Why: after flashing, esptool hard-resets the board and it boots immediately,
# but the monitor takes a second or two to start — losing the entire boot log.
# Boards whose USB-UART auto-reset circuit doesn't fire when the monitor opens
# (the Inkplate 13 Spectra) never replay it. This filter runs inside the
# monitor process and pulses EN via RTS on the monitor's own (already open)
# port — the same "hard resetting via RTS pin" esptool does — so there is no
# race and no second port open (the monitor holds the port exclusively).
#
# Enabled per-env via `monitor_filters = ..., reset_on_attach`. Works with
# both `pio run -t monitor` and `pio device monitor`. The env should also set
# `monitor_dtr = 0` so the boot-strap pin (IO0) stays released and the pulse
# boots the chip in normal (non-download) mode.

import threading
import time

from platformio.public import DeviceMonitorFilterBase


class ResetOnAttach(DeviceMonitorFilterBase):
    NAME = "reset_on_attach"

    def set_running_terminal(self, terminal):
        super().set_running_terminal(terminal)
        threading.Thread(target=self._pulse_reset, daemon=True).start()

    def _pulse_reset(self):
        time.sleep(0.5)  # let the terminal finish starting its reader
        try:
            ser = self.get_running_terminal().serial
            ser.rts = True  # assert -> EN low, chip held in reset
            time.sleep(0.1)
            ser.rts = False  # release -> chip boots, monitor already listening
            print("[reset_on_attach] board reset, capturing boot log")
        except Exception as exc:  # never take down the monitor over this
            print(
                "[reset_on_attach] reset failed (%s) — press the board's reset "
                "button to replay the boot log" % exc
            )
