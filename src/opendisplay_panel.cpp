#include "opendisplay_panel.h"
#include "homeplate.h"

#include <Arduino.h>

// Per-board OpenDisplay capability + rendering strategy:
//
//   Inkplate 6 COLOR  -> 6-color BWGBRY (4 bpp)
//                        Server dithers to the 6-color palette; we unpack
//                        nibbles and map directly to INKPLATE_* color
//                        constants. The panel's 7th color (orange) is
//                        unused.
//
//   3-bit grayscale   -> GRAYSCALE_16 (4 bpp)
//   (all B&W boards)    Server dithers to 16 gray levels; we downsample
//                       each nibble to the Inkplate's 3-bit grayscale by
//                       a single right-shift (16 levels -> 8 levels).
//                       Display is switched to INKPLATE_3BIT mode for the
//                       render and the framebuffer accepts gray 0-7
//                       directly via drawPixel().
//
// If a controller refuses our advertised color scheme it can fall back to
// uploading a 1-bit MONO bitplane — the session inspects the upload size
// and the renderer handles that case too. (HomePlate also has its own
// native dithering kernels for non-OpenDisplay image paths; controllers
// here pre-dither server-side, so we don't redither on-device.)

ODInkplateRenderer::ODInkplateRenderer()
    : w_(E_INK_WIDTH),
      h_(E_INK_HEIGHT)
{
#if defined(INKPLATE_IS_COLOR)
    fmt_      = od::ImageFormat::Color6_4bppMSB;
    expected_ = (uint32_t)w_ * h_ / 2;  // 2 pixels per byte
#elif defined(INKPLATE_HAS_DISPLAY_MODES)
    fmt_      = od::ImageFormat::Grayscale16_4bppMSB;
    expected_ = (uint32_t)w_ * h_ / 2;
#else
    fmt_      = od::ImageFormat::Monochrome1bppMSB;
    expected_ = (uint32_t)w_ * h_ / 8;
#endif
}

bool ODInkplateRenderer::supportsPartialUpdate() const
{
#ifdef INKPLATE_HAS_PARTIAL_UPDATE
    // HomePlate doesn't yet implement the 0x76 partial-update protocol path
    // (see opendisplay.md "Known Limitations"). Report unsupported until that
    // lands, even though the panel hardware can do partial refreshes for
    // other activities (Trmnl, HA dashboards).
    return false;
#else
    return false;
#endif
}

// --- Nibble -> Inkplate color helpers --------------------------------------

#if defined(INKPLATE_IS_COLOR)
// BWGBRY firmware palette index -> INKPLATE_* color constant.
// Wire values (per py-opendisplay encoding/images.py):
//   0=black 1=white 2=yellow 3=red 5=blue 6=green (4 reserved, 7 unused).
// Inkplate COLOR constants from boards/Inkplate6COLOR/pins.h.
static inline uint8_t bwgbryToInkplate(uint8_t v)
{
    switch (v & 0x07) {
        case 0:  return INKPLATE_BLACK;
        case 1:  return INKPLATE_WHITE;
        case 2:  return INKPLATE_YELLOW;
        case 3:  return INKPLATE_RED;
        case 5:  return INKPLATE_BLUE;
        case 6:  return INKPLATE_GREEN;
        default: return INKPLATE_WHITE;  // 4, 7: not defined by spec
    }
}
#endif

// --- Format-specific render loops ------------------------------------------

static void renderMono(const uint8_t *buf, uint32_t len, uint16_t W, uint16_t H)
{
    const uint32_t stride = (uint32_t)W / 8;
    for (uint16_t y = 0; y < H; y++) {
        const uint32_t rowStart = (uint32_t)y * stride;
        if (rowStart >= len) break;
        for (uint16_t xByte = 0; xByte < stride; xByte++) {
            const uint32_t off = rowStart + xByte;
            if (off >= len) break;
            uint8_t b = buf[off];
            for (int bit = 7; bit >= 0; bit--) {
                uint16_t x = (uint16_t)(xByte * 8 + (7 - bit));
                uint16_t color = ((b >> bit) & 0x01) ? HP_BG : HP_FG;
                display.drawPixel(x, y, color);
            }
        }
    }
}

