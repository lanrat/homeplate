#include "homeplate.h"

#if defined(ARDUINO_INKPLATE10V2) && TOUCHPAD_ENABLE
#error "TOUCHPAD_ENABLE is not supported with ARDUINO_INKPLATE10V2"
#endif

#define INPUT_TASK_PRIORITY 10

// only supported on the Inkplate10 (not v2)
#if defined(ARDUINO_INKPLATE10)

#define INT_PAD1 (1 << (PAD1 - 8)) // 0x04
#define INT_PAD2 (1 << (PAD2 - 8)) // 0x08
#define INT_PAD3 (1 << (PAD3 - 8)) // 0x10

// read a byte from the expander
unsigned int readMCPRegister(const byte reg)
{
    Wire.beginTransmission(IO_INT_ADDR);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom(IO_INT_ADDR, 1);
    return Wire.read();
}

// assume i2clock is already taken
// debounce touches that are too quick
bool checkPad(uint8_t pad)
{
    if (display.readTouchpad(pad))
    {
        vTaskDelay(250 / portTICK_PERIOD_MS);
        return display.readTouchpad(pad);
    }
    return false;
}

// TODO use a ring buffer for last 5 touchpad events, and if < some threshold, reboot....

void checkButtons(void *params)
{
    static int lastDebounceTime = 0;
    bool button = false;
    vTaskDelay(3 * SECOND / portTICK_PERIOD_MS); // wait for touchpad calibration to finish before checking
    Serial.printf("[INPUT] Starting input monitoring...\n");
    while (true)
    {
        // debounce
        if ((millis() - lastDebounceTime) < DEBOUNCE_DELAY_MS)
        {
            vTaskDelay(250 / portTICK_PERIOD_MS);
            continue;
        }
        printDebug("[INPUT] checking for buttons...");
        i2cStart();
        // check buttons
        if (TOUCHPAD_ENABLE)
        {
            if (checkPad(PAD1))
            {
                Serial.printf("[INPUT] touchpad 1\n");
                startActivity(DEFAULT_ACTIVITY);
                button = true;
            }
            else if (checkPad(PAD2))
            {
                Serial.printf("[INPUT] touchpad 2\n");
                startActivity(GuestWifi);
                button = true;
            }
            else if (checkPad(PAD3))
            {
                Serial.printf("[INPUT] touchpad 3\n");
                startActivity(Info);
                button = true;
            }
            else if (!digitalRead(WAKE_BUTTON))
            {
                Serial.printf("[INPUT] wake button\n");
                startActivity(DEFAULT_ACTIVITY);
                button = true;
            }
        }

        if (button)
        {
            // clear the interrupt on the MCP
            readMCPRegister(MCP23017_INTCAPB);
            lastDebounceTime = millis();
            button = false;
            delaySleep(10);
            printDebugStackSpace();
        }
        i2cEnd();
        vTaskDelay(400 / portTICK_PERIOD_MS);
    }
}

#endif // defined(ARDUINO_INKPLATE10)

void startMonitoringButtonsTask()
{
    #if defined(ARDUINO_INKPLATE10)
        // inkplate code needs to be on arduino core or may get i2c errors
        // use mutex for all inkplate code
        xTaskCreatePinnedToCore(
            checkButtons,        /* Task function. */
            "INPUT_BUTTON_TASK", /* String with name of task. */
            4096,                /* Stack size */
            NULL,                /* Parameter passed as input of the task */
            INPUT_TASK_PRIORITY, /* Priority of the task. */
            NULL,
            CONFIG_ARDUINO_RUNNING_CORE);
    #else
        Serial.println("[INPUT] Input monitoring not supported by this platform");
    #endif
}

void checkBootPads()
{
    #if defined(ARDUINO_INKPLATE10)
        unsigned int key = 0;
        i2cStart();
        key = readMCPRegister(MCP23017_INTFB);
        i2cEnd();
        if (key) // which pin caused interrupt
        {
            // Serial.printf("INTFB: %#x\n", keyInt);
            //  value of pin at time of interrupt
            if (key & INT_PAD1)
            {
                Serial.println("[INPUT] boot: PAD1");
                startActivity(DEFAULT_ACTIVITY);
            }
            else if (key & INT_PAD2)
            {
                Serial.println("[INPUT] boot: PAD2");
                startActivity(GuestWifi);
            }
            else if (key & INT_PAD3)
            {
                Serial.println("[INPUT] boot: PAD3");
                startActivity(Info);
            }
            Serial.println();

            i2cStart();
            key = readMCPRegister(MCP23017_INTCAPB); // this clears the interrupt
            i2cEnd();
            // Serial.printf("INTCAP: %#x\n", keyValue);
            //  if (keyValue & INT_PAD1)
            //  {
            //      Serial.print("PAD1 ");
            //  }
            //  if (keyValue & INT_PAD2)
            //  {
            //      Serial.print("PAD2 ");
            //  }
            //  if (keyValue & INT_PAD3)
            //  {
            //      Serial.print("PAD3 ");
            //  }
            //  Serial.println();
        }
    #endif
}

void setupWakePins()
{
    #if defined(ARDUINO_INKPLATE10)
        // set which pads can allow wakeup
        display.setIntPin(PAD1, RISING, IO_INT_ADDR);
        display.setIntPin(PAD2, RISING, IO_INT_ADDR);
        display.setIntPin(PAD3, RISING, IO_INT_ADDR);
    #endif
    pinMode(WAKE_BUTTON, INPUT_PULLUP);

}