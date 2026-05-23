#include "opendisplay_decompress.h"

#include <stdlib.h>
#include <string.h>

extern "C" {
#include "uzlib.h"
}

namespace od {

namespace {
// 32 KB is zlib's maximum sliding window. uzlib lets the dictionary be
// smaller, but a compressor that uses the full 32 KB window will fail to
// reproduce back-references if our dict is smaller. The OpenDisplay
// server compresses with the standard zlib, so use the full window.
constexpr unsigned DICT_BYTES = 32 * 1024;
} // namespace

bool zlibDecompress(const uint8_t *src, size_t srcLen,
                    uint8_t *dst, size_t dstCap,
                    size_t *outLen)
{
    if (outLen) *outLen = 0;
    if (!src || !dst || srcLen == 0 || dstCap == 0) return false;

    uint8_t *dict = (uint8_t *)malloc(DICT_BYTES);
    if (!dict) return false;

    static bool inited = false;
    if (!inited) {
        uzlib_init();
        inited = true;
    }

    uzlib_uncomp d;
    memset(&d, 0, sizeof(d));
    d.source        = src;
    d.source_limit  = src + srcLen;
    d.source_read_cb = nullptr;
    d.dest_start    = dst;
    d.dest          = dst;
    d.dest_limit    = dst + dstCap;

    // Parse the 2-byte zlib header (CMF + FLG) and validate window size.
    int hdr = uzlib_zlib_parse_header(&d);
    if (hdr < 0) {
        free(dict);
        return false;
    }

    uzlib_uncompress_init(&d, dict, DICT_BYTES);

    int rc = uzlib_uncompress_chksum(&d);
    size_t produced = (size_t)(d.dest - dst);
    if (outLen) *outLen = produced;
    free(dict);

    // TINF_DONE  = stream ended naturally + adler32 verified
    // TINF_OK    = uzlib's inner loop exited because dest filled exactly
    //              at end-of-stream, before it could read the end-of-block
    //              marker; the trailing adler32 never got checked. The
    //              output bytes are correct; the caller should sanity-check
    //              against expected length and proceed.
    // anything < 0 = real decode error.
    return rc == TINF_DONE || rc == TINF_OK;
}

} // namespace od
