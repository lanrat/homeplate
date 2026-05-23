// One-shot zlib decompressor for OpenDisplay compressed direct-write.
//
// Wraps pfalcon/uzlib with a 32 KB dictionary buffer. Zlib license
// (https://github.com/pfalcon/uzlib) so safe to use from Apache-2.0 code.

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace od {

// Decompress a complete zlib-wrapped deflate buffer into dst.
// Returns true on success; *outLen receives the bytes written.
// On failure dst contents are undefined and *outLen is set to bytes
// produced before the error (useful for diagnostics).
//
// The dictionary buffer (32 KB) is allocated internally on each call so
// the caller doesn't pay the memory cost outside an active session.
bool zlibDecompress(const uint8_t *src, size_t srcLen,
                    uint8_t *dst, size_t dstCap,
                    size_t *outLen);

} // namespace od
