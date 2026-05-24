#include "opendisplay_ble.h"
#include "opendisplay_proto.h"

#include "homeplate.h" // for printDramHeap

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <WiFi.h> // for WiFi.macAddress() to build the advertised name

// OpenDisplay GATT service + characteristic UUID — both use the 16-bit
// alias 0x2446 (full 128-bit: 00002446-0000-1000-8000-00805F9B34FB). See
// opendisplay-ble-handoff.md "BLE protocol facts".
static const char *OD_SERVICE_UUID = "00002446-0000-1000-8000-00805F9B34FB";
static const char *OD_CHAR_UUID    = "00002446-0000-1000-8000-00805F9B34FB";

// 14-byte v1 MSD blob (same format as the WiFi mDNS TXT) emitted in the
// advertisement's manufacturer-specific-data field. Prefixed with the
// company ID 0x2446 in little-endian, per BLE manufacturer-data layout.
static constexpr uint16_t OD_MANUFACTURER_ID = 0x2446;

// Cap on a single inbound ATT-write — sized so a max-MTU (517) write fits
// comfortably. The BLE library lives well below this in practice.
static constexpr size_t MAX_RX_FRAME = 600;

// Pending inbound frames; one entry = one ATT write = one logical
// od::ITransport frame. 8 deep is more than enough to absorb back-to-back
// chunked direct-write commands while the session drains.
static constexpr size_t RX_QUEUE_DEPTH = 8;

namespace {

// Heap-allocated frame stored on the queue. Allocated in the write
// callback, freed by recvFrame() after copying out.
struct RxFrame {
    uint16_t len;
    uint8_t  data[];
};

class ServerCb : public BLEServerCallbacks {
public:
    explicit ServerCb(ODBleTransport *t) : t_(t) {}
    void onConnect(BLEServer *) override { t_->onCentralConnect(); }
    void onDisconnect(BLEServer *s) override {
        t_->onCentralDisconnect();
        // Re-advertise so the next central can connect within the same
        // listen window without re-running begin(). Bluedroid sometimes
        // gets sticky after a disconnect — stop+start is more reliable
        // than start alone.
        BLEDevice::startAdvertising();
        Serial.println("[OD-BLE] re-advertising after disconnect");
        (void)s;
    }
    void onMtuChanged(BLEServer *, esp_ble_gatts_cb_param_t *param) override {
        t_->onMtuUpdated(param->mtu.mtu);
    }
private:
    ODBleTransport *t_;
};

class CharCb : public BLECharacteristicCallbacks {
public:
    explicit CharCb(ODBleTransport *t) : t_(t) {}
    void onWrite(BLECharacteristic *c) override {
        String v = c->getValue();
        t_->enqueueRxFrame((const uint8_t *)v.c_str(), v.length());
    }
private:
    ODBleTransport *t_;
};

} // namespace

ODBleTransport::ODBleTransport(const uint8_t msd[14])
{
    memcpy(msd_, msd, 14);
}

ODBleTransport::~ODBleTransport() { end(); }

bool ODBleTransport::begin()
{
    if (started_) return true;

    rxQueue_ = xQueueCreate(RX_QUEUE_DEPTH, sizeof(RxFrame *));
    if (!rxQueue_) {
        Serial.println("[OD-BLE] failed to create rx queue");
        return false;
    }

    // Advertised name = "OD<last 3 MAC bytes>", matching what every other
    // OpenDisplay firmware uses so controllers' scanners recognise us.
    // Short enough (8 chars) to fit alongside the MSD field in a single
    // 31-byte primary adv packet — longer names would overflow and need
    // scan-response handling.
    char devName[16];
    String mac = WiFi.macAddress(); // "AA:BB:CC:DD:EE:FF"
    snprintf(devName, sizeof(devName), "OD%c%c%c%c%c%c",
             mac[9], mac[10], mac[12], mac[13], mac[15], mac[16]);

    BLEDevice::init(devName);
    // Request a generous MTU; the central drives final negotiation but the
    // local cap matters for our READ_CONFIG response (~135 bytes) which
    // would not fit in a single default-MTU (23 → 20-byte payload)
    // notification. Most modern centrals negotiate to 247+.
    BLEDevice::setMTU(517);

    server_ = BLEDevice::createServer();
    server_->setCallbacks(new ServerCb(this));

    BLEService *svc = server_->createService(OD_SERVICE_UUID);
    chr_ = svc->createCharacteristic(
        OD_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
    // CCCD descriptor is required for centrals to enable notifications.
    chr_->addDescriptor(new BLE2902());
    chr_->setCallbacks(new CharCb(this));
    svc->start();

    // Build advertisement: name + manufacturer-specific data carrying the
    // 14-byte MSD blob. Manufacturer data on BLE is prefixed by the 2-byte
    // company ID in little-endian, then the vendor payload.
    BLEAdvertising *adv = BLEDevice::getAdvertising();
    BLEAdvertisementData advData;
    advData.setName(devName);
    {
        uint8_t buf[2 + sizeof(msd_)];
        buf[0] = (uint8_t)(OD_MANUFACTURER_ID & 0xFF);
        buf[1] = (uint8_t)(OD_MANUFACTURER_ID >> 8);
        memcpy(buf + 2, msd_, sizeof(msd_));
        advData.setManufacturerData(String((const char *)buf, sizeof(buf)));
    }
    adv->setAdvertisementData(advData);
    adv->addServiceUUID(OD_SERVICE_UUID);
    BLEDevice::startAdvertising();

    started_ = true;
    Serial.printf("[OD-BLE] advertising as %s (service 0x2446)\n", devName);
    return true;
}

void ODBleTransport::end()
{
    if (!started_) return;
    BLEDevice::getAdvertising()->stop();
    // Drain any pending frames so we don't leak.
    if (rxQueue_) {
        RxFrame *f = nullptr;
        while (xQueueReceive(rxQueue_, &f, 0) == pdTRUE) {
            free(f);
        }
        vQueueDelete(rxQueue_);
        rxQueue_ = nullptr;
    }
    // Tear down the BLE stack so the next wake (or WiFi-only mode) doesn't
    // pay the radio cost. deinit(true) releases the controller memory.
    BLEDevice::deinit(true);
    server_    = nullptr;
    chr_       = nullptr;
    started_   = false;
    connected_ = false;
}

bool ODBleTransport::waitForClient(uint32_t timeoutMs)
{
    uint32_t deadline = millis() + timeoutMs;
    while (!connected_) {
        if ((int32_t)(millis() - deadline) >= 0) return false;
        delay(50);
    }
    return true;
}

void ODBleTransport::onCentralConnect()
{
    // Drain any frames left over from a previous central that disconnected
    // mid-session. Without this, a stale 0x71 chunk could poison the new
    // central's session.
    if (rxQueue_) {
        RxFrame *f = nullptr;
        unsigned drained = 0;
        while (xQueueReceive(rxQueue_, &f, 0) == pdTRUE) {
            free(f);
            drained++;
        }
        if (drained) {
            Serial.printf("[OD-BLE] drained %u stale rx frame(s)\n", drained);
        }
    }
    connected_ = true;
    Serial.println("[OD-BLE] central connected");
    printDramHeap("ble.connect");
}

void ODBleTransport::onCentralDisconnect()
{
    connected_ = false;
    mtu_ = 23;
    Serial.println("[OD-BLE] central disconnected");
    printDramHeap("ble.disconnect");
}

void ODBleTransport::onMtuUpdated(uint16_t mtu)
{
    mtu_ = mtu;
    Serial.printf("[OD-BLE] MTU negotiated: %u\n", (unsigned)mtu);
}

void ODBleTransport::enqueueRxFrame(const uint8_t *data, size_t len)
{
    if (!rxQueue_ || len == 0 || len > MAX_RX_FRAME) return;
    RxFrame *f = (RxFrame *)malloc(sizeof(RxFrame) + len);
    if (!f) {
        Serial.println("[OD-BLE] rx alloc failed, dropping frame");
        return;
    }
    f->len = (uint16_t)len;
    memcpy(f->data, data, len);
    if (xQueueSend(rxQueue_, &f, 0) != pdTRUE) {
        Serial.println("[OD-BLE] rx queue full, dropping frame");
        free(f);
    }
}

int ODBleTransport::recvFrame(uint8_t *out, size_t outCap, uint32_t timeoutMs)
{
    if (!rxQueue_) return -1;
    RxFrame *f = nullptr;
    TickType_t ticks = (timeoutMs == 0) ? 0 : pdMS_TO_TICKS(timeoutMs);
    if (xQueueReceive(rxQueue_, &f, ticks) != pdTRUE) {
        return connected_ ? 0 : -1;
    }
    uint16_t n = f->len;
    if (n > outCap) {
        Serial.printf("[OD-BLE] rx frame %u > outCap %zu, dropping\n",
                      (unsigned)n, outCap);
        free(f);
        return -1;
    }
    memcpy(out, f->data, n);
    free(f);
    return (int)n;
}

bool ODBleTransport::sendFrame(const uint8_t *payload, uint16_t len)
{
    if (!connected_ || !chr_ || len == 0) return false;
    // BLE notifications cap at MTU-3 bytes. The only OD response that may
    // bump up against this is the ~135-byte READ_CONFIG TLV blob, which
    // fits in the default negotiated MTU of any modern central (247+).
    // If it doesn't, log so we know to add fragmentation.
    uint16_t maxNotify = (mtu_ > 3) ? (uint16_t)(mtu_ - 3) : 20;
    if (len > maxNotify) {
        Serial.printf("[OD-BLE][WARN] frame %u > MTU payload %u; "
                      "central may see truncated response\n",
                      (unsigned)len, (unsigned)maxNotify);
    }
    chr_->setValue((uint8_t *)payload, len);
    chr_->notify();
    return true;
}

bool ODBleTransport::sendChunked(const uint8_t *payload, uint32_t len)
{
    // Layout we receive (built by buildReadConfigResponse): [echo:2 BE][tlv:N].
    // Layout we emit per BLE notification, per py-opendisplay device.py:
    //   First chunk:      [echo:2 BE][chunk_idx=0:2 LE][total_len:2 LE][tlv slice]
    //   Subsequent chunks:[echo:2 BE][chunk_idx:2 LE][tlv slice]
    // total_len is the total TLV size (not including any of these headers).
    if (!connected_ || !chr_ || len < 2) return false;

    const uint8_t *echo = payload;
    const uint8_t *tlv  = payload + 2;
    uint32_t tlvLen     = len - 2;

    // Per-notification cap: MTU - 3 (ATT header) - 2 (our echo) - 2 (chunk_idx)
    // -2 (total_len, first chunk only). We size for the first-chunk worst
    // case and live with one extra byte of headroom on subsequent chunks.
    uint16_t notifyCap = (mtu_ > 9) ? (uint16_t)(mtu_ - 3) : 20;
    uint16_t firstSliceMax  = (notifyCap >= 6) ? (uint16_t)(notifyCap - 6) : 0;
    uint16_t laterSliceMax  = (notifyCap >= 4) ? (uint16_t)(notifyCap - 4) : 0;
    if (firstSliceMax == 0 || laterSliceMax == 0) {
        Serial.println("[OD-BLE][WARN] MTU too small for chunked response");
        return false;
    }

    uint32_t offset = 0;
    uint16_t chunkIdx = 0;
    // Outbound buffer sized for one full chunk + headers.
    uint8_t buf[517];

    while (offset < tlvLen) {
        bool first = (offset == 0);
        uint16_t maxSlice = first ? firstSliceMax : laterSliceMax;
        uint32_t remaining = tlvLen - offset;
        uint16_t slice = (remaining > maxSlice) ? maxSlice : (uint16_t)remaining;

        uint8_t *p = buf;
        // [echo:2 BE]
        *p++ = echo[0]; *p++ = echo[1];
        // [chunk_idx:2 LE]
        *p++ = (uint8_t)(chunkIdx & 0xFF);
        *p++ = (uint8_t)((chunkIdx >> 8) & 0xFF);
        // [total_len:2 LE] — first chunk only
        if (first) {
            *p++ = (uint8_t)(tlvLen & 0xFF);
            *p++ = (uint8_t)((tlvLen >> 8) & 0xFF);
        }
        // [tlv slice]
        memcpy(p, tlv + offset, slice);
        p += slice;

        chr_->setValue(buf, (size_t)(p - buf));
        chr_->notify();
        // Slow down so the central can absorb back-to-back notifications.
        // Default MTU 23 case will need many chunks; pause briefly.
        if (offset + slice < tlvLen) delay(10);

        offset += slice;
        chunkIdx++;
    }
    return true;
}

bool ODBleTransport::connected() const { return connected_; }
