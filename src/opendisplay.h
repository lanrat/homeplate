// Shared OpenDisplay helpers exported from src/opendisplay.cpp for use by
// transport-specific files (opendisplay_wifi.cpp, opendisplay_ble.cpp).

#pragma once

#include <stdint.h>

// Populate the 14-byte v1-format MSD (manufacturer-specific data) blob with
// current battery, temperature, and status fields. WiFi hex-encodes the blob
// for the mDNS TXT record; BLE emits the bytes directly inside the advert.
// loopCounter is the low 4 bits of bootCount.
void buildOdMsdBytes(uint8_t msd[14], uint8_t loopCounter);
