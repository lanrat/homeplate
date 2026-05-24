// OpenDisplay (Flex profile) session state machine.
//
// Pure C++. Drives one TCP session for one wake cycle. Talks to two
// abstract interfaces — transport (network I/O) and renderer (panel I/O) —
// so this code stays free of Arduino / Inkplate dependencies.

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace od {

// Bitplane formats the session may hand to the renderer.
// All formats are row-major. For multi-pixel-per-byte formats the
// highest bits hold the leftmost pixel (MSB-first).
enum class ImageFormat : uint8_t {
    Monochrome1bppMSB,   // 1 bpp; bit=1 white, bit=0 black
    Grayscale16_4bppMSB, // 4 bpp; nibble 0=black .. 15=white (linear gray)
    Color6_4bppMSB,      // 4 bpp; OpenDisplay BWGBRY firmware values
                         //   0=black, 1=white, 2=yellow, 3=red,
                         //   5=blue, 6=green (4 reserved/unused)
};

// Renderer interface — implemented in src/opendisplay_panel.cpp.
class IRenderer {
public:
    virtual ~IRenderer() = default;
    // Panel pixel dimensions for the announcement / size validation.
    virtual uint16_t width() const = 0;
    virtual uint16_t height() const = 0;
    virtual ImageFormat format() const = 0;
    // Total expected raw image bytes per upload (width*height*bpp/8).
    virtual uint32_t expectedBytes() const = 0;
    // Reported to controllers in the DisplayConfig packet of READ_CONFIG
    // responses so they know whether to issue 0x76 partial updates.
    virtual bool supportsPartialUpdate() const = 0;
    // Render a complete image buffer + refresh the panel.
    // buf is owned by the session and remains valid until this call returns.
    virtual bool renderImage(const uint8_t *buf, uint32_t len) = 0;
};

// Transport interface — implemented in src/opendisplay_wifi.cpp.
// All methods are blocking with the given timeoutMs (0 = poll).
class ITransport {
public:
    virtual ~ITransport() = default;
    // Read one frame; returns payload length, 0 on timeout, <0 on error.
    // Caller's buffer must be at least WIFI_LAN_MAX_PAYLOAD bytes.
    virtual int recvFrame(uint8_t *out, size_t outCap, uint32_t timeoutMs) = 0;
    // Send one frame payload (transport adds the length prefix).
    virtual bool sendFrame(const uint8_t *payload, uint16_t len) = 0;
    virtual bool connected() const = 0;
};

// Logger callback for diagnostics — kept simple so the lib doesn't
// depend on Serial / ESP_LOG.
using LogFn = void (*)(const char *msg);

// Optional override for upload-buffer allocation. The session needs
// allocations large enough to hold a whole compressed-or-raw image
// (up to ~500 KB for the largest Inkplate 4bpp framebuffer), which
// won't fit in ESP32 internal heap. Activity glue passes a function
// that allocates from PSRAM (e.g. ps_malloc). Default (nullptr) uses
// plain malloc — works for small panels, fails for large ones.
using AllocFn = void *(*)(size_t bytes);
using FreeFn  = void  (*)(void *ptr);
void setSessionAllocator(AllocFn alloc, FreeFn free);

// Returns true if an image was successfully received and rendered.
// loopTimeoutMs is the inactivity timeout between frames within one session.
// deepSleepSec is reported to controllers in the READ_CONFIG response so
// they know how long until our next wake; pass 0 if unknown.
bool runSession(ITransport &t, IRenderer &r, LogFn log,
                uint32_t loopTimeoutMs, uint32_t deepSleepSec = 0);

} // namespace od
