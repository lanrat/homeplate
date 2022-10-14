#include "homeplate.h"
#include <libs/pngle/pngle.h>

// // url buffer
// static char Image_URL[MESSAGE_BUFFER_SIZE];

// void setImageURL(const char *m)
// {
//     strlcpy(Image_URL, m, MESSAGE_BUFFER_SIZE);
//     Serial.printf("Setting URL: %s -> %s\n", m, Image_URL); // TODO rm
// }

void displayStats()
{
    displayStart();
    display.setTextColor(C_BLACK, C_WHITE); // Set text color to black on white
    display.setFont(&Roboto_12);
    display.setTextSize(1);
    // display status message
    display.setCursor(1155, 820);

    // text to print over box
    display.printf("[%s]", timeString());
    displayEnd();
}

bool remotePNG(const char *url)
{
    if (url == NULL) {
         Serial.print("[IMAGE] ERROR: got null image!");
         return false;
    }
    displayStatusMessage("Downloading image...");
    Serial.print("[IMAGE] Downloading image: ");
    Serial.println(url);
    // set len for png image, or set 54373?
    static int32_t defaultLen = E_INK_WIDTH * E_INK_HEIGHT * 4 + 100;
    uint8_t *buff = display.downloadFile(url, &defaultLen);
    if (!buff)
    {
        Serial.println("[IMAGE] Download failed");
        displayStatusMessage("Download failed!");
        return false;
    }
    // check for stop after download before rendering
    if (stopActivity())
    {
        free(buff);
        displayStart();
        display.clearDisplay(); // refresh the display buffer before rendering.
        displayEnd();
        return false;
    }
    Serial.println("[IMAGE] Download done");
    displayStatusMessage("Rendering image...");

    displayStart();
    display.selectDisplayMode(INKPLATE_3BIT); // set grayscale mode
    display.clearDisplay();                   // refresh the display buffer before rendering.
    displayEnd();

    // display the image
    if (drawPngFromBuffer(buff, defaultLen, 0, 0))
    {
        Serial.println("[IMAGE] Image render ready");
        displayStats();
    }
    else
    {
        // If is something failed (wrong filename or format), write error message on the screen.
        displayStart();
        display.clearDisplay();
        displayEnd();
        displayStatusMessage("Image display error");
    }
    free(buff);
    // check for stop (could have happened inside drawPngFromBuffer())
    if (stopActivity())
    {
        displayStart();
        display.clearDisplay(); // refresh the display buffer before rendering.
        displayEnd();
        return false;
    }
    Serial.println("[IMAGE] displaying....");
    i2cStart();
    displayStart();
    display.display();
    // wait before releasing the i2c bus while the display settles. Helps prevent image fading
    vTaskDelay(0.25 * SECOND/portTICK_PERIOD_MS);
    displayEnd();
    i2cEnd();
    Serial.println("[IMAGE] displaying done.");
    return true;
}

static uint16_t _pngX = 0;
static uint16_t _pngY = 0;

// copied from ImagePNG from inkplate library with color code removed
void pngle_draw_callback(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t rgba[4])
{
    if (rgba[3])
    {
        for (int j = 0; j < h; ++j)
        {
            for (int i = 0; i < w; ++i)
            {
                if (stopActivity())
                    return;
                uint8_t r = rgba[0];
                uint8_t g = rgba[1];
                uint8_t b = rgba[2];

                pngle_ihdr_t *ihdr = pngle_get_ihdr(pngle);

                // if 1 bit....
                if (ihdr->depth == 1)
                    r = g = b = (b ? 0xFF : 0);

                // RGB3BIT(r, g, b) ((54UL * (r) + 183UL * (g) + 19UL * (b)) >> 13)
                uint8_t px = RGB3BIT(r, g, b);

                display.drawPixel(_pngX + x + i, _pngY + y + j, px);
            }
        }
    }
}

