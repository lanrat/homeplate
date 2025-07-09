/*
   Inkplate10_Wavefrom_EEPROM_Programming example for e-radionica.com Inkplate 10
   For this example you will need only USB cable and Inkplate 10.
   Select "e-radionica Inkplate10" or "Soldered Inkplate10" from Tools -> Board menu.
   Don't have "e-radionica Inkplate10" or "Soldered Inkplate10" option? Follow our tutorial and add it:
   https://soldered.com/learn/add-inkplate-6-board-definition-to-arduino-ide/

   This example will program a new waveform in ESP32 EEPROM if the waveform has been accidentally overwritten.
   Upload code and open Serial Monitor. Send numbers form 1 to 4 to display selected waveform.
   They should go from dark to light. Select the one that looks the best to you.
   Send "ok" to select waveform and burn it into ESP32 EEPROM or send "test" to display a an test image.
   CAUTION! Changing EEPROM size can wipe waveform data.
   Waveform data is stored in EEPROM on address range form address 0 to address 75.

   Want to learn more about Inkplate? Visit www.inkplate.io
   Looking to get support? Write on our forums: https://forum.soldered.com/
   18 January 2023 by Soldered
*/

// Next 3 lines are a precaution, you can ignore those, and the example would also work without them
#if !defined(ARDUINO_INKPLATE10) && !defined(ARDUINO_INKPLATE10V2)
#error                                                                                                                 \
    "Wrong board selection for this example, please select e-radionica Inkplate10 or Soldered Inkplate10 in the boards menu."
#endif

#include "EEPROM.h"   // Include ESP32 EEPROM library
#include "Inkplate.h" // Include Inkplate library to the sketch
#include "image.h"    // Include the demo image for test

// added definitions to compile with platformio
void showGradient(int _selected);
int getWaveformNumer();


Inkplate display(INKPLATE_3BIT); // Create object on Inkplate library and set library to work in grayscale mode

// All waveforms for Inkplate 10 board
uint8_t waveform1[8][9] = {{0, 0, 0, 0, 0, 0, 0, 1, 0}, {0, 0, 0, 2, 2, 2, 1, 1, 0}, {0, 0, 2, 1, 1, 2, 2, 1, 0},
                           {0, 1, 2, 2, 1, 2, 2, 1, 0}, {0, 0, 2, 1, 2, 2, 2, 1, 0}, {0, 2, 2, 2, 2, 2, 2, 1, 0},
                           {0, 0, 0, 0, 0, 2, 1, 2, 0}, {0, 0, 0, 2, 2, 2, 2, 2, 0}};
uint8_t waveform2[8][9] = {{0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 2, 1, 2, 1, 1, 0}, {0, 0, 0, 2, 2, 1, 2, 1, 0},
                           {0, 0, 2, 2, 1, 2, 2, 1, 0}, {0, 0, 0, 2, 1, 1, 1, 2, 0}, {0, 0, 2, 2, 2, 1, 1, 2, 0},
                           {0, 0, 0, 0, 0, 1, 2, 2, 0}, {0, 0, 0, 0, 2, 2, 2, 2, 0}};
uint8_t waveform3[8][9] = {{0, 3, 3, 3, 3, 3, 3, 3, 0}, {0, 1, 2, 1, 1, 2, 2, 1, 0}, {0, 2, 2, 2, 1, 2, 2, 1, 0},
                           {0, 0, 2, 2, 2, 2, 2, 1, 0}, {0, 3, 3, 2, 1, 1, 1, 2, 0}, {0, 3, 3, 2, 2, 1, 1, 2, 0},
                           {0, 2, 1, 2, 1, 2, 1, 2, 0}, {0, 3, 3, 3, 2, 2, 2, 2, 0}};
uint8_t waveform4[8][9] = {{0, 0, 0, 0, 0, 0, 0, 1, 0}, {0, 0, 0, 2, 2, 2, 1, 1, 0}, {0, 0, 2, 1, 1, 2, 2, 1, 0},
                           {1, 1, 2, 2, 1, 2, 2, 1, 0}, {0, 0, 2, 1, 2, 2, 2, 1, 0}, {0, 1, 2, 2, 2, 2, 2, 1, 0},
                           {0, 0, 0, 2, 2, 2, 1, 2, 0}, {0, 0, 0, 2, 2, 2, 2, 2, 0}};
uint8_t waveform5[8][9] = {{0, 0, 0, 0, 0, 0, 0, 1, 0}, {0, 0, 0, 2, 2, 2, 1, 1, 0}, {2, 2, 2, 1, 0, 2, 1, 0, 0},
                           {2, 1, 1, 2, 1, 1, 1, 2, 0}, {2, 2, 2, 1, 1, 1, 0, 2, 0}, {2, 2, 2, 1, 1, 2, 1, 2, 0},
                           {0, 0, 0, 0, 2, 1, 2, 2, 0}, {0, 0, 0, 0, 2, 2, 2, 2, 0}};
uint8_t *waveformList[] = {&waveform1[0][0], &waveform2[0][0], &waveform3[0][0], &waveform4[0][0], &waveform5[0][0]};

// Calculate number of possible waveforms
uint8_t waveformListSize = (sizeof(waveformList) / sizeof(uint8_t *));

// Struct for reading waveform from EEPROM memory of ESP32
struct waveformData waveformEEPROM;

int currentWaveform = 0;

