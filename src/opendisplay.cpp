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
