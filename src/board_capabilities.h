#pragma once

// Board-derived capability flags. These depend only on the ARDUINO_INKPLATE*
// build-flag macros (no library symbols), so this header is safe to include
// before <Inkplate.h>.

// ==== Supported boards ====
// Fail the build early if no supported board macro was passed via build flags.
#if !defined(ARDUINO_INKPLATE5) \
    && !defined(ARDUINO_INKPLATE5V2) \
    && !defined(ARDUINO_INKPLATE10) \
    && !defined(ARDUINO_INKPLATE10V2) \
    && !defined(ARDUINO_INKPLATE6) \
    && !defined(ARDUINO_INKPLATE6V2) \
    && !defined(ARDUINO_INKPLATE6PLUS) \
    && !defined(ARDUINO_INKPLATE6PLUSV2) \
    && !defined(ARDUINO_INKPLATE6FLICK) \
    && !defined(ARDUINO_INKPLATECOLOR)
#error "Unsupported board selection, please select a supported Inkplate board."
#endif

// ==== Touchpads ====
// Boards with capacitive touchpads (PAD1, PAD2, PAD3)
#if defined(ARDUINO_INKPLATE10) || defined(ARDUINO_INKPLATE6)
#define HAS_TOUCHPADS
// MCP23017 internal expander pin numbers for the three touchpads.
// Used for direct register bit math (INTFB / INTCAPB) and for setIntPin().
// The library used to expose these as PAD1/PAD2/PAD3 in defines.h; v11.0.0
// removed them, so we define them locally.
#define PAD1 10
#define PAD2 11
#define PAD3 12
#endif

// ==== Wake button (physical hold-to-enter-config-portal) ====
#if defined(ARDUINO_INKPLATE10) \
    || defined(ARDUINO_INKPLATE10V2) \
    || defined(ARDUINO_INKPLATE6V2) \
    || defined(ARDUINO_INKPLATE6PLUS) \
    || defined(ARDUINO_INKPLATE6PLUSV2) \
    || defined(ARDUINO_INKPLATE6FLICK) \
    || defined(ARDUINO_INKPLATECOLOR) \
    || defined(ARDUINO_INKPLATE5) \
    || defined(ARDUINO_INKPLATE5V2)
#define WAKE_BUTTON GPIO_NUM_36
// GPIO 36 is input-only and has no internal pull-up resistor on ESP32 — the
// Inkplate PCB provides an external one, so we must use INPUT (not INPUT_PULLUP).
#define WAKE_BUTTON_MODE INPUT
// the original Inkplate 6 does not have a wake button
#elif defined(ARDUINO_INKPLATE6)
#define WAKE_BUTTON GPIO_NUM_13
// GPIO 13 is a regular bidirectional pin with internal pull-up available.
#define WAKE_BUTTON_MODE INPUT_PULLUP
#endif

// ==== Panel capability flags ====
// Color panels (Inkplate 6 COLOR / 13 SPECTRA) lack partial updates and
// runtime display-mode switching — they are full-refresh, single-palette only.
// Call sites should #ifdef on these rather than on individual board macros so
// new boards (color or B&W) only need to be added to the lists above.
#if defined(ARDUINO_INKPLATECOLOR)
#define INKPLATE_IS_COLOR
#else
#define INKPLATE_HAS_PARTIAL_UPDATE
#define INKPLATE_HAS_DISPLAY_MODES
#define INKPLATE_HAS_TEMPERATURE
#endif

// ==== Device model string (for logging, MQTT discovery, TRMNL headers) ====
// Board defines from Inkplate-Arduino-library/src/include/defines.h
#if defined(ARDUINO_INKPLATE2)
#define DEVICE_MODEL "Inkplate 2"
#elif defined(ARDUINO_INKPLATE4)
#define DEVICE_MODEL "Inkplate 4"
#elif defined(ARDUINO_INKPLATE4TEMPERA)
#define DEVICE_MODEL "Inkplate 4 Tempera"
#elif defined(ARDUINO_INKPLATE5)
#define DEVICE_MODEL "Inkplate 5"
#elif defined(ARDUINO_INKPLATE5V2)
#define DEVICE_MODEL "Inkplate 5v2"
#elif defined(ARDUINO_INKPLATE6)
#define DEVICE_MODEL "Inkplate 6"
#elif defined(ARDUINO_INKPLATE6V2)
#define DEVICE_MODEL "Inkplate 6v2"
#elif defined(ARDUINO_INKPLATE6PLUS)
#define DEVICE_MODEL "Inkplate 6 Plus"
#elif defined(ARDUINO_INKPLATE6PLUSV2)
#define DEVICE_MODEL "Inkplate 6 Plusv2"
#elif defined(ARDUINO_INKPLATECOLOR)
#define DEVICE_MODEL "Inkplate 6 Color"
#elif defined(ARDUINO_INKPLATE6FLICK)
#define DEVICE_MODEL "Inkplate 6 Flick"
#elif defined(ARDUINO_INKPLATE7)
#define DEVICE_MODEL "Inkplate 7"
#elif defined(ARDUINO_INKPLATE10)
#define DEVICE_MODEL "Inkplate 10"
#elif defined(ARDUINO_INKPLATE10V2)
#define DEVICE_MODEL "Inkplate 10v2"
#else
#define DEVICE_MODEL "Inkplate (other)"
#endif