void setup()
{
    // Start up a Serial monitor @ 115200 bauds.
    Serial.begin(115200);

    display.begin();   // Init library (you should call this function ONLY ONCE)
    EEPROM.begin(512); // Init EEPROM library with 512 of EEPROM size.

    // Display all shades of gray on e-paper with first waveform
    showGradient(currentWaveform);

    // Flag for programming
    int progFlag = 0;

    // Flag for data receive
    int uartFlag = 0;

    Serial.printf("Current waveform: %d\n", currentWaveform+1);
    Serial.printf("Send wavefrom [1-5], \"ok\" to burn it, \"test\" for image: ");

    // Until "ok" is not send, user can select one of the waveforms
    while (uartFlag != 255)
    {
        // Check if user send waveform number (or request for waveform programming).
        uartFlag = getWaveformNumer();

        // Got waveform number? Show it!
        if (uartFlag > 0 && uartFlag <= waveformListSize)
        {
            currentWaveform = uartFlag - 1;
            Serial.printf("Showing Gradient using waveform: %d\n", currentWaveform+1);
            showGradient(currentWaveform);

            Serial.printf("Current waveform: %d\n", currentWaveform+1);
            Serial.printf("Send wavefrom [1-5], \"ok\" to burn it, \"test\" for image: ");
        }

        // If function returns 254, show test image
        if (uartFlag == 254)
        {
            Serial.printf("Showing Test Image using waveform: %d\n", currentWaveform+1);
            display.clearDisplay();
            display.drawBitmap3Bit(0, 0, demo_image, demo_image_w, demo_image_h);

            display.fillRect(0, 0, 300, 40, 7);
            display.setTextSize(4);
            display.setTextColor(0);
            display.setCursor(10, 10);
            display.printf("WAVEFORM: %d", currentWaveform + 1);

            display.display();

            Serial.printf("Current waveform: %d\n", currentWaveform+1);
            Serial.printf("Send wavefrom [1-5], \"ok\" to burn it, \"test\" for image: ");
        }
    }

    Serial.printf("Burning waveform: %d\n", currentWaveform+1);

    // Load waveform in EEPROM memory of ESP32
    waveformEEPROM.waveformId = INKPLATE10_WAVEFORM1 + currentWaveform;
    memcpy(&waveformEEPROM.waveform, waveformList[currentWaveform], sizeof(waveformEEPROM.waveform));
    waveformEEPROM.checksum = display.calculateChecksum(waveformEEPROM);
    display.burnWaveformToEEPROM(waveformEEPROM);

    // Show message on the display
    display.clearDisplay();
    display.setCursor(10, 100);
    display.print("Waveform");
    display.print(currentWaveform + 1, DEC);
    display.print(" selected & programmed into ESP32 EEPROM");
    display.display();

    Serial.printf("Done, please reboot & program Inkplate\n");
}

void loop()
{
    // Empty...
}

// Prints gradient lines and currently selected waveform
void showGradient(int _selected)
{
    display.clearDisplay();

    int w = display.width() / 8;
    int h = display.height() - 100;

    display.changeWaveform(waveformList[currentWaveform]);

    display.fillRect(0, 725, 1200, 100, 7);

    display.setTextSize(4);
    display.setTextColor(0);
    display.setCursor(10, 743);
    display.print("Waveform select: ");

    display.setCursor(432, 743);
    // Display waveform numbers.
    for (int i = 1; i <= waveformListSize; i++)
    {
        display.print(i, DEC);
        display.print(' ');
    }
    display.drawRect((_selected * 6 * 4 * 2) + 432 - 3, 740, (6 * 4) + 2, (8 * 4) + 2, 0);

    for (int i = 0; i < 8; i++)
    {
        display.fillRect(i * w, 0, w, h, i);
    }

    display.setTextSize(3);
    display.setCursor(10, 792);
    display.print("Send wavefrom number, or send \"ok\" to burn it, \"test\" for image");

    display.display();
}

/**
 * @brief       Get the command from the serial.
 *
 * @return      Returns 0 for no captured data on serial, 255 for wavefrom programming, 254 for test image show.
 */
int getWaveformNumer()
{
    // Buffer for the serial data.
    char _buffer[20];

    // Variable for array indexing (serial buffer).
    int _n = 0;

    // Number for storing current waveform.
    int _waveform = 0;

    // Check if something has been sent to the Inkplate.
    if (Serial.available())
    {
        // Capture the time!
        unsigned long _timeout = millis();

        // Wait until 250 milliseconds have passed from last received char from UART.
        while ((unsigned long)(millis() - _timeout) < 250)
        {
            // Got some space in the buffer? Get the chars!
            if (Serial.available() && _n < 19)
            {
                _buffer[_n++] = Serial.read();
                Serial.write(_buffer[_n-1]); // echo back response
            }
            else
            {
                // Drop the data from the UART (buffer is full).
                Serial.read();
            }
        }

        // Add null-terminating character at the end.
        _buffer[_n] = '\0';
    }

    // Convert whole string to the lowercase
    for (int i = 0; _buffer[i] != '\0'; i++)
    {
        _buffer[i] = tolower(_buffer[i]);
    }

    // Check if you got something.
    if (_n != 0)
    {
        // print newline
        Serial.printf("\n");
        // Try to parse it.

        // First check for the keyword for programming or show test image.
        if (strstr("ok\r\n", _buffer) != NULL)
        {
            return 255;
        }
        else if (strstr("test\r\n", _buffer))
        {
            return 254;
        }
        else if (sscanf(_buffer, "%d", &_waveform))
        {
            // Check bounds.
            if ((_waveform > 0) && _waveform <= (waveformListSize))
            {
                return _waveform;
            }
        }
    }
    return 0;
}
