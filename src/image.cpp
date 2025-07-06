#include "homeplate.h"
#include <libs/pngle/pngle.h>

#define IMAGE_HTTP_REQUEST_TIMEOUT 15

void displayStats()
{
    displayStart();
    display.setTextColor(C_BLACK, C_WHITE); // Set text color to black on white
    display.setFont(&Roboto_12);
    display.setTextSize(1);
    // display status message
    display.setCursor(1155, 820);

    // text to print over box
    display.printf("[%s]", timeString().c_str());
    displayEnd();
}

// Enum to represent the different image types we can detect.
enum class ImageType {
    UNKNOWN,
    PNG,
    JPEG,
    BMP,
    WEBP,
};

/**
 * @brief Determines the image type from a buffer of bytes.
 *
 * This function checks the "magic numbers" (the first few bytes) of the
 * buffer to identify if it corresponds to a known image format.
 *
 * @param buffer A pointer to the constant unsigned char buffer containing the file data.
 * @param size The size of the buffer in bytes.
 * @return An ImageType enum value (PNG, JPEG, BMP, or UNKNOWN).
 */
ImageType getImageType(const unsigned char* buffer, size_t size) {
    // --- Basic Sanity Checks ---
    // If the buffer is null or too small to contain any magic numbers,
    // we can't determine the type.
    if (buffer == nullptr || size < 8) {
        return ImageType::UNKNOWN;
    }

    // --- PNG Check ---
    // PNG files have a fixed 8-byte signature.
    // Hex: 89 50 4E 47 0D 0A 1A 0A
    // ASCII: .PNG....
    const unsigned char png_signature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (memcmp(buffer, png_signature, 8) == 0) {
        return ImageType::PNG;
    }

    // --- JPEG Check ---
    // JPEG files start with FF D8 FF. The fourth byte varies.
    // Common signatures are FF D8 FF E0 (JFIF) or FF D8 FF E1 (EXIF).
    // We only need to check the first 3 bytes for a reliable identification.
    if (size >= 3 && buffer[0] == 0xFF && buffer[1] == 0xD8 && buffer[2] == 0xFF) {
        return ImageType::JPEG;
    }

    // --- BMP Check ---
    // BMP files start with the ASCII characters 'B' and 'M'.
    // Hex: 42 4D
    if (size >= 2 && buffer[0] == 0x42 && buffer[1] == 0x4D) {
        return ImageType::BMP;
    }

    // --- WebP Check ---
    // WebP files have "RIFF" at bytes 0-3 and "WEBP" at bytes 8-11.
    // We need at least 12 bytes to check for this signature.
    if (size >= 12 && buffer[0] == 'R' && buffer[1] == 'I' && buffer[2] == 'F' && buffer[3] == 'F' &&
        buffer[8] == 'W' && buffer[9] == 'E' && buffer[10] == 'B' && buffer[11] == 'P') {
        return ImageType::WEBP;
    }

    // --- Unknown Type ---
    // If none of the above signatures match, we return UNKNOWN.
    return ImageType::UNKNOWN;
}

// Helper function to convert ImageType enum to a string for printing.
const char* imageTypeToString(ImageType type) {
    switch (type) {
        case ImageType::PNG:    return "PNG";
        case ImageType::JPEG:   return "JPEG";
        case ImageType::BMP:    return "BMP";
        case ImageType::WEBP:   return "WEBP";
        case ImageType::UNKNOWN:return "UNKNOWN";
        default:                return "ERROR";
    }
}

bool drawImageFromURL(const char *url) {
 if (url == NULL) {
         Serial.print("[IMAGE] ERROR: got null url!");
         return false;
    }
    displayStatusMessage("Downloading image...");
    static int32_t len = E_INK_WIDTH * E_INK_HEIGHT + 100;
    Serial.printf("[IMAGE] Downloading image: %s\n", url);
    uint8_t *buff = httpGetRetry(3, url, NULL, &len, IMAGE_HTTP_REQUEST_TIMEOUT);
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
    bool good = drawImageFromBuffer(buff, len);
    free(buff);
    return good;
}

bool drawImageFromBuffer(uint8_t *buff, size_t size) {
    displayStatusMessage("Rendering image...");

    displayStart();
    display.selectDisplayMode(DISPLAY_MODE); // set grayscale mode
    display.clearDisplay();                   // refresh the display buffer before rendering.
    displayEnd();

    bool good = false;
    auto type = getImageType(buff, size);
    if (type == ImageType::UNKNOWN) {
        displayStatusMessage("Unsupported Image!");
        Serial.println("[IMAGE][ERROR] Image render unknown type!");
        good = false;
    } else {
        Serial.printf("[IMAGE] Detected image as %s\n", imageTypeToString(type));
        // display the image
        displayStart();
        switch(type) {
            case ImageType::PNG:
                good = drawPngFromBuffer(buff, size, 0, 0, USE_DITHERING, false);
                break;
            case ImageType::BMP:
                good = display.drawBitmapFromBuffer(buff, 0, 0, USE_DITHERING, false);
                break;
            case ImageType::JPEG:
                good = display.drawJpegFromBuffer(buff, size, 0, 0, USE_DITHERING, false);
                break;
            default:
                good = false;
                Serial.println("[IMAGE][ERROR] Attempt to render unsupported image!");
        }
        displayEnd();
    }
    
    if (good)
    {
        Serial.println("[IMAGE] Image render ready");
        if (DISPLAY_LAST_UPDATE_TIME) {
            displayStats();
        }
    }
    else
    {
        // If is something failed (wrong filename or format), write error message on the screen.
        displayStart();
        display.clearDisplay();
        displayEnd();
        displayStatusMessage("Image display error");
    }
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
