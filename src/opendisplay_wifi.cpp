#include "opendisplay_wifi.h"
#include "opendisplay_proto.h"
#include "homeplate.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <mdns.h>   // for mdns_service_remove(); Arduino-ESP32's ESPmDNS
                     // wraps esp-idf mdns but doesn't expose removeService

ODWiFiTransport::ODWiFiTransport(uint16_t port)
    : port_(port), server_(port) {}

ODWiFiTransport::~ODWiFiTransport() { end(); }

bool ODWiFiTransport::begin()
{
    server_.begin(port_);
    server_.setNoDelay(true);
    // mDNS responder is owned by network.cpp's mdnsStart() — we just
    // layer the _opendisplay._tcp service on top. Make sure it's running
    // (no-op if mdnsStart already ran on WiFi connect).
    mdnsStart();
    if (MDNS.addService("opendisplay", "tcp", port_)) {
        serviceAdded_ = true;
    } else {
        Serial.println("[OD] MDNS.addService(opendisplay/tcp) failed");
    }
    Serial.printf("[OD] listening on port %u, mDNS service _opendisplay._tcp\n", port_);
    return true;
}

bool ODWiFiTransport::waitForClient(uint32_t timeoutMs)
{
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        client_ = server_.accept();
        if (client_) {
            client_.setNoDelay(true);
            Serial.printf("[OD] client connected from %s\n", client_.remoteIP().toString().c_str());
            return true;
        }
        delay(50);
    }
    return false;
}

void ODWiFiTransport::setMsdTxt(const char *hex)
{
    if (!serviceAdded_ || !hex) return;
    // ESPmDNS replaces the value if the key already exists.
    MDNS.addServiceTxt("opendisplay", "tcp", "msd", hex);
}

void ODWiFiTransport::end()
{
    if (client_) {
        client_.stop();
    }
    server_.stop();
    if (serviceAdded_) {
        mdns_service_remove("_opendisplay", "_tcp");
        serviceAdded_ = false;
    }
}

bool ODWiFiTransport::connected() const
{
    // const_cast: Arduino WiFiClient::connected() isn't declared const.
    return const_cast<WiFiClient&>(client_).connected();
}

// Frame format on the wire: [len:2 LE][payload bytes]. payload begins with
// a 2-byte big-endian command code (handled by the session, not here).
int ODWiFiTransport::recvFrame(uint8_t *out, size_t outCap, uint32_t timeoutMs)
{
    if (!connected()) return -1;

    uint8_t hdr[2];
    uint32_t deadline = millis() + timeoutMs;

    // Read the 2-byte little-endian length prefix.
    size_t got = 0;
    while (got < 2) {
        if (!connected()) return -1;
        if (millis() > deadline) return 0;
        int avail = client_.available();
        if (avail <= 0) { delay(5); continue; }
        int n = client_.read(hdr + got, 2 - got);
        if (n > 0) got += (size_t)n;
    }
    uint16_t flen = od::rdU16LE(hdr);
    if (flen == 0 || flen > od::WIFI_LAN_MAX_PAYLOAD || flen > outCap) {
        Serial.printf("[OD] invalid frame length %u, dropping connection\n", flen);
        client_.stop();
        return -1;
    }

    // Read the payload — extend deadline because larger frames are slow.
    deadline = millis() + timeoutMs;
    got = 0;
    while (got < flen) {
        if (!connected()) return -1;
        if (millis() > deadline) return 0;
        int avail = client_.available();
        if (avail <= 0) { delay(2); continue; }
        int n = client_.read(out + got, flen - got);
        if (n > 0) got += (size_t)n;
    }
    return (int)flen;
}

bool ODWiFiTransport::sendFrame(const uint8_t *payload, uint16_t len)
{
    if (!connected() || len == 0) return false;
    uint8_t hdr[2];
    od::wrU16LE(hdr, len);
    if (client_.write(hdr, 2) != 2) return false;
    return client_.write(payload, len) == len;
}
