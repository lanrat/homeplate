#include "opendisplay_config.h"
#include "opendisplay_proto.h"

#include <string.h>

namespace od {

namespace {

constexpr uint8_t PKT_SYSTEM       = 0x01;
constexpr uint8_t PKT_MANUFACTURER = 0x02;
constexpr uint8_t PKT_POWER        = 0x04;
constexpr uint8_t PKT_DISPLAY      = 0x20;

// Sizes from py-opendisplay protocol/config_parser.py _get_packet_size().
constexpr size_t SZ_SYSTEM       = 22;
constexpr size_t SZ_MANUFACTURER = 22;
constexpr size_t SZ_POWER        = 30;
constexpr size_t SZ_DISPLAY      = 46;

// transmission_modes bits, from py-opendisplay models/config.py.
constexpr uint8_t TM_ZIPXL        = 0x01; // PSRAM-backed large compressed buffer
constexpr uint8_t TM_ZIP          = 0x02; // zlib-compressed direct write
constexpr uint8_t TM_DIRECT_WRITE = 0x08; // uncompressed direct write (0x70/71/72)

// communication_modes bitfield (firmware convention; verified from
// reference firmware wifi_service.cpp).
constexpr uint8_t CM_WIFI = (1u << 2);

// Map our ImageFormat to the OpenDisplay color_scheme byte. Mirrors the
// scheme integer values published in epaper_dithering.palettes.ColorScheme.
uint8_t colorSchemeFor(ImageFormat fmt) {
    switch (fmt) {
        case ImageFormat::Monochrome1bppMSB:   return CS_MONOCHROME;    // 0
        case ImageFormat::Color6_4bppMSB:      return CS_SIX_COLOR;     // 4 (BWGBRY)
        case ImageFormat::Grayscale16_4bppMSB: return 6;                 // GRAYSCALE_16
        default:                                return CS_MONOCHROME;
    }
}

// Append [packet_number:1][packet_type:1] header + zeroed payload of `size` bytes.
// Returns pointer to the start of the payload so the caller can fill it.
uint8_t *appendPacketHeader(uint8_t *p, uint8_t &packetNum, uint8_t type, size_t size) {
    p[0] = packetNum++;
    p[1] = type;
    memset(p + 2, 0, size);
    return p + 2;
}

} // namespace

size_t buildReadConfigResponse(uint8_t *out, size_t cap,
                               const IRenderer &r,
                               uint32_t deepSleepSec)
{
    if (cap < READ_CONFIG_RESPONSE_BYTES) return 0;

    uint8_t *p = out;

    // ---- Response echo (2 bytes BE) ----
    wrU16BE(p, (uint16_t)CMD_READ_CONFIG | RESPONSE_ACK_FLAG);
    p += 2;

    // ---- Wrapper header. length = version(1) + packets(128) + crc(2) = 131 ----
    constexpr size_t packetsBytes = (2 + SZ_SYSTEM) + (2 + SZ_MANUFACTURER) +
                                    (2 + SZ_POWER)  + (2 + SZ_DISPLAY);
    constexpr uint16_t wrapperLen = (uint16_t)(1 + packetsBytes + 2);
    wrU16LE(p, wrapperLen);
    p += 2;
    *p++ = 0x01;  // version

    uint8_t *checksummedStart = p - 1;  // CRC covers version + packets

    uint8_t packetNum = 0;

    // ---- SYSTEM (0x01, 22 bytes) ----
    // Fields (LE within packet): ic_type(u16), comm_modes(u8), dev_flags(u8),
    //   pwr_pin(u8), reserved(15), pwr_pin_2(u8), pwr_pin_3(u8).
    {
        uint8_t *payload = appendPacketHeader(p, packetNum, PKT_SYSTEM, SZ_SYSTEM);
        p += 2 + SZ_SYSTEM;
        // ic_type: 0 = unspecified (Inkplate is classic ESP32 which has no
        // dedicated value in py-opendisplay's ICType enum).
        wrU16LE(payload + 0, 0);
        payload[2] = CM_WIFI;  // communication_modes: WiFi only in this release
        // dev_flags, pwr_pin, reserved, pwr_pin_2/3 all stay zero.
    }

    // ---- MANUFACTURER (0x02, 22 bytes) ----
    // Fields: mfg_id(u16), board_type(u8), board_revision(u8), reserved(18).
    {
        uint8_t *payload = appendPacketHeader(p, packetNum, PKT_MANUFACTURER, SZ_MANUFACTURER);
        p += 2 + SZ_MANUFACTURER;
        // 0 = DIY (per BoardManufacturer enum). Honest: HomePlate isn't a
        // commercial OpenDisplay board.
        wrU16LE(payload + 0, 0);
        // board_type, board_revision, reserved: zero.
    }

    // ---- POWER (0x04, 30 bytes) ----
    // Fields (LE): power_mode(u8), battery_capacity(3 bytes), sleep_timeout(u16),
    //   tx_power(s8), sleep_flags(u8), batt_sense_pin(u8), batt_sense_en_pin(u8),
    //   batt_sense_flags(u8), capacity_estimator(u8), voltage_scaling(u16),
    //   deep_sleep_current_ua(u32), deep_sleep_time_seconds(u16), reserved(10).
    {
        uint8_t *payload = appendPacketHeader(p, packetNum, PKT_POWER, SZ_POWER);
        p += 2 + SZ_POWER;
        payload[0] = 1;  // power_mode: BATTERY (Inkplate is battery-capable)
        // battery_capacity_mah: 3000 mAh typical (24-bit LE)
        payload[1] = 0xB8;
        payload[2] = 0x0B;
        payload[3] = 0x00;
        // sleep_timeout(2), tx_power(1), sleep_flags(1), batt_sense_pin(1),
        // batt_sense_en_pin(1), batt_sense_flags(1) — leave zero.
        payload[10] = 1;  // capacity_estimator: LI_ION
        // voltage_scaling(2 @ offset 11), deep_sleep_current(4 @ 13) — zero.
        // deep_sleep_time_seconds @ offset 17 (u16 LE)
        uint16_t sleepClamped = deepSleepSec > 0xFFFF ? 0xFFFF : (uint16_t)deepSleepSec;
        wrU16LE(payload + 17, sleepClamped);
        // reserved (10 bytes @ offset 19): zero.
    }

    // ---- DISPLAY (0x20, 46 bytes) — the field controllers actually use ----
    // Fields (LE): instance(u8), display_tech(u8), panel_ic(u16),
    //   pixel_width(u16), pixel_height(u16), active_w_mm(u16),
    //   active_h_mm(u16), tag_type(u16), rotation(u8), reset_pin(u8),
    //   busy_pin(u8), dc_pin(u8), cs_pin(u8), data_pin(u8),
    //   partial_update(u8), color_scheme(u8), trans_modes(u8), clk_pin(u8),
    //   reserved_pins(7), full_update_mC(u16), reserved(11).
    {
        uint8_t *payload = appendPacketHeader(p, packetNum, PKT_DISPLAY, SZ_DISPLAY);
        p += 2 + SZ_DISPLAY;
        payload[0] = 0;  // instance_number
        payload[1] = 0;  // display_technology (unspecified)
        wrU16LE(payload + 2, 0);            // panel_ic_type — none of the
                                             // upstream IDs match Inkplate panels
        wrU16LE(payload + 4, r.width());    // pixel_width
        wrU16LE(payload + 6, r.height());   // pixel_height
        // active_w_mm, active_h_mm, tag_type — unknown, leave zero
        // rotation, reset/busy/dc/cs/data pins — N/A for our setup, zero
        payload[20] = r.supportsPartialUpdate() ? 1 : 0;
        payload[21] = colorSchemeFor(r.format());
        payload[22] = TM_DIRECT_WRITE | TM_ZIP | TM_ZIPXL;
        // clk_pin(1), reserved_pins(7), full_update_mC(2), reserved(11) — zero.
    }

    // ---- CRC16-CCITT over (version + packets) ----
    size_t checksummedLen = (size_t)(p - checksummedStart);
    uint16_t crc = crc16ccitt(checksummedStart, checksummedLen);
    wrU16LE(p, crc);
    p += 2;

    return (size_t)(p - out);
}

} // namespace od