#if defined(INKPLATE_HAS_DISPLAY_MODES) || defined(INKPLATE_IS_COLOR)
// 4 bpp, 2 pixels per byte, high nibble = leftmost pixel.
// Caller-supplied mapper turns each 4-bit palette index into the
// Inkplate color/level argument expected by drawPixel().
static void render4bpp(const uint8_t *buf, uint32_t len, uint16_t W, uint16_t H,
                       uint8_t (*map)(uint8_t))
{
    const uint32_t stride = ((uint32_t)W + 1) / 2;
    for (uint16_t y = 0; y < H; y++) {
        const uint32_t rowStart = (uint32_t)y * stride;
        if (rowStart >= len) break;
        for (uint16_t xByte = 0; xByte < stride; xByte++) {
            const uint32_t off = rowStart + xByte;
            if (off >= len) break;
            uint8_t b = buf[off];
            uint16_t x0 = (uint16_t)(xByte * 2);
            if (x0 < W)     display.drawPixel(x0,     y, map((b >> 4) & 0x0F));
            if (x0 + 1 < W) display.drawPixel(x0 + 1, y, map( b       & 0x0F));
        }
    }
}
#endif

bool ODInkplateRenderer::renderImage(const uint8_t *buf, uint32_t len)
{
    // Format auto-detection from byte count. The renderer's preferred
    // format is what we advertise to controllers (via caps()), but if a
    // controller chose to send a different (typically smaller) bit depth
    // — e.g. a controller that doesn't support GRAYSCALE_16 falling back
    // to MONO — we still want to render what arrived rather than treat
    // the bytes as the wrong format.
    od::ImageFormat actualFmt = fmt_;
    const uint32_t pixels   = (uint32_t)w_ * h_;
    const uint32_t bytesMono   = pixels / 8;
    const uint32_t bytes4bpp   = pixels / 2;
    if (len == bytesMono && fmt_ != od::ImageFormat::Monochrome1bppMSB) {
        Serial.printf("[OD] received %u bytes (matches MONO), rendering as 1bpp\n",
                      (unsigned)len);
        actualFmt = od::ImageFormat::Monochrome1bppMSB;
    } else if (len == bytes4bpp && fmt_ == od::ImageFormat::Monochrome1bppMSB) {
        // Build doesn't currently support 4bpp on this board; will fall
        // through to the default arm below and render as mono with the
        // top bit of each byte.
    } else if (len < expected_) {
        Serial.printf("[OD] short image: got %u bytes, expected %u\n",
                      (unsigned)len, (unsigned)expected_);
        // Render what we have anyway — partial images still better than blank.
    }

    displayStart();

#ifdef INKPLATE_HAS_DISPLAY_MODES
    // Switch to 3-bit grayscale for GRAYSCALE_16, 1-bit for MONO. On
    // boards without runtime modes (INKPLATE_IS_COLOR), this is a no-op.
    if (actualFmt == od::ImageFormat::Grayscale16_4bppMSB) {
        display.selectDisplayMode(INKPLATE_3BIT);
    } else if (actualFmt == od::ImageFormat::Monochrome1bppMSB) {
        display.selectDisplayMode(INKPLATE_1BIT);
    }
#endif

    display.clearDisplay();

    switch (actualFmt) {
        case od::ImageFormat::Monochrome1bppMSB:
            renderMono(buf, len, w_, h_);
            break;

#if defined(INKPLATE_HAS_DISPLAY_MODES)
        case od::ImageFormat::Grayscale16_4bppMSB:
            // 4-bit (0-15) -> 3-bit (0-7) by simple right shift. Inkplate
            // 3-bit mode uses 0=black, 7=white, which matches GRAYSCALE_16's
            // 0=black, 15=white linear ordering.
            render4bpp(buf, len, w_, h_,
                       [](uint8_t v) -> uint8_t { return (uint8_t)(v >> 1); });
            break;
#endif

#if defined(INKPLATE_IS_COLOR)
        case od::ImageFormat::Color6_4bppMSB:
            render4bpp(buf, len, w_, h_, bwgbryToInkplate);
            break;
#endif

        default:
            // Format we can't render on this build (e.g. color image to a
            // mono board). Render as if mono using the high bit of each
            // byte as a crude monochrome fallback so something still shows.
            Serial.printf("[OD] unsupported format %u on this board, mono fallback\n",
                          (unsigned)actualFmt);
            renderMono(buf, len, w_, h_);
            break;
    }

    displayRefresh();
    displayEnd();
    return true;
}