// Depending on task priority, this can take between 5-30s
bool drawPngFromBuffer(uint8_t *buff, int32_t len, int x, int y)
{
    _pngX = x;
    _pngY = y;

    bool ret = 1;

    if (!buff)
        return 0;

    pngle_t *pngle = pngle_new();
    pngle_set_draw_callback(pngle, pngle_draw_callback);

    displayStart();
    if (pngle_feed(pngle, buff, len) < 0)
    {
        ret = 0;
    }
    displayEnd();

    pngle_destroy(pngle);
    return ret;
}

// returns height
// y value should be the top of the text location
uint16_t centerTextX(const char *t, int16_t x1, int16_t x2, int16_t y, bool lock)
{
    // center text
    int16_t x1b, y1b;
    uint16_t w, h;
    // y = n to give plenty of room for text to clear height of screen
    display.getTextBounds(t, 0, 100, &x1b, &y1b, &w, &h);

    int16_t x = ((x2 - x1) - w) / 2 + x1;

    if (lock)
        displayStart();
    display.setCursor(x, y + h);
    display.print(t);
    if (lock)
        displayEnd();
    return h;
}

// NOTE I2C & display locks MUST NOT be held by caller.
void displayStatusMessage(const char *format, ...)
{
    static char statusBuffer[100];
    // setup format string
    va_list argptr;
    va_start(argptr, format);
    vsnprintf(statusBuffer, 100, format, argptr);
    va_end(argptr);

    Serial.printf("[STATUS] %s\n", statusBuffer);

    i2cStart();
    displayStart();
    display.selectDisplayMode(INKPLATE_1BIT);
    display.setTextColor(BLACK, WHITE);       // Set text color to black on white
    display.setFont(&Roboto_16);
    display.setTextSize(1);

    const int16_t pad = 3;           // padding
    const int16_t mar = 5;           // margin
    const int16_t statusWidth = 400; // extra space to clear for text
    const int16_t x = mar;
    const int16_t y = E_INK_HEIGHT - mar;

    // get text size for box
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(statusBuffer, x, y, &x1, &y1, &w, &h);

    // background box to set internal buffer colors
    display.fillRect(x - pad, y - pad - h, max(w + (pad * 2), statusWidth), h + (pad * 2), WHITE);
    //Serial.printf("fillRect(x:%u, y:%u, w:%u, h:%u)\n", x-pad, y-pad-h, max(w+(pad*2), 400), h+(pad*2));
    display.partialUpdate(sleepBoot);

    // display status message
    display.setCursor(x, y);

    // text to print over box
    display.print(statusBuffer);
    display.partialUpdate(sleepBoot);
    displayEnd();
    i2cEnd();
}

void splashScreen()
{
    static const char *splashName = "HomePlate";
    displayStart();
    display.selectDisplayMode(INKPLATE_1BIT); // testing
    display.setTextColor(BLACK, WHITE);       // Set text color to black on white
    display.setFont(&Roboto_128);
    display.setTextSize(1);

    // Roboto_64, size: 1, center (439, 437)
    // Roboto_64, size: 2, center (279, 461)
    // Roboto_128, size: 1, center (285, 461)
    int16_t x = 285;
    int16_t y = 461;
    bool dynamicPlacement = false;
    if (dynamicPlacement)
    {
        //get text size for box
        int16_t x1, y1;
        uint16_t w, h;
        display.getTextBounds(splashName, 100, 100, &x1, &y1, &w, &h);
        x = (E_INK_WIDTH - w) / 2;
        y = (E_INK_HEIGHT - h) / 2 + h;
        Serial.printf("SplashScreen location (%d, %d)\n", x, y);
    }

    display.setCursor(x, y);

    // text to print over box
    display.print(splashName);
    displayEnd();
    i2cStart();
    displayStart();
    display.partialUpdate(sleepBoot);
    displayEnd();
    i2cEnd();
}
