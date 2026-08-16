#include <HTTPClient.h> // HTTP_CODE_NOT_MODIFIED
#include "homeplate.h"

#define IMAGE_HTTP_REQUEST_TIMEOUT 15

// Per-request dither override stash. Set by an upstream source (e.g. MQTT
// img action's "dither" field) before triggering an image render; consumed
// and reset to -1 on the next drawImageFromURL() entry. -1 means "no
// override". HTTP X-Dither response header takes precedence over this when
// both are present.
static int8_t pendingDitherOverride = -1;

void setPendingDitherOverride(int8_t v) { pendingDitherOverride = v; }

// Conditional-GET cache. Holds the validators the server sent with the image
// currently on the panel, so the next fetch can ask "still the same?" and skip
// the render (and the e-ink refresh) on a 304.
//
// RTC_DATA_ATTR because the panel keeps its pixels across deep sleep, so a
// validator captured on the previous wake still describes what is on the glass.
// A power cycle zeroes RTC memory, which is exactly right: setup() clears and
// repaints the panel on a cold boot, so a surviving validator would be lying.
static const size_t IMG_ETAG_SIZE = 64;
static const size_t IMG_LAST_MODIFIED_SIZE = 40; // "Www, dd Mmm yyyy hh:mm:ss GMT" + slack
RTC_DATA_ATTR static char imgEtag[IMG_ETAG_SIZE] = "";
RTC_DATA_ATTR static char imgLastModified[IMG_LAST_MODIFIED_SIZE] = "";
// Validators are per-resource. The IMG activity renders arbitrary URLs pushed
// over MQTT and TRMNL renders its own image_url, so without this an ETag from
// one URL could be sent to another — and a coincidental 304 would wedge the
// wrong image on screen.
RTC_DATA_ATTR static uint32_t imgUrlHash = 0;
// True only while the cached image is believed to be the thing on the panel.
// Cleared by anything that puts pixels on the glass — full repaints via
// displayRefresh, partial ones via displayStatusMessage and
// displayBatteryWarning — so a 304 can never skip a render that something
// else has made necessary.
RTC_DATA_ATTR static bool imgOnScreen = false;

// FNV-1a. Only used to tell "same URL as last time" from "different URL"; a
// collision costs one stale frame until the next change, not correctness of
// anything persistent.
static uint32_t urlHash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s != '\0'; s++)
    {
        h ^= (uint8_t)*s;
        h *= 16777619u;
    }
    return h;
}

void invalidateImageCache()
{
    if (imgOnScreen || imgEtag[0] != '\0' || imgLastModified[0] != '\0')
    {
        Serial.println("[IMAGE] panel repainted, dropping conditional-GET validators");
    }
    imgEtag[0] = '\0';
    imgLastModified[0] = '\0';
    imgUrlHash = 0;
    imgOnScreen = false;
}

// True when we hold validators for this exact URL and believe it is still the
// image on the panel — i.e. a 304 would be safe to act on.
static bool imageCacheUsable(const char *url)
{
    return imageCacheArmed() && imgUrlHash == urlHash(url);
}

// URL-agnostic form of the above: "this wake might end without repainting".
// Callers that paint progress text before the target URL is known use this to
// stay quiet, since a skipped render leaves that text stranded on the panel.
bool imageCacheArmed()
{
    return imgOnScreen && (imgEtag[0] != '\0' || imgLastModified[0] != '\0');
}

// Remember the validators for the image we just put on the panel. Absent
// headers leave the cache empty, so a server that sends neither simply never
// gets a conditional request and nothing changes for it.
static void rememberImageValidators(const char *url, std::map<String, String> &respHeaders)
{
    imgEtag[0] = '\0';
    imgLastModified[0] = '\0';

    // Drop rather than truncate: a truncated validator is a different string,
    // so it would never match and we'd pay for the conditional request forever.
    auto etag = respHeaders.find("ETag");
    if (etag != respHeaders.end() && etag->second.length() < sizeof(imgEtag))
    {
        strlcpy(imgEtag, etag->second.c_str(), sizeof(imgEtag));
    }
    auto lastMod = respHeaders.find("Last-Modified");
    if (lastMod != respHeaders.end() && lastMod->second.length() < sizeof(imgLastModified))
    {
        strlcpy(imgLastModified, lastMod->second.c_str(), sizeof(imgLastModified));
    }

    imgUrlHash = urlHash(url);
    imgOnScreen = (imgEtag[0] != '\0' || imgLastModified[0] != '\0');

    if (imgOnScreen)
    {
        Serial.printf("[IMAGE] cached validators: ETag(%s) Last-Modified(%s)\n",
                      imgEtag[0] ? imgEtag : "-", imgLastModified[0] ? imgLastModified : "-");
    }
    else
    {
        Serial.println("[IMAGE] server sent no ETag/Last-Modified, unchanged-image skip unavailable");
    }
}

