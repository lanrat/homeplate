// VCOM programming utility for Inkplate boards.
//
// Reads the desired VCOM voltage from DEFAULT_VCOM_VOLTAGE below and writes it
// to the TPS65186 PMIC's on-chip EEPROM via the library's setVCOM() helper.
// VCOM only needs to be set once per board — the value is then persistent.
//
// The Inkplate v11.0.0 library exposes setVCOM() / getStoredVCOM() directly on
// the display object, replacing the manual TPS register bit-banging this file
// used to do.

#include <Inkplate.h>

#define DEFAULT_VCOM_VOLTAGE -1.19

Inkplate display(INKPLATE_1BIT);

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[VCOM] Starting VCOM programming utility");

    display.begin();

    double existing = display.getStoredVCOM();
    Serial.printf("[VCOM] Existing stored VCOM: %.2fV\n", existing);

    Serial.printf("[VCOM] Programming VCOM to %.2fV...\n", DEFAULT_VCOM_VOLTAGE);
    if (display.setVCOM(DEFAULT_VCOM_VOLTAGE))
    {
        double readback = display.getStoredVCOM();
        Serial.printf("[VCOM] OK — VCOM now reads back as %.2fV\n", readback);
    }
    else
    {
        Serial.println("[VCOM] FAILED — setVCOM() returned false");
    }
}

void loop()
{
    // Nothing — VCOM is a one-shot operation
}
