// Waveform selection utility for Inkplate 10.
//
// Lets you preview each of the available 3-bit grayscale waveforms and burn
// the chosen one into ESP32 EEPROM. Useful if the factory waveform was
// accidentally overwritten or you need a different look on your panel.
//
// The Inkplate v11.0.0 library now provides setWaveform(num, burnToEEPROM)
// which both previews and persists, replacing the manual changeWaveform /
// burnWaveformToEEPROM bit-banging this file used to do.
//
// Usage (via Serial monitor @ 115200):
//   1..5  preview that waveform (renders the gradient)
//   test  render the demo image with the currently-selected waveform
//   ok    burn the currently-selected waveform into EEPROM and exit

#if !defined(ARDUINO_INKPLATE10) && !defined(ARDUINO_INKPLATE10V2)
#error "Wrong board selection for this example, please select Soldered Inkplate10 in platformio.ini [user] section."
#endif

#include <Inkplate.h>
#include "image.h"

Inkplate display(INKPLATE_3BIT);

static const uint8_t WAVEFORM_COUNT = 5;
static uint8_t currentWaveform = 1; // 1-indexed to match library API

static void showGradient(uint8_t waveform);
static void showTestImage(uint8_t waveform);
static int readSerialCommand();

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[WAVEFORM] Inkplate 10 waveform selection utility");

    display.begin();

    // Preview the initial waveform
    display.setWaveform(currentWaveform, false);
    showGradient(currentWaveform);

    Serial.printf("Current waveform: %d\n", currentWaveform);
    Serial.println("Send waveform [1-5], \"test\" for image, \"ok\" to burn:");

    int cmd = 0;
    while (cmd != 255)
    {
        cmd = readSerialCommand();

        if (cmd >= 1 && cmd <= WAVEFORM_COUNT)
        {
            currentWaveform = cmd;
            Serial.printf("Previewing waveform: %d\n", currentWaveform);
            display.setWaveform(currentWaveform, false);
            showGradient(currentWaveform);
            Serial.printf("Current waveform: %d\n", currentWaveform);
            Serial.println("Send waveform [1-5], \"test\" for image, \"ok\" to burn:");
        }
        else if (cmd == 254)
        {
            Serial.printf("Showing test image with waveform: %d\n", currentWaveform);
            showTestImage(currentWaveform);
            Serial.printf("Current waveform: %d\n", currentWaveform);
            Serial.println("Send waveform [1-5], \"test\" for image, \"ok\" to burn:");
        }
    }

    Serial.printf("Burning waveform: %d to EEPROM\n", currentWaveform);
    display.setWaveform(currentWaveform, true);

    display.clearDisplay();
    display.setCursor(10, 100);
    display.setTextSize(4);
    display.setTextColor(0);
    display.printf("Waveform %d programmed into ESP32 EEPROM", currentWaveform);
    display.display();

    Serial.println("Done. Please reboot and flash your normal firmware.");
}

void loop()
{
    // Empty
}

static void showGradient(uint8_t waveform)
{
    display.clearDisplay();

    int w = display.width() / 8;
    int h = display.height() - 100;

    for (int i = 0; i < 8; i++)
    {
        display.fillRect(i * w, 0, w, h, i);
    }

    display.fillRect(0, 725, 1200, 100, 7);
    display.setTextSize(4);
    display.setTextColor(0);
    display.setCursor(10, 743);
    display.printf("Waveform: %d", waveform);
    display.display();
}

static void showTestImage(uint8_t waveform)
{
    display.clearDisplay();
    display.image.drawBitmap3Bit(0, 0, demo_image, demo_image_w, demo_image_h);

    display.fillRect(0, 0, 300, 40, 7);
    display.setTextSize(4);
    display.setTextColor(0);
    display.setCursor(10, 10);
    display.printf("WAVEFORM: %d", waveform);
    display.display();
}

// Read a line from Serial. Returns:
//   1..5  waveform number selected
//   254   "test" command
//   255   "ok" command (exit loop and burn)
//   0     no command yet or invalid input
static int readSerialCommand()
{
    static char buf[16];
    static uint8_t idx = 0;

    while (Serial.available())
    {
        char c = Serial.read();
        if (c == '\n' || c == '\r')
        {
            if (idx == 0)
                continue;
            buf[idx] = '\0';
            idx = 0;

            if (strcmp(buf, "ok") == 0)
                return 255;
            if (strcmp(buf, "test") == 0)
                return 254;
            int n = atoi(buf);
            if (n >= 1 && n <= WAVEFORM_COUNT)
                return n;
            Serial.printf("Unknown command: %s\n", buf);
            return 0;
        }
        else if (idx < sizeof(buf) - 1)
        {
            buf[idx++] = c;
        }
    }
    return 0;
}