// Enum to represent the different image types we can detect.
enum class ImageType {
    UNKNOWN,
    PNG,
    JPEG,
    BMP,
    WEBP,
};

// Forward declaration
ImageType getImageType(const unsigned char* buffer, size_t size);

struct imageInfo {
    ImageType type;
    int width;
    int height;
};

imageInfo getImageInfo(uint8_t *buff, size_t size) {
    imageInfo result = {ImageType::UNKNOWN, 0, 0};
    
    if (buff == nullptr || size < 8) {
        return result;
    }
    
    ImageType type = getImageType(buff, size);
    result.type = type;
    
    switch (type) {
        case ImageType::PNG:
            if (size >= 24) {
                // PNG IHDR chunk starts at byte 8, width at bytes 16-19, height at bytes 20-23
                result.width = (buff[16] << 24) | (buff[17] << 16) | (buff[18] << 8) | buff[19];
                result.height = (buff[20] << 24) | (buff[21] << 16) | (buff[22] << 8) | buff[23];
            }
            break;
            
        case ImageType::JPEG:
            // Find SOF (Start of Frame) marker
            for (size_t i = 2; i < size - 9; i++) {
                if (buff[i] == 0xFF && (buff[i+1] == 0xC0 || buff[i+1] == 0xC2)) {
                    // SOF marker found, dimensions are at offset +5 (height) and +7 (width)
                    result.height = (buff[i+5] << 8) | buff[i+6];
                    result.width = (buff[i+7] << 8) | buff[i+8];
                    break;
                }
            }
            break;
            
        case ImageType::BMP:
            if (size >= 26) {
                // BMP width is at bytes 18-21, height at bytes 22-25
                result.width = buff[18] | (buff[19] << 8) | (buff[20] << 16) | (buff[21] << 24);
                result.height = buff[22] | (buff[23] << 8) | (buff[24] << 16) | (buff[25] << 24);
                // BMP height can be negative (top-down), take absolute value
                if (result.height < 0) result.height = -result.height;
            }
            break;
            
        case ImageType::WEBP:
            if (size >= 30) {
                // Check for VP8 or VP8L format
                if (buff[12] == 'V' && buff[13] == 'P' && buff[14] == '8' && buff[15] == ' ') {
                    // VP8 format - dimensions start at byte 26
                    result.width = ((buff[26] | (buff[27] << 8)) & 0x3FFF);
                    result.height = ((buff[28] | (buff[29] << 8)) & 0x3FFF);
                } else if (buff[12] == 'V' && buff[13] == 'P' && buff[14] == '8' && buff[15] == 'L') {
                    // VP8L format - dimensions start at byte 21
                    uint32_t bits = buff[21] | (buff[22] << 8) | (buff[23] << 16) | (buff[24] << 24);
                    result.width = (bits & 0x3FFF) + 1;
                    result.height = ((bits >> 14) & 0x3FFF) + 1;
                }
            }
            break;
            
        default:
            break;
    }
    
    return result;
}

void displayStats()
{
    displayStart();
    display.setTextColor(HP_FG, HP_BG); // Set text color to foreground on background
    display.setFont(&FONT_SMALL);
    display.setTextSize(1);

    // measure the time string to position from right edge
    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "[%s]", timeString().c_str());
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(timeBuf, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(HP_WIDTH - w - 5, HP_HEIGHT - 5);

    display.print(timeBuf);
    displayEnd();
}

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
    // Snapshot and consume any pending override (e.g. set by an MQTT img command).
    int8_t mqttOverride = pendingDitherOverride;
    pendingDitherOverride = -1;

    // Ask the server whether the image we already have is still current. Only
    // meaningful when we hold validators for this same URL and believe it is
    // still the thing on the panel.
    std::map<String, String> reqHeaders;
    bool conditional = imageCacheUsable(url);
    if (conditional)
    {
        if (imgEtag[0] != '\0')
            reqHeaders["If-None-Match"] = imgEtag;
        if (imgLastModified[0] != '\0')
            reqHeaders["If-Modified-Since"] = imgLastModified;
    }

    // displayStatusMessage paints on the panel, and a 304 skips the full update
    // that would otherwise wipe it — leaving "Downloading image..." stranded
    // over an image that never changed. Only announce the download when a
    // render is certain to follow.
    if (!conditional)
    {
        displayStatusMessage("Downloading image...");
    }

    // Intentionally the compile-time E_INK_* constants: this is a buffer size,
    // and width*height is the same in either orientation.
    static int32_t len = E_INK_WIDTH * E_INK_HEIGHT + 100;
    Serial.printf("[IMAGE] Downloading image%s: %s\n", conditional ? " (conditional)" : "", url);
    std::map<String, String> respHeaders;
    int httpCode = 0;
    uint8_t *buff = httpGetRetry(3, url, conditional ? &reqHeaders : NULL, &len,
                                 IMAGE_HTTP_REQUEST_TIMEOUT, &respHeaders, &httpCode);

    if (!buff && httpCode == HTTP_CODE_NOT_MODIFIED)
    {
        // Re-check rather than trusting the pre-request test: another activity
        // could have repainted the panel while the request was in flight, which
        // would make "unchanged" true of the server and false of the glass.
        if (imageCacheUsable(url))
        {
            Serial.println("[IMAGE] unchanged (304), skipping render");
            return true;
        }
        // Either the panel was repainted while the request was in flight, or
        // the server sent an unsolicited 304 (we asked unconditionally). Either
        // way we have no body and nothing trustworthy on screen — go get it.
        Serial.printf("[IMAGE] unusable 304 (%s), re-fetching unconditionally\n",
                      conditional ? "panel repainted mid-request" : "not requested conditionally");
        len = E_INK_WIDTH * E_INK_HEIGHT + 100;
        buff = httpGetRetry(3, url, NULL, &len, IMAGE_HTTP_REQUEST_TIMEOUT, &respHeaders, &httpCode);
    }

    if (!buff)
    {
        Serial.println("[IMAGE] Download failed");
        // Don't paint the failure banner if a new activity has been queued —
        // it would briefly show through and (on non-partial-update boards)
        // clobber message[] via displayCriticalMessage->displayMessage.
        if (!stopActivity()) {
            displayCriticalMessage("Image Download Failed");
        }
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

    // HTTP X-Dither header wins over MQTT-pending override.
    int8_t ditherOverride = mqttOverride;
    auto it = respHeaders.find("X-Dither");
    if (it != respHeaders.end())
    {
        ditherOverride = parseDitherName(it->second.c_str());
    }

    // renderOk, not the return value: drawImageFromBuffer returns true whenever
    // it painted *something*, including the "Image Display Error" banner it
    // draws when the decode fails. Caching validators on that would let the
    // next 304 skip past a stuck error screen.
    bool renderOk = false;
    bool good = drawImageFromBuffer(buff, len, false, ditherOverride, &renderOk);
    free(buff);
    // drawImageFromBuffer's own displayRefresh() invalidated the cache on its
    // way through, so this both re-arms it and leaves a failed or aborted
    // render with no claim on the panel.
    if (good && renderOk)
    {
        rememberImageValidators(url, respHeaders);
    }
    return good;
}

bool drawImageFromBuffer(uint8_t *buff, size_t size, bool center, int8_t ditherOverride, bool *renderOk) {
    WakeLock lock("image-render", 60);
    if (renderOk) {
        *renderOk = false;
    }
    displayStatusMessage("Rendering image...");

    displayStart();
#ifdef INKPLATE_HAS_DISPLAY_MODES
    display.selectDisplayMode(DISPLAY_MODE); // set grayscale mode
#endif
    display.clearDisplay();                   // refresh the display buffer before rendering.
    displayEnd();

    uint8_t effectiveKernel = (ditherOverride < 0) ? plateCfg.ditherKernel : (uint8_t)ditherOverride;
    bool useDither = effectiveKernel != 0;
    if (useDither) {
        // display.image is `Image` on B&W boards and `ImageColor` on the 6COLOR
        // board; both expose a class-local DitherKernel enum with identical
        // values (0=FloydSteinberg .. 6=ReducedDiffusion). decltype picks the
        // right scope for each board without an #ifdef.
        display.image.setDitherKernel((decltype(display.image)::DitherKernel)(effectiveKernel - 1));
    }
    Serial.printf("[IMAGE] Dither: %s%s\n",
        ditherKernelName(effectiveKernel),
        (ditherOverride >= 0) ? " (override)" : "");

    bool good = false;
    auto img = getImageInfo(buff, size);
    if (img.type == ImageType::UNKNOWN) {
        Serial.println("[IMAGE][ERROR] Image render unknown type!");
        displayCriticalMessage("Unsupported Image");
        good = false;
    } else {
        Serial.printf("[IMAGE] Detected image as %s %dx%d\n", imageTypeToString(img.type), img.width, img.height);
        int xLoc = 0;
        int yLoc = 0;
        if (center) {
            if (img.width < HP_WIDTH) {
                xLoc = (HP_WIDTH - img.width) / 2;
            }
            if (img.height < HP_HEIGHT) {
                yLoc = (HP_HEIGHT - img.height) / 2;
            }
            Serial.printf("[IMAGE] Centering Image at %dx%d\n",xLoc, yLoc);
        }
        // display the image
        displayStart();
        switch(img.type) {
            case ImageType::PNG:
                good = display.image.drawPngFromBuffer(buff, size, xLoc, yLoc, useDither, false);
                break;
            case ImageType::BMP:
                good = display.image.drawBitmapFromBuffer(buff, xLoc, yLoc, useDither, false);
                break;
            case ImageType::JPEG:
                good = display.image.drawJpegFromBuffer(buff, size, xLoc, yLoc, useDither, false);
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
        if (plateCfg.displayLastUpdateTime) {
            displayStats();
        }
    }
    else
    {
        // If something failed (wrong filename or format), write error message on
        // the screen — unless a new activity has been queued, in which case
        // skip the banner so it doesn't clobber message[] / steal the screen
        // from the about-to-run activity.
        if (!stopActivity()) {
#ifdef INKPLATE_HAS_PARTIAL_UPDATE
            displayStart();
            display.clearDisplay();
            displayEnd();
#endif
            displayCriticalMessage("Image Display Error");
        }
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
    displayRefresh();
    // wait before releasing the i2c bus while the display settles. Helps prevent image fading
    vTaskDelay(0.25 * SECOND/portTICK_PERIOD_MS);
    displayEnd();
    i2cEnd();
    Serial.println("[IMAGE] displaying done.");
    if (renderOk) {
        *renderOk = good;
    }
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

#ifdef INKPLATE_HAS_PARTIAL_UPDATE
    // Partial-paint counterpart to the invalidation in displayRefresh. Most
    // status text outlives the call — "Image Download Failed", "WiFi failed!",
    // an OTA error — with no full repaint behind it to clear it. Skipping the
    // next render on a 304 would strand that text over an otherwise-correct
    // image until the image itself changed, so any paint here gives up the
    // right to skip. Callers that can avoid painting (the boot message, the
    // download notice) gate on imageCacheArmed() instead; without that gating
    // this line would fire every wake and the skip would never happen.
    invalidateImageCache();
    i2cStart();
    displayStart();
    display.selectDisplayMode(INKPLATE_1BIT);
    display.setTextColor(HP_FG, HP_BG);
    display.setFont(&FONT_BODY);
    display.setTextSize(1);

    const int16_t pad = 3;           // padding
    const int16_t mar = 5;           // margin
    const int16_t statusWidth = scaleX(400); // extra space to clear for text
    const int16_t x = mar;
    const int16_t y = HP_HEIGHT - mar;

    // get text size for box
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(statusBuffer, x, y, &x1, &y1, &w, &h);

    // background box to set internal buffer colors
    display.fillRect(x - pad, y - pad - h, max(w + (pad * 2), statusWidth), h + (pad * 2), HP_BG);
    display.partialUpdate(sleepBoot);

    // display status message
    display.setCursor(x, y);

    // text to print over box
    display.print(statusBuffer);
    display.partialUpdate(sleepBoot);
    displayEnd();
    i2cEnd();
#endif
}

void splashScreen()
{
#ifdef INKPLATE_HAS_PARTIAL_UPDATE
    static const char *splashName = "HomePlate";
    displayStart();
    display.selectDisplayMode(INKPLATE_1BIT);
    display.setTextColor(HP_FG, HP_BG);

    FontSizing font = findFontSizeFit(splashName, HP_WIDTH, HP_HEIGHT);
    display.setFont(font.font);
    display.setTextSize(1);

    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(splashName, 100, 100, &x1, &y1, &w, &h);
    int16_t x = (HP_WIDTH - w) / 2;
    int16_t y = (HP_HEIGHT - h) / 2 + h;

    display.setCursor(x, y);
    display.print(splashName);
    displayEnd();
    i2cStart();
    displayStart();
    display.partialUpdate(sleepBoot);
    displayEnd();
    i2cEnd();
#endif
}
