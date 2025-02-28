
// from: https://forum.e-radionica.com/en/viewtopic.php?f=23&t=407#p810
// and example: Inkplate_Factory_Programming_VCOM

#ifndef ARDUINO_INKPLATE10
#error "Wrong board selection for this example, please select Inkplate 10 in the boards menu."
#endif

#include <Inkplate.h>
#include <EEPROM.h>

#define DEFAULT_VCOM_VOLTAGE -1.19
#define LIGHT_VCOM_VOLTAGE -1.10

Inkplate display(INKPLATE_1BIT);
double vcomVoltage;
int EEPROMaddress = 0;


void writeVCOMToEEPROM(double v);
void eraseEEPROM();
void programEEPROM();

void setup()
{
    Serial.begin(115200);
    Serial.println("Starting VCOM util");
    eraseEEPROM();
    programEEPROM();
}

void loop() {
  // Nothing
}

void eraseEEPROM() {
  Serial.println("Clearing EEPROM data...");
  EEPROM.begin(64);
  EEPROM.write(0, 0);
  EEPROM.commit();
  Serial.println("Done Clearing EEPROM!");
}

void programEEPROM()
{
    display.begin();
    EEPROM.begin(64);

    vcomVoltage = DEFAULT_VCOM_VOLTAGE;

    if (EEPROM.read(EEPROMaddress) != 170)
    {
        Serial.println("Vcom being set...");
        display.pinModeInternal(IO_INT_ADDR, display.ioRegsInt, 6, INPUT_PULLUP);
        writeVCOMToEEPROM(vcomVoltage);
        EEPROM.write(EEPROMaddress, 170);
        EEPROM.commit();
        display.selectDisplayMode(INKPLATE_1BIT);
    } else {
        Serial.println("Vcom already set!");
        vcomVoltage = (double)EEPROM.read(EEPROMaddress) / 100;

    }
    Serial.printf("vcomVoltage = %f\n", vcomVoltage);
    Serial.println("Done!");
}

uint8_t readReg(uint8_t _reg)
{
    Wire.beginTransmission(0x48);
    Wire.write(_reg);
    Wire.endTransmission(false);
    Wire.requestFrom(0x48, 1);
    return Wire.read();
}

void writeReg(uint8_t _reg, uint8_t _data)
{
    Wire.beginTransmission(0x48);
    Wire.write(_reg);
    Wire.write(_data);
    Wire.endTransmission();
}

void writeVCOMToEEPROM(double v)
{
    int vcom = int(abs(v) * 100);
    int vcomH = (vcom >> 8) & 1;
    int vcomL = vcom & 0xFF;
    // First, we have to power up TPS65186
    // Pull TPS65186 WAKEUP pin to High
    display.digitalWriteInternal(IO_INT_ADDR, display.ioRegsInt, 3, HIGH);

    // Pull TPS65186 PWR pin to High
    display.digitalWriteInternal(IO_INT_ADDR, display.ioRegsInt, 4, HIGH);
    delay(10);

    // Send to TPS65186 first 8 bits of VCOM
    writeReg(0x03, vcomL);

    // Send new value of register to TPS
    writeReg(0x04, vcomH);
    delay(1);

    // Program VCOM value to EEPROM
    writeReg(0x04, vcomH | (1 << 6));

    // Wait until EEPROM has been programmed
    delay(1);
    do
    {
        delay(1);
    } while (display.digitalReadInternal(IO_INT_ADDR, display.ioRegsInt, 6));

    // Clear Interrupt flag by reading INT1 register
    readReg(0x07);

    // Now, power off whole TPS
    // Pull TPS65186 WAKEUP pin to Low
    display.digitalWriteInternal(IO_INT_ADDR, display.ioRegsInt, 3, LOW);

    // Pull TPS65186 PWR pin to Low
    display.digitalWriteInternal(IO_INT_ADDR, display.ioRegsInt, 4, LOW);

    // Wait a little bit...
    delay(1000);

    // Power up TPS again
    display.digitalWriteInternal(IO_INT_ADDR, display.ioRegsInt, 3, HIGH);

    delay(10);

    // Read VCOM valuse from registers
    vcomL = readReg(0x03);
    vcomH = readReg(0x04);
    Serial.print("Vcom: ");
    Serial.println(vcom);
    Serial.print("Vcom register: ");
    Serial.println(vcomL | (vcomH << 8));

    if (vcom != (vcomL | (vcomH << 8)))
    {
        Serial.println("\nVCOM EEPROM PROGRAMMING FAILED!\n");
    }
    else
    {
        Serial.println("\nVCOM EEPROM PROGRAMMING OK\n");
    }
}