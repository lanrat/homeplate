#include "homeplate.h"

// message buffer
static char message[MESSAGE_BUFFER_SIZE];

void setMessage(const char *m)
{
    strlcpy(message, m, MESSAGE_BUFFER_SIZE);
}

const char* getMessage()
{
    return message;
}

FontSizing findFontSizeFit(const char *m, uint16_t max_width, uint16_t max_height)
{
    int16_t x1, y1;
    FontSizing font;

    // display.setTextWrap(false);

    for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++)
    {
        font.font = fonts[i];
        display.setFont(font.font);
        // y = n to give plenty of room for text to clear height of screen
        display.getTextBounds(m, 0, (uint8_t)font.font->yAdvance, &x1, &y1, &font.width, &font.height);
        font.yAdvance = (uint8_t)font.font->yAdvance;
        font.lineHeight = font.height % font.yAdvance;
        // Serial.printf("[MESSAGE][DEBUG] Testing font # %u with height = %u and width = %u  bounds = (%d, %d) lineHeight = %d\n", i, font.height, font.width, x1, y1, font.lineHeight);
        if (font.width <= max_width && font.height <= max_height)
        {
            // Serial.printf("[MESSAGE] Using font %u with height = %u and width = %u  bounds = (%d, %d) screen(%d, %d) lineHeight = %d\n", i, font.height, font.width, x1, y1, E_INK_WIDTH, E_INK_HEIGHT, font.lineHeight);
            return font;
        }
    }
    Serial.printf("[MESSAGE][ERROR] Unable to find good font size for %s\n", m);
    return font;
}

void displayMessage(const char *m)
{
    if (m != NULL)
    {
        setMessage(m);
    }

    Serial.printf("[MESSAGE] rendering message: %s\n", message);

    displayStart();
    display.selectDisplayMode(INKPLATE_1BIT);
    display.setTextColor(BLACK, WHITE);
    display.setTextSize(1);
    display.clearDisplay();

    FontSizing font = findFontSizeFit(message, E_INK_WIDTH, E_INK_HEIGHT);
    display.setFont(font.font);

    char *savePtr;
    char *pch = NULL;
    pch = strtok_r(message, "\n", &savePtr);
    uint line = 0;
    uint16_t w, h = 0;
    int16_t y = 0;
    int16_t x = 0;
    int16_t x1b, y1b;                                                 // unused
    int16_t upperYBound = ((E_INK_HEIGHT - font.height) / 2) * 0.75f; // * 0.Nf to shift everything up a hair
    // Serial.printf("[MESSAGE][DEBUG] upper Y = %d, yAdvance=%d\n", upperYBound, font.yAdvance);
    while (pch != NULL)
    {
        line++;

        display.getTextBounds(pch, 0, 100, &x1b, &y1b, &w, &h);
        x = (E_INK_WIDTH - w) / 2;
        y = upperYBound + (font.yAdvance * line);
        // Serial.printf("[MESSAGE] Line %u, upperYBound=%d, y=%d: %s\n", line, upperYBound, y, pch);
        display.setCursor(x, y);
        display.print(pch);

        // Serial.printf("[MESSAGE][DEBUG] line: %d h=%d\n", line, h);
        if (h > font.yAdvance)
        {
            line += (h / font.yAdvance);
        }

        pch = strtok_r(NULL, "\n", &savePtr);
    }
    displayEnd();

    i2cStart();
    displayStart();
    display.display();
    displayEnd();
    i2cEnd();
}
