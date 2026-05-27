#include <qrcode.h>
#include "homeplate.h"

void serialPrintQR(QRCode qrcode);
void renderQR(QRCode qrcode, uint32_t x, uint32_t y, uint32_t size);

void displayWiFiQR()
{
    if (strlen(plateCfg.qrWifiName) == 0) {
        Serial.println("[QR] QR WiFi name not configured");
        return;
    }
    Serial.printf("Rendering wifi QR Code\n");
    char buf[1024];
    snprintf(buf, 1024, "WIFI:S:%s;T:WPA;P:%s;;", plateCfg.qrWifiName, plateCfg.qrWifiPassword);
    // Create the QR code
    static uint8_t version = 5;
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(version)];
    qrcode_initText(&qrcode, qrcodeData, version, ECC_MEDIUM, buf);
    uint32_t size = max(scaleY(15), 8);
    uint32_t padRight = scaleX(100);
    uint32_t padText = scaleX(100);

    uint32_t y = (E_INK_HEIGHT - (qrcode.size * size)) / 2;  // center QR code vertically
    uint32_t x = (E_INK_WIDTH - (qrcode.size * size) - padRight); // proportional padding on right side

    // serialPrintQR(qrcode);  // for testing
    displayStart();
#ifdef INKPLATE_HAS_DISPLAY_MODES
    display.selectDisplayMode(INKPLATE_1BIT);
#endif
    display.setTextColor(HP_FG, HP_BG);
    display.setFont(&FONT_TITLE);
    display.setTextSize(1);
    display.clearDisplay();
    displayEnd();

    renderQR(qrcode, x, y, size);

    y = y + scaleY(100); // lower text a little
    // proportional padding on each side
    uint16_t h = centerTextX("WiFi", padText, x - padText, y);
    y = y + scaleY(60);

    // do some math to resize the wifi information to fit in the bounding box
    FontSizing font = findFontSizeFit(plateCfg.qrWifiName, x-padText, (E_INK_HEIGHT-y/2));
    display.setFont(font.font);
    h = centerTextX(plateCfg.qrWifiName, padText, x - padText, y + h + scaleY(30));
    font = findFontSizeFit(plateCfg.qrWifiPassword, x - padText, (E_INK_HEIGHT-y));
    h = centerTextX(plateCfg.qrWifiPassword, padText, x - padText, y + (h + scaleY(30)) * 2);

    i2cStart();
    displayStart();
    displayRefresh();
    displayEnd();
    i2cEnd();
}

// Render an arbitrary text string as a centered QR code, auto-picking the
// smallest QR version that fits the data at ECC_MEDIUM. Used by the MQTT
// "Display QR Code" action so a user can scan a URL / vCard / arbitrary
// text without needing it pre-configured in NVS.
void displayTextQR(const char *text)
{
    if (text == nullptr || text[0] == '\0') {
        Serial.println("[QR] empty text, nothing to render");
        return;
    }
    Serial.printf("[QR] Rendering text QR: %.80s%s\n", text, strlen(text) > 80 ? "..." : "");

    // Worst-case buffer for version 40 (~3.5KB). Allocated once on the
    // stack so the version-iteration loop reuses the same memory.
    uint8_t qrcodeData[qrcode_getBufferSize(40)];
    QRCode qrcode;
    int8_t err = -1;
    uint8_t version = 1;
    for (; version <= 40; version++) {
        err = qrcode_initText(&qrcode, qrcodeData, version, ECC_MEDIUM, text);
        if (err == 0) break;
    }
    if (err != 0) {
        Serial.printf("[QR][ERROR] text too long even for version 40 (len=%u)\n", (unsigned)strlen(text));
        return;
    }
    Serial.printf("[QR] picked version %u (size=%u modules)\n", version, qrcode.size);

    // Compute pixel-per-module to fill the shorter screen axis with padding.
    uint32_t pad   = scaleY(60);
    uint32_t avail = (E_INK_WIDTH < E_INK_HEIGHT ? E_INK_WIDTH : E_INK_HEIGHT) - 2 * pad;
    uint32_t pixelsPerModule = avail / qrcode.size;
    if (pixelsPerModule < 2) pixelsPerModule = 2; // never go invisibly small
    uint32_t totalPx = qrcode.size * pixelsPerModule;
    uint32_t x = (E_INK_WIDTH  - totalPx) / 2;
    uint32_t y = (E_INK_HEIGHT - totalPx) / 2;

    displayStart();
#ifdef INKPLATE_HAS_DISPLAY_MODES
    display.selectDisplayMode(INKPLATE_1BIT);
#endif
    display.setTextColor(HP_FG, HP_BG);
    display.setTextSize(1);
    display.clearDisplay();
    displayEnd();

    renderQR(qrcode, x, y, pixelsPerModule);

    i2cStart();
    displayStart();
    displayRefresh();
    displayEnd();
    i2cEnd();
}

void renderQR(QRCode qrcode, uint32_t x, uint32_t y, uint32_t size)
{
    displayStart();
    const uint16_t foreground = HP_FG;
    const uint16_t background = HP_BG;

    // set the correct pixels
    for (uint8_t j = 0; j < qrcode.size; j++)
    {
        // Each horizontal module
        for (uint8_t i = 0; i < qrcode.size; i++)
        {
            // use filled rect to scale the image
            display.fillRect(x + (i * size), y + (j * size), size, size, qrcode_getModule(&qrcode, i, j) ? foreground : background);
        }
    }
    displayEnd();
}

void serialPrintQR(QRCode qrcode)
{
    // Top quiet zone
    Serial.print("\n\n\n\n");

    for (uint8_t y = 0; y < qrcode.size; y++)
    {

        // Left quiet zone
        Serial.print("        ");

        // Each horizontal module
        for (uint8_t x = 0; x < qrcode.size; x++)
        {

            // Print each module (UTF-8 \u2588 is a solid block)
            Serial.print(qrcode_getModule(&qrcode, x, y) ? "\u2588\u2588" : "  ");
        }

        Serial.print("\n");
    }

    // Bottom quiet zone
    Serial.print("\n\n\n\n");
}
