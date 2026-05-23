#include "opendisplay_panel.h"
#include "homeplate.h"

#include <Arduino.h>

// MVP renderer:
//   - Monochrome 1bpp (MSB-first, row-major) only.
//   - Inkplate 6 COLOR (and other color/grayscale-only boards): not yet
//     handled. The capability is reported as monochrome anyway, so a
//     compliant controller will dither to 1-bit before sending — the
//     resulting mono image will render but visual fidelity is limited.
//
// Known limitations (mirror in opendisplay.md):
//   - 3-bit grayscale Inkplates report as monochrome; grayscale detail
//     lost via OpenDisplay (Flex color schemes don't define grayscale).
//   - Inkplate 6 COLOR reports monochrome in this MVP; the panel's color
//     capabilities are not yet exposed (6-color mapping is a follow-up).

ODInkplateRenderer::ODInkplateRenderer()
    : w_(E_INK_WIDTH),
      h_(E_INK_HEIGHT),
      fmt_(od::ImageFormat::Monochrome1bppMSB),
      expected_((uint32_t)E_INK_WIDTH * E_INK_HEIGHT / 8)
{
}

bool ODInkplateRenderer::renderImage(const uint8_t *buf, uint32_t len)
{
    const uint32_t need = expected_;
    if (len < need) {
        Serial.printf("[OD] short image: got %u bytes, expected %u\n", (unsigned)len, (unsigned)need);
        // Render what we have anyway — partial images still better than blank.
    }

    displayStart();
    display.clearDisplay();

    const uint16_t W = w_;
    const uint16_t H = h_;
    const uint32_t stride = (uint32_t)W / 8;

    for (uint16_t y = 0; y < H; y++) {
        const uint32_t rowStart = (uint32_t)y * stride;
        if (rowStart >= len) break;
        for (uint16_t xByte = 0; xByte < stride; xByte++) {
            const uint32_t off = rowStart + xByte;
            if (off >= len) break;
            uint8_t b = buf[off];
            // MSB-first: bit 7 is leftmost pixel.
            for (int bit = 7; bit >= 0; bit--) {
                uint16_t x = (uint16_t)(xByte * 8 + (7 - bit));
                // bit=1 -> white, bit=0 -> black (matches OpenDisplay
                // bitplane encoding for MONO).
                uint16_t color = ((b >> bit) & 0x01) ? HP_BG : HP_FG;
                display.drawPixel(x, y, color);
            }
        }
    }

    displayRefresh();
    displayEnd();
    return true;
}
