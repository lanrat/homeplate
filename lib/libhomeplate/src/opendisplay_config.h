// Minimal OpenDisplay READ_CONFIG (0x0040) response builder.
//
// Emits the smallest TLV blob py-opendisplay's parser accepts:
// SYSTEM (0x01), MANUFACTURER (0x02), POWER (0x04), DISPLAY (0x20).
// Everything else (LED, SENSOR, TOUCH, WIFI, SECURITY, ...) is optional
// and omitted — controllers reading our config get the four mandatory
// packets plus the wrapper that py-opendisplay's parse_config_response()
// expects ([length:2 LE][version:1][packets...][crc:2]).

#pragma once

#include "opendisplay_session.h"

#include <stdint.h>
#include <stddef.h>

namespace od {

// Build a complete READ_CONFIG response payload (suitable for passing
// straight to ITransport::sendFrame). Layout:
//   [echo:2 BE]   = (CMD_READ_CONFIG | RESPONSE_ACK_FLAG)
//   [length:2 LE] = byte count of (version + packets + crc)
//   [version:1]   = 1
//   [packets...]  = 4 TLV packets, fixed sizes per py-opendisplay
//   [crc:2 LE]    = CRC16-CCITT over (version + packets)
//
// Total: 2 + 3 + (24 + 24 + 32 + 48) + 2 = 135 bytes.
//
// `deepSleepSec` populates POWER.deep_sleep_time_seconds, telling
// controllers how long until our next wake.
// Returns bytes written, or 0 if cap is too small.
size_t buildReadConfigResponse(uint8_t *out, size_t cap,
                               const IRenderer &r,
                               uint32_t deepSleepSec);

// Total bytes the response will occupy. Useful for sizing buffers.
constexpr size_t READ_CONFIG_RESPONSE_BYTES = 2 + 3 + (24 + 24 + 32 + 48) + 2;

} // namespace od
