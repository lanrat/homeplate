// OpenDisplay WiFi/LAN transport for HomePlate.
//
// Implements od::ITransport over a single WiFiClient accepted by a
// WiFiServer listener. Also handles mDNS service advertisement.

#pragma once

#include "opendisplay_session.h"

#include <WiFi.h>

class ODWiFiTransport : public od::ITransport {
public:
    ODWiFiTransport(uint16_t port, const char *serviceName);
    ~ODWiFiTransport();

    // Start listening + advertise mDNS. Returns false if listener
    // couldn't bind.
    bool begin();
    // Block until a client connects or timeoutMs elapses.
    // Returns true if a client was accepted.
    bool waitForClient(uint32_t timeoutMs);
    // Stop listener + unadvertise mDNS.
    void end();

    // od::ITransport ----------------------------------------------------
    int  recvFrame(uint8_t *out, size_t outCap, uint32_t timeoutMs) override;
    bool sendFrame(const uint8_t *payload, uint16_t len) override;
    bool connected() const override;

private:
    uint16_t port_;
    const char *serviceName_; // e.g. "od-XXXXXX"
    WiFiServer server_;
    WiFiClient client_;
    bool       mdnsStarted_ = false;
};
