#include "homeplate.h"

#include "opendisplay.h"
#include "opendisplay_wifi.h"
#include "opendisplay_ble.h"
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
// Populate the 14-byte MSD blob (raw bytes, no encoding). Shared by WiFi
// (which then hex-encodes for mDNS TXT) and BLE (which emits the bytes as
// manufacturer-specific data in the advertisement packet).
void buildOdMsdBytes(uint8_t msd[14], uint8_t loopCounter)
{
    memset(msd, 0, 14);

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
}

// hexOut must point to a buffer of at least 29 bytes (28 hex + NUL).
static void buildOdMsdHex(char *hexOut, uint8_t loopCounter)
{
    uint8_t msd[14];
    buildOdMsdBytes(msd, loopCounter);
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

    printDramHeap("od.enter");

    // Route session allocations through PSRAM. Internal heap is ~300 KB
    // total and can't accommodate the largest panel's 4bpp framebuffer
    // (Inkplate 10: 495 KB compressed-or-raw). ps_malloc() comes from
    // Arduino-ESP32 when BOARD_HAS_PSRAM is set (see platformio.ini).
    od::setSessionAllocator(
        [](size_t n) -> void * { return ps_malloc(n); },
        [](void *p)            { free(p); });
    od::setSessionFirmwareVersion(VERSION);

    // mDNS host name is owned by network.cpp's shared responder (set to
    // plateCfg.hostname at WiFi-connect time). The transport only adds /
    // removes the _opendisplay._tcp service entry on top of it.
    ODWiFiTransport wifiTransport(plateCfg.odListenPort);
    if (!wifiTransport.begin()) {
        Serial.println("[OD] WiFi transport begin() failed");
        return false;
    }

    // Build the v1 MSD blob once. WiFi publishes it as a hex mDNS TXT
    // record; BLE emits the raw bytes inside its advertisement.
    uint8_t msd[14];
    buildOdMsdBytes(msd, (uint8_t)bootCount);
    char msdHex[29];
    static const char *hex = "0123456789abcdef";
    for (int i = 0; i < 14; i++) {
        msdHex[i * 2]     = hex[(msd[i] >> 4) & 0x0F];
        msdHex[i * 2 + 1] = hex[ msd[i]       & 0x0F];
    }
    msdHex[28] = '\0';
    wifiTransport.setMsdTxt(msdHex);
    Serial.printf("[OD] mDNS msd=%s\n", msdHex);

    // BLE transport is opt-in (default on for new firmware builds). When
    // enabled it runs concurrently with WiFi; whichever controller connects
    // first wins. ESP32 BT+WiFi coexistence costs throughput but image
    // upload is once per wake, so the duty cycle is fine.
    printDramHeap("od.wifi_up");

    ODBleTransport bleTransport(msd);
    bool bleStarted = false;
    if (plateCfg.odEnableBle) {
        // BLE+WiFi share the ESP32's tiny internal DRAM. With BLE active,
        // every mDNS UDP burst from the LAN tries to allocate an lwip
        // pbuf, fails, and the cascade of failed allocs disrupts BLE
        // scan-response broadcasts and spams the log. Stop the mDNS
        // responder for the duration of the OD activity — we restart it
        // before returning so OTA/etc. discovery still works between
        // wakes. The WiFi-side _opendisplay._tcp record is lost too, but
        // when BLE is on, BLE is the preferred discovery path anyway.
        mdnsStop();
        printDramHeap("od.mdns_down");
        bleStarted = bleTransport.begin();
        if (!bleStarted) {
            Serial.println("[OD] BLE transport begin() failed; WiFi-only this cycle");
        } else {
            printDramHeap("od.ble_up");
        }
    }

    Serial.printf("[OD] waiting up to %us for a controller (WiFi%s)...\n",
                  plateCfg.odListenSec, bleStarted ? "+BLE" : "");
    displayStatusMessage("OpenDisplay\nWaiting for controller (%us)", (unsigned)plateCfg.odListenSec);

    // Loop accepting controllers until one renders an image, or the listen
    // window expires. This matters in a busy BLE environment where random
    // neighbours' scanners can briefly connect, do a READ_CONFIG probe, and
    // disconnect — the real client needs another chance after that.
    ODInkplateRenderer renderer;
    uint32_t deadline = millis() + plateCfg.odListenSec * 1000UL;
    bool ok = false;
    unsigned sessionNum = 0;

    while ((int32_t)(millis() - deadline) < 0) {
        // Unified wait: poll both transports until one accepts a client.
        // The 100ms tick is fine — both transports finish their actual
        // handshakes in their own callbacks; this loop observes state.
        od::ITransport *active = nullptr;
        while ((int32_t)(millis() - deadline) < 0) {
            if (wifiTransport.waitForClient(100)) { active = &wifiTransport; break; }
            if (bleStarted && bleTransport.connected()) { active = &bleTransport; break; }
        }
        if (!active) break;

        sessionNum++;
        Serial.printf("[OD] session #%u starting\n", sessionNum);
        printDramHeap("od.session_start");

        // Per-frame inactivity timeout — kept short for "probe" sessions so
        // a misbehaving central doesn't burn the whole listen window.
        // Generous enough for the real image-upload flow (chunked direct-
        // write at ~230 B/chunk over BLE, plenty of headroom).
        ok = od::runSession(*active, renderer, odLog, 15000,
                            (uint32_t)plateCfg.sleepMinutes * 60);
        printDramHeap("od.session_end");
        Serial.printf("[OD] session #%u %s\n", sessionNum,
                      ok ? "rendered an image" : "ended without rendering");
        if (ok) break;
        // Loop back: keep listening for the real client. WiFi listener and
        // BLE advertising both stay up; both transports cleanly accept
        // subsequent connections.
    }

    wifiTransport.end();
    if (bleStarted) {
        bleTransport.end();
        printDramHeap("od.ble_down");
        // Restart mDNS so OTA / other components that rely on discovery
        // work between OpenDisplay activities.
        mdnsStart();
    }
    printDramHeap("od.exit");

    if (sessionNum == 0) {
        Serial.println("[OD] no controller connected within listen window");
        return false;
    }

    if (ok) {
        Serial.println("[OD] image rendered successfully");
    } else {
        Serial.println("[OD] session ended without rendering an image");
    }
    return ok;
}
