// OpenDisplay BLE GATT transport for HomePlate.
//
// Implements od::ITransport on top of a single-characteristic GATT service
// (UUID 0x2446). Each ATT write from the central is delivered to the session
// as one logical frame; sendFrame() writes the response to the notify
// characteristic. Compare with src/opendisplay_wifi.cpp for the TCP analogue.
//
// Uses the framework's bundled <BLEDevice.h> (Bluedroid). On classic ESP32
// the stock pioarduino libbt.a overflows IRAM; HomePlate ships a slimmed
// rebuild — see vendor/esp32-bt-slim/README.md.

#pragma once

#include "opendisplay_session.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class BLEServer;
class BLECharacteristic;

class ODBleTransport : public od::ITransport {
public:
    // msd points to the 14-byte OpenDisplay v1 MSD blob built via
    // buildOdMsdBytes(); copied internally, so the caller's buffer can be
    // stack-allocated.
    explicit ODBleTransport(const uint8_t msd[14]);
    ~ODBleTransport();

    // Bring up the BLE stack, register the GATT service + characteristic,
    // and start advertising. Returns false on init failure.
    bool begin();
    // Stop advertising + tear down GATT. Safe to call repeatedly.
    void end();
    // Block up to timeoutMs for a central to connect. Returns true on
    // connect, false on timeout. Mirrors ODWiFiTransport::waitForClient().
    bool waitForClient(uint32_t timeoutMs);

    // od::ITransport ----------------------------------------------------
    int  recvFrame(uint8_t *out, size_t outCap, uint32_t timeoutMs) override;
    bool sendFrame(const uint8_t *payload, uint16_t len) override;
    // Overridden to apply BLE's chunked-response framing: payload is
    // [echo:2 BE][tlv...], emitted as one or more notifications carrying
    // [echo:2][chunk_idx:2 LE][total_len:2 LE on first chunk][tlv slice].
    // See py-opendisplay's device.py interrogate() for the wire format.
    bool sendChunked(const uint8_t *payload, uint32_t len) override;
    bool connected() const override;

    // ISR-callable: called by the GATT characteristic write callback to
    // hand a full ATT-write payload into the inbound frame queue. Public
    // because BLE library callbacks are free functions / friends.
    void enqueueRxFrame(const uint8_t *data, size_t len);
    // Called by server callbacks when central state changes.
    void onCentralConnect();
    void onCentralDisconnect();
    void onMtuUpdated(uint16_t mtu);

private:
    uint8_t msd_[14];
    BLEServer *server_       = nullptr;
    BLECharacteristic *chr_  = nullptr;
    QueueHandle_t rxQueue_   = nullptr;
    volatile bool connected_ = false;
    volatile bool started_   = false;
    volatile uint16_t mtu_   = 23; // default ATT MTU until negotiation
};
