#include "homeplate.h"

#if (defined(ARDUINO_INKPLATE10V2) || defined(ARDUINO_INKPLATE6V2) || defined(ARDUINO_INKPLATE6PLUS) || defined(ARDUINO_INKPLATE6PLUSV2) || defined(ARDUINO_INKPLATE6FLICK)) && TOUCHPAD_ENABLE
#error "TOUCHPAD_ENABLE is not supported on this board"
#endif

#define INPUT_TASK_PRIORITY 10

// only supported on boards with touchpads (Inkplate 10 v1, Inkplate 6 v1)
#ifdef HAS_TOUCHPADS

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
// pad is the touchpad index 1, 2 or 3
bool checkPad(uint8_t pad)
{
    if (display.touchpad.read(pad))
    {
        vTaskDelay(250 / portTICK_PERIOD_MS);
        return display.touchpad.read(pad);
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
            if (checkPad(1))
            {
                Serial.printf("[INPUT] touchpad 1\n");
                startActivity(activityFromString(plateCfg.defaultActivityStr));
                button = true;
            }
            else if (checkPad(2))
            {
                Serial.printf("[INPUT] touchpad 2\n");
                startActivity(GuestWifi);
                button = true;
            }
            else if (checkPad(3))
            {
                Serial.printf("[INPUT] touchpad 3\n");
                startActivity(Info);
                button = true;
            }
            #ifdef WAKE_BUTTON
            else if (!digitalRead(WAKE_BUTTON))
            {
                Serial.printf("[INPUT] wake button\n");
                startActivity(activityFromString(plateCfg.defaultActivityStr));
                button = true;
            }
            #endif
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

#endif // HAS_TOUCHPADS

void startMonitoringButtonsTask()
{
    #ifdef HAS_TOUCHPADS
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
    #ifdef HAS_TOUCHPADS
        // Read the INTF and INTCAP values that the MCP latched at the moment
        // of the wake-triggering interrupt.
        uint16_t intf   = display.expander1.getInterruptFlagsAtBegin();
        uint16_t intcap = display.expander1.getInterruptCaptureAtBegin();
        // Port B is in the high byte; the touchpad pins (10/11/12) live there.
        uint8_t  intfB   = (intf >> 8) & 0xFF;
        uint8_t  intcapB = (intcap >> 8) & 0xFF;
        // Rising-edge filter: only treat a pad as "pressed" if it was HIGH at
        // the instant of the interrupt. This rejects falling edges (release)
        // and noise that already returned to LOW.
        uint8_t rising = intfB & intcapB;

        Serial.printf("[INPUT] boot wake: INTFB=%#x INTCAPB=%#x rising=%#x\n",
                      intfB, intcapB, rising);

        if (rising & INT_PAD1)
        {
            Serial.println("[INPUT] boot: PAD1");
            startActivity(activityFromString(plateCfg.defaultActivityStr));
        }
        else if (rising & INT_PAD2)
        {
            Serial.println("[INPUT] boot: PAD2");
            startActivity(GuestWifi);
        }
        else if (rising & INT_PAD3)
        {
            Serial.println("[INPUT] boot: PAD3");
            startActivity(Info);
        }
        else if (intfB)
        {
            Serial.println("[INPUT] boot: spurious wake (release / noise), ignoring");
        }
    #endif
}

void setupWakePins()
{
    #if TOUCHPAD_ENABLE && defined(HAS_TOUCHPADS)
        // Configure each touchpad pin to wake on rising edge only (press,
        // not release / capacitive noise).
        display.expander1.setIntPin(PAD1, RISING);
        display.expander1.setIntPin(PAD2, RISING);
        display.expander1.setIntPin(PAD3, RISING);
    #endif
    #ifdef WAKE_BUTTON
        pinMode(WAKE_BUTTON, WAKE_BUTTON_MODE);
    #endif
}