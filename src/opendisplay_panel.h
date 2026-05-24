// Inkplate adapter for the OpenDisplay renderer interface.
//
// Capability reporting and bitplane→framebuffer conversion. Known
// limitations (documented in opendisplay.md and at the call sites):
//   - 3-bit grayscale Inkplates report as monochrome (Flex color schemes
//     have no grayscale option). Detail loss is real.
//   - Inkplate 6 COLOR (7-color ACeP) reports as 6-color; the panel's
//     7th color (orange) is unused.

#pragma once

#include "opendisplay_session.h"

class ODInkplateRenderer : public od::IRenderer {
public:
    ODInkplateRenderer();

    uint16_t width() const override  { return w_; }
    uint16_t height() const override { return h_; }
    od::ImageFormat format() const override { return fmt_; }
    uint32_t expectedBytes() const override { return expected_; }
    bool supportsPartialUpdate() const override;
    bool renderImage(const uint8_t *buf, uint32_t len) override;

private:
    uint16_t w_, h_;
    od::ImageFormat fmt_;
    uint32_t expected_;
};
