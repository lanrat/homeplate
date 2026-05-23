#include "opendisplay_session.h"
#include "opendisplay_proto.h"
#include "opendisplay_decompress.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

namespace od {

namespace {

constexpr uint32_t MAX_RAW_IMAGE_BYTES = 256 * 1024; // covers all Inkplates

struct UploadCtx {
    uint8_t *buf;          // accumulator (compressed bytes if compressed, raw bytes otherwise)
    uint32_t bufCap;       // allocation size of buf
    uint32_t bytesIn;      // bytes appended so far
    bool     active;
    bool     compressed;
    uint32_t uncompressedSize; // declared in 0x70 (compressed mode only)
};

void resetUpload(UploadCtx &u) {
    if (u.buf) {
        free(u.buf);
        u.buf = nullptr;
    }
    u.bufCap = 0;
    u.bytesIn = 0;
    u.active = false;
    u.compressed = false;
    u.uncompressedSize = 0;
}

bool sendAck(ITransport &t, uint16_t cmd) {
    uint8_t ack[2];
    buildAck(ack, cmd);
    return t.sendFrame(ack, 2);
}

// Try to give the renderer a finished raw bitmap. On compressed uploads
// this decompresses into a fresh buffer first. Returns true if rendered.
bool finalizeAndRender(UploadCtx &up, IRenderer &r, LogFn log, char *dbg, size_t dbgCap) {
    if (!up.compressed) {
        return r.renderImage(up.buf, up.bytesIn);
    }
    // Compressed path. Allocate a decompression target sized to the
    // declared uncompressed length.
    uint8_t *raw = (uint8_t *)malloc(up.uncompressedSize);
    if (!raw) {
        if (log) log("[OD] malloc failed for decompressed buffer");
        return false;
    }
    size_t produced = 0;
    bool ok = zlibDecompress(up.buf, up.bytesIn, raw, up.uncompressedSize, &produced);
    // Render whenever we produced the full expected byte count, regardless
    // of uzlib's residual return code — uzlib has a quirk where it returns
    // a non-DONE code when the output buffer fills exactly at end-of-stream
    // (the trailing adler32 never gets verified), but the produced bytes
    // are intact. We still bail on real failures: short output, or
    // decompressor reporting a hard error before producing the expected
    // count.
    bool fullOutput = (produced == up.uncompressedSize);
    if (!ok && !fullOutput) {
        if (log) {
            snprintf(dbg, dbgCap, "[OD] decompress failed at byte %u/%u",
                     (unsigned)produced, (unsigned)up.uncompressedSize);
            log(dbg);
        }
        free(raw);
        return false;
    }
    if (log) {
        snprintf(dbg, dbgCap, "[OD] decompressed %u -> %u bytes%s",
                 (unsigned)up.bytesIn, (unsigned)produced,
                 ok ? "" : " (adler32 unchecked, full output)");
        log(dbg);
    }
    bool rendered = r.renderImage(raw, (uint32_t)produced);
    free(raw);
    return rendered;
}

} // namespace

bool runSession(ITransport &t, IRenderer &r, LogFn log, uint32_t loopTimeoutMs)
{
    UploadCtx up = {nullptr, 0, 0, false};
    uint8_t frame[WIFI_LAN_MAX_PAYLOAD];

    bool rendered = false;
    char dbg[96];

    while (t.connected()) {
        int n = t.recvFrame(frame, sizeof(frame), loopTimeoutMs);
        if (n == 0) {
            if (log) log("[OD] frame timeout, ending session");
            break;
        }
        if (n < 0) {
            if (log) log("[OD] transport error");
            break;
        }
        if (n < 2) {
            if (log) log("[OD] short frame, ignoring");
            continue;
        }

        uint16_t cmd = rdU16BE(frame);
        const uint8_t *payload = frame + 2;
        int payloadLen = n - 2;

        switch (cmd) {
        case CMD_DIRECT_WRITE_START: {
            resetUpload(up);
            // Two flows (per py-opendisplay protocol/commands.py):
            //   Uncompressed: empty payload. All bytes follow via 0x71.
            //   Compressed:   [uncompressed_size:4 LE][first_chunk_zlib_data]
            //                 Subsequent 0x71 frames append more compressed
            //                 bytes; we decompress all on 0x72.
            if (payloadLen == 0) {
                // Uncompressed.
                up.compressed = false;
                up.bufCap = r.expectedBytes();
                if (up.bufCap == 0 || up.bufCap > MAX_RAW_IMAGE_BYTES) {
                    if (log) log("[OD] renderer reports invalid expectedBytes");
                    sendAck(t, cmd);
                    break;
                }
            } else if (payloadLen >= 4) {
                // Compressed. We bound the compressed buffer cap to the
                // declared uncompressed size — for image data, compressed
                // is always smaller, so this is a safe upper bound.
                up.compressed = true;
                up.uncompressedSize = rdU32LE(payload);
                if (up.uncompressedSize == 0 || up.uncompressedSize > MAX_RAW_IMAGE_BYTES) {
                    if (log) log("[OD] compressed start: invalid uncompressed_size");
                    sendAck(t, cmd);
                    break;
                }
                if (up.uncompressedSize != r.expectedBytes()) {
                    if (log) {
                        snprintf(dbg, sizeof(dbg), "[OD] compressed size mismatch: got %u, panel needs %u",
                                 (unsigned)up.uncompressedSize, (unsigned)r.expectedBytes());
                        log(dbg);
                    }
                    // We still accept it — controller may be sending a
                    // larger framebuffer (e.g. with palette planes) that
                    // we don't yet support. Render will be partial.
                }
                up.bufCap = up.uncompressedSize; // upper bound for compressed
            } else {
                if (log) log("[OD] START payload too short for compressed mode");
                sendAck(t, cmd);
                break;
            }
            up.buf = (uint8_t *)malloc(up.bufCap);
            if (!up.buf) {
                if (log) log("[OD] malloc failed for upload buffer");
                sendAck(t, cmd);
                break;
            }
            up.active = true;
            // Append any initial chunk that came in the START payload.
            if (up.compressed && payloadLen > 4) {
                uint32_t take = (uint32_t)payloadLen - 4;
                if (take > up.bufCap) take = up.bufCap;
                memcpy(up.buf, payload + 4, take);
                up.bytesIn = take;
            }
            if (log) {
                snprintf(dbg, sizeof(dbg), "[OD] direct-write start (%s), buf=%u bytes",
                         up.compressed ? "compressed" : "uncompressed",
                         (unsigned)up.bufCap);
                log(dbg);
            }
            sendAck(t, cmd);
            break;
        }

        case CMD_DIRECT_WRITE_DATA: {
            if (!up.active || !up.buf) {
                if (log) log("[OD] DATA before START, ignoring");
                sendAck(t, cmd);
                break;
            }
            uint32_t remaining = up.bufCap - up.bytesIn;
            uint32_t take = (uint32_t)payloadLen;
            if (take > remaining) take = remaining;
            memcpy(up.buf + up.bytesIn, payload, take);
            up.bytesIn += take;
            sendAck(t, cmd);
            break;
        }

        case CMD_DIRECT_WRITE_END: {
            // payload[0] = refresh mode (full=0, fast=1). We always do
            // a full refresh on the Inkplate for OpenDisplay images.
            if (!up.active || !up.buf) {
                if (log) log("[OD] END without START");
                sendAck(t, cmd);
                break;
            }
            sendAck(t, cmd);
            if (log) {
                snprintf(dbg, sizeof(dbg), "[OD] direct-write end, received %u/%u bytes",
                         (unsigned)up.bytesIn, (unsigned)up.bufCap);
                log(dbg);
            }
            bool ok = finalizeAndRender(up, r, log, dbg, sizeof(dbg));
            resetUpload(up);
            rendered = rendered || ok;
            // Notify controller that refresh completed. ACK echo of 0x73
            // (DIRECT_WRITE_REFRESH_DONE) is how the firmware signals
            // completion to the host.
            uint8_t done[2];
            wrU16BE(done, (uint16_t)CMD_DIRECT_WRITE_REFRESH_DONE | RESPONSE_ACK_FLAG);
            t.sendFrame(done, 2);
            return rendered;
        }

        case CMD_READ_FW_VERSION:
        case CMD_READ_CONFIG:
        default: {
            // Anything we don't explicitly handle gets a bare ACK so the
            // controller doesn't hang waiting on us. The ACK has no
            // payload — controllers that strictly require config / fw
            // version data will fail here. Documented limitation.
            if (log) {
                snprintf(dbg, sizeof(dbg), "[OD] unhandled cmd 0x%04x, sending bare ACK", cmd);
                log(dbg);
            }
            sendAck(t, cmd);
            break;
        }
        }
    }

    resetUpload(up);
    return rendered;
}

} // namespace od
