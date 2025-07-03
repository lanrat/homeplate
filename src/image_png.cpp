/**
 * This file exists to add support for drawPngFromBuffer() to the Inkplate library
 * Most of this file has functions/variables pulled directly from ImagePNG.cpp
 * With one new function drawPngFromBuffer from https://github.com/SolderedElectronics/Inkplate-Arduino-library/pull/210
 * If that PR is merged, this entire file can be removed.
 */

#include "homeplate.h"
#include <libs/pngle/pngle.h>
#include <include/Image.h> // testing for ditherbuffer....

// variables from ImagePNG.cpp
extern Image *_imagePtrPng;
static bool _pngInvert = 0;
static bool _pngDither = 0;
static int16_t lastY = -1;
static uint16_t _pngX = 0;
static uint16_t _pngY = 0;
static Image::Position _pngPosition = Image::_npos;

// copied pngle_on_draw from Inkplate ImagePNG.cpp and renamed to pngle_on_draw2
/**
 * @brief       pngle_on_draw2
 *
 * @param       pngle_t *pngle
 *              pointer to image
 * @param       uint32_t x
 *              x plane position
 * @param       uint32_t y
 *              y plane position
 * @param       uint32_t w
 *              image width
 * @param       uint32_t h
 *              image height
 * @param       uint8_t rgba[4]
 *              color
 */
void pngle_on_draw2(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t rgba[4])
{
    if (_pngPosition != Image::_npos)
    {
        _imagePtrPng->getPointsForPosition(_pngPosition, pngle_get_width(pngle), pngle_get_height(pngle), E_INK_WIDTH,
                                           E_INK_HEIGHT, &_pngX, &_pngY);
        lastY = _pngY;
        _pngPosition = Image::_npos;
    }
    if (rgba[3])
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i)
            {
                uint8_t r = rgba[0];
                uint8_t g = rgba[1];
                uint8_t b = rgba[2];

                pngle_ihdr_t *ihdr = pngle_get_ihdr(pngle);

                if (ihdr->depth == 1)
                    r = g = b = (b ? 0xFF : 0);

#if defined(ARDUINO_INKPLATECOLOR) || defined(ARDUINO_INKPLATE2) || defined(ARDUINO_INKPLATE4) ||                      \
    defined(ARDUINO_INKPLATE7)
                if (_pngInvert)
                {
                    r = 255 - r;
                    g = 255 - g;
                    b = 255 - b;
                }

                uint8_t px = _imagePtrPng->findClosestPalette(r, g, b);
#else
                uint8_t px = RGB3BIT(r, g, b);
#endif

                if (_pngDither)
                {
#if defined(ARDUINO_INKPLATECOLOR) || defined(ARDUINO_INKPLATE2) || defined(ARDUINO_INKPLATE4) ||                      \
    defined(ARDUINO_INKPLATE7)
                    px = _imagePtrPng->ditherGetPixelBmp((r << 16) | (g << 8) | (b), x + i, y + j,
                                                         _imagePtrPng->width(), 0);
#else
                    px = _imagePtrPng->ditherGetPixelBmp(RGB8BIT(r, g, b), x + i, y + j, _imagePtrPng->width(), 0);
                    if (_pngInvert)
                        px = 7 - px;
                    if (_imagePtrPng->getDisplayMode() == INKPLATE_1BIT)
                        px = (~px >> 2) & 1;
#endif
                }
                _imagePtrPng->drawPixel(_pngX + x + i, _pngY + y + j, px);
            }
    if (lastY != y)
    {
        lastY = y;
        _imagePtrPng->ditherSwap(_imagePtrPng->width());
    }
}

// from https://github.com/SolderedElectronics/Inkplate-Arduino-library/pull/210
/**
 * @brief       drawPngFromBuffer function draws png image from buffer
 *
 * @param       int32_t len
 *              size of buffer
 * @param       int x
 *              x position for top left image corner
 * @param       int y
 *              y position for top left image corner
 * @param       bool dither
 *              1 if using dither, 0 if not
 * @param       bool invert
 *              1 if using invert, 0 if not
 *
 * @return      1 if drawn successfully, 0 if not
 */
bool drawPngFromBuffer(uint8_t *buf, int32_t len, int x, int y, bool dither, bool invert)
{
    if (!buf)
        return 0;
    
    bool ret = 1;

    //_pngDither = dither;
    //_pngDither = false;
    _pngInvert = invert;
    lastY = y;

    // dithering PNG is unsupported for now
    // can't access private Image variable without bad hacks
    // if (dither)
    //     memset(_imagePtrPng->ditherBuffer, 0, sizeof ditherBuffer);

    pngle_t *pngle = pngle_new();
    _pngX = x;
    _pngY = y;
    pngle_set_draw_callback(pngle, pngle_on_draw2);

    if (pngle_feed(pngle, buf, len) < 0)
        ret = 0;

    pngle_destroy(pngle);
    return ret;
}