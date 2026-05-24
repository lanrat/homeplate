// OpenDisplay WiFi/LAN transport for HomePlate.
//
// Implements od::ITransport over a single WiFiClient accepted by a
// WiFiServer listener. Adds / removes the `_opendisplay._tcp` advertisement
// on the shared mDNS responder owned by network.cpp.

#pragma once

#include "opendisplay_session.h"

#include <WiFi.h>

class ODWiFiTransport : public od::ITransport {
public:
    explicit ODWiFiTransport(uint16_t port);
    ~ODWiFiTransport();

    // Start listening + advertise mDNS service. Returns false if listener
    // couldn't bind.
    bool begin();
    // Update / add the mDNS service's "msd" TXT record (battery, temp,
    // status — see opendisplay.cpp buildOdMsdHex). hex must be NUL-
    // terminated lowercase hex of the 14-byte MSD payload.
    void setMsdTxt(const char *hex);
    // Block until a client connects or timeoutMs elapses.
    // Returns true if a client was accepted.
    bool waitForClient(uint32_t timeoutMs);
    // Stop listener + remove the OpenDisplay mDNS service. Leaves the
    // shared mDNS responder running (other services like OTA persist).
    void end();

    // od::ITransport ----------------------------------------------------
    int  recvFrame(uint8_t *out, size_t outCap, uint32_t timeoutMs) override;
    bool sendFrame(const uint8_t *payload, uint16_t len) override;
    bool connected() const override;

private:
    uint16_t port_;
    WiFiServer server_;
    WiFiClient client_;
    bool       serviceAdded_ = false;
};
