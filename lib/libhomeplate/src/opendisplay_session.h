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
enum class ImageFormat : uint8_t {
    Monochrome1bppMSB, // 1 bit per pixel, row-major, MSB-first within byte
    Color6bpp4bit,     // 4 bits per pixel, palette index (Inkplate 6 COLOR)
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

// Returns true if an image was successfully received and rendered.
// loopTimeoutMs is the inactivity timeout between frames within one session.
bool runSession(ITransport &t, IRenderer &r, LogFn log, uint32_t loopTimeoutMs);

} // namespace od
