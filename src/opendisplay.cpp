#include "homeplate.h"

#include "opendisplay_wifi.h"
#include "opendisplay_panel.h"
#include "opendisplay_session.h"

#include <Arduino.h>

// HomePlate's OpenDisplay (Flex profile) activity — WiFi LAN device side.
//
// Flow per wake cycle:
//   1. Start TCP listener on plateCfg.odListenPort + advertise mDNS
//      _opendisplay._tcp.local.
//   2. Block up to plateCfg.odListenSec waiting for a controller (HA,
//      py-opendisplay host, etc.) to connect.
//   3. If a connection arrives, run the direct-write session: accept
//      0x70/0x71/0x72 commands, accumulate the image, render it on the
//      Inkplate.
//   4. Tear down and let the normal sleep path take over.
//
// Known limitations in this MVP (see opendisplay.md):
//   - WiFi only, no BLE.
//   - No encryption / authentication.
//   - Read-only config; READ_CONFIG / READ_FW_VERSION get bare ACKs.
//     Controllers that strictly require config data may not work.
//   - Monochrome bitplane rendering only; color/grayscale schemes are
//     not yet mapped to Inkplate palette.
//   - Uncompressed direct-write only; compressed transfers are rejected
//     (limits image size to what fits in a single uncompressed upload).
//   - Full refresh only; no partial updates.

static void odLog(const char *m)
{
    Serial.println(m);
}

// Build the v1-format manufacturer-specific data (MSD) blob advertised in
// the mDNS "msd" TXT record. Byte layout matches py-opendisplay's
// AdvertisementData parser:
//   [0..10]  Dynamic return data (button/touch events; zero for HomePlate
//            — Inkplate touchpads aren't exposed via OpenDisplay).
//   [11]     Temperature: (temp_c + 40) * 2  (0.5°C resolution, -40..+87.5)
//   [12]     Battery voltage low byte (10mV units; combined with status bit0)
//   [13]     Status: bit0=battery high bit, bit1=reboot flag,
//                    bit2=connection requested, bits4-7=loop counter (4-bit)
// hexOut must point to a buffer of at least 29 bytes (28 hex + NUL).
static void buildOdMsdHex(char *hexOut, uint8_t loopCounter)
{
    uint8_t msd[14] = {0};

    i2cStart();
    double voltage = display.readBattery();
#ifdef INKPLATE_HAS_TEMPERATURE
    int temp_c = display.readTemperature();
#else
    // No temperature sensor on this board (e.g. Inkplate COLOR). Leave
    // msd[11] at its zero-initialized value, which decodes to -40°C — the
    // bottom of the encoded range and an obvious "no real reading" sentinel
    // for a passive indoor display. The OpenDisplay wire format has no
    // dedicated "missing temperature" code, so this is convention.
    int temp_c = -40;
#endif
    i2cEnd();

#ifdef INKPLATE_HAS_TEMPERATURE
    // Temperature: clamp to -40..+87°C (encoded range fits in uint8).
    int t = temp_c;
    if (t < -40) t = -40;
    if (t >  87) t =  87;
    msd[11] = (uint8_t)((t + 40) * 2);
#else
    (void)temp_c; // msd[11] already 0 from memset
#endif

    // Battery in 10mV units; spans 9 bits (status bit 0 is the high bit).
    uint16_t batt_10mv = 0;
    if (voltage > 0) {
        long mv = (long)(voltage * 100.0 + 0.5); // volts -> 10mV units
        if (mv < 0) mv = 0;
        if (mv > 511) mv = 511; // 9-bit max => 5.11V, safely above LiPo range
        batt_10mv = (uint16_t)mv;
    }
    msd[12] = (uint8_t)(batt_10mv & 0xFF);

    uint8_t status = 0;
    if (batt_10mv & 0x100) status |= 0x01;          // battery high bit
    if (bootCount <= 1)    status |= 0x02;          // reboot flag
    // bit 2 (connection requested) — not applicable to us
    status |= (uint8_t)((loopCounter & 0x0F) << 4); // 4-bit loop counter
    msd[13] = status;

    static const char *hex = "0123456789abcdef";
    for (int i = 0; i < 14; i++) {
        hexOut[i * 2]     = hex[(msd[i] >> 4) & 0x0F];
        hexOut[i * 2 + 1] = hex[ msd[i]       & 0x0F];
    }
    hexOut[28] = '\0';
}

bool openDisplayActivity()
{
    if (plateCfg.odListenPort == 0) {
        Serial.println("[OD] no listen port configured");
        return false;
    }

    // Derive an mDNS service name: "od-<hostname>" so it's distinguishable
    // from HomePlate's own hostname while still being human-readable.
    char serviceName[64];
    snprintf(serviceName, sizeof(serviceName), "od-%s", plateCfg.hostname);

    ODWiFiTransport transport(plateCfg.odListenPort, serviceName);
    if (!transport.begin()) {
        Serial.println("[OD] transport begin() failed");
        return false;
    }

    // Publish the v1 MSD blob via mDNS TXT so controllers can read
    // battery / temperature / status without opening a TCP connection.
    char msdHex[29];
    buildOdMsdHex(msdHex, (uint8_t)bootCount);
    transport.setMsdTxt(msdHex);
    Serial.printf("[OD] mDNS msd=%s\n", msdHex);

    Serial.printf("[OD] waiting up to %us for a controller...\n", plateCfg.odListenSec);
    displayStatusMessage("OpenDisplay\nWaiting for controller (%us)", (unsigned)plateCfg.odListenSec);

    if (!transport.waitForClient(plateCfg.odListenSec * 1000UL)) {
        Serial.println("[OD] no controller connected within listen window");
        transport.end();
        return false;
    }

    ODInkplateRenderer renderer;
    // Per-frame inactivity timeout. 15s is generous for a slow LAN +
    // chunked transfers up to ~125 KB.
    bool ok = od::runSession(transport, renderer, odLog, 15000);
    transport.end();

    if (ok) {
        Serial.println("[OD] image rendered successfully");
    } else {
        Serial.println("[OD] session ended without rendering an image");
    }
    return ok;
}
