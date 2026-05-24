// OpenDisplay (Flex profile) wire-protocol primitives.
//
// Pure C++ — no Arduino, no network, no hardware dependencies. The
// transport layer (WiFi listener) and the panel renderer live elsewhere.
//
// Wire format (verified from py-opendisplay + OpenDisplay Firmware ref):
//   - WiFi LAN frame: [length:u16 little-endian][payload bytes]
//   - Inside payload, command code is u16 BIG-endian, followed by command-
//     specific bytes.
//   - ACK responses echo the command with the high bit set (CMD | 0x8000).
//
// Multi-byte fields within payloads follow command-specific endianness;
// see comments next to each constant.

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace od {

// 2-byte big-endian command codes shared by BLE and WiFi LAN transports.
enum CommandCode : uint16_t {
    CMD_REBOOT                       = 0x000F,
    CMD_READ_CONFIG                  = 0x0040,
    CMD_WRITE_CONFIG                 = 0x0041,
    CMD_WRITE_CONFIG_CHUNK           = 0x0042,
    CMD_READ_FW_VERSION              = 0x0043,
    CMD_AUTHENTICATE                 = 0x0050,
    CMD_DIRECT_WRITE_START           = 0x0070,
    CMD_DIRECT_WRITE_DATA            = 0x0071,
    CMD_DIRECT_WRITE_END             = 0x0072,
    CMD_DIRECT_WRITE_REFRESH_DONE    = 0x0073, // device->host
    CMD_DIRECT_WRITE_REFRESH_TIMEOUT = 0x0074, // device->host
    CMD_DIRECT_WRITE_PARTIAL_START   = 0x0076,
};

static constexpr uint16_t RESPONSE_ACK_FLAG    = 0x8000;
static constexpr uint16_t WIFI_LAN_MAX_PAYLOAD = 4096;
static constexpr uint16_t CHUNK_SIZE_PLAIN     = 230;

// OpenDisplay color schemes (basic-standard enum; Flex uses same values).
enum ColorScheme : uint8_t {
    CS_MONOCHROME    = 0x00,
    CS_BW_RED        = 0x01,
    CS_BW_YELLOW     = 0x02,
    CS_BW_RED_YELLOW = 0x03,
    CS_SIX_COLOR     = 0x04,
};

// Refresh modes accepted in DIRECT_WRITE_END.
enum RefreshMode : uint8_t {
    REFRESH_FULL = 0,
    REFRESH_FAST = 1,
};

// --- Endian helpers ---------------------------------------------------------

inline uint16_t rdU16BE(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] << 8 | (uint16_t)b[1]);
}
inline uint16_t rdU16LE(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | (uint16_t)b[1] << 8);
}
inline uint32_t rdU32LE(const uint8_t *b) {
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
inline void wrU16BE(uint8_t *b, uint16_t v) {
    b[0] = (uint8_t)(v >> 8);
    b[1] = (uint8_t)(v & 0xFF);
}
inline void wrU16LE(uint8_t *b, uint16_t v) {
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)(v >> 8);
}

// Build a 2-byte ACK payload (just the echoed command with high bit set).
// Returns bytes written (always 2).
inline size_t buildAck(uint8_t *out, uint16_t cmd) {
    wrU16BE(out, cmd | RESPONSE_ACK_FLAG);
    return 2;
}

// CRC16-CCITT (poly 0x1021, init 0xFFFF, no reflection, no final XOR).
// Used for the READ_CONFIG response wrapper. Most parsers (incl. py-
// opendisplay) ignore the trailing checksum, but we emit a valid value
// for stricter integrations.
inline uint16_t crc16ccitt(const uint8_t *buf, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)buf[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

} // namespace od
