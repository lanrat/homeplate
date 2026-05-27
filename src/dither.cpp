#include <Arduino.h>
#include <Inkplate.h>
#include <ctype.h>
#include "homeplate.h"

// Names for each library DitherKernel value, indexed by Image::DitherKernel
// (0..DITHER_KERNEL_COUNT-1). Used for the WiFiManager dropdown, serial logs,
// the published MQTT options list, and user-input matching — parseDitherName()
// normalizes its input (lowercase; strips '-', '_', whitespace) before
// comparing against these strings, so any case/punctuation works.
//
// The Inkplate library's DITHER_KERNELS array does not expose names, so we
// maintain this table here. The static_assert catches drift: if a future
// library version adds or removes a kernel, the build fails and this table
// must be updated to match.
static const char *const DITHER_KERNEL_NAMES[] = {
    "Floyd-Steinberg",      // 0 FloydSteinberg
    "Jarvis-Judice-Ninke",  // 1 JarvisJudiceNinke
    "Atkinson",             // 2 Atkinson
    "Burkes",               // 3 Burkes
    "Stucki",               // 4 Stucki
    "Sierra-Lite",          // 5 SierraLite
    "Reduced-Diffusion",    // 6 ReducedDiffusion
};
static_assert(sizeof(DITHER_KERNEL_NAMES) / sizeof(DITHER_KERNEL_NAMES[0]) == DITHER_KERNEL_COUNT,
    "DITHER_KERNEL_NAMES is out of sync with Inkplate library DITHER_KERNELS — "
    "update DITHER_KERNEL_NAMES in dither.cpp to match.");

const char *ditherKernelName(uint8_t value)
{
    if (value == 0)
        return "off";  // HA's mqtt.select treats "none" / "None" as Unknown
    uint8_t index = value - 1;
    if (index < DITHER_KERNEL_COUNT)
        return DITHER_KERNEL_NAMES[index];
    return "(unknown)";
}

// Lowercase and strip '-', '_', and whitespace into `out`. Returns out length.
static size_t normalizeDitherName(const char *in, char *out, size_t outSize)
{
    size_t n = 0;
    if (!in || outSize == 0)
    {
        if (outSize > 0) out[0] = '\0';
        return 0;
    }
    for (const char *p = in; *p && n + 1 < outSize; p++)
    {
        unsigned char c = (unsigned char)*p;
        if (c == '-' || c == '_' || c == ' ' || c == '\t')
            continue;
        out[n++] = (char)tolower(c);
    }
    out[n] = '\0';
    return n;
}

int8_t parseDitherName(const char *name)
{
    if (name == NULL)
        return -1;

    char norm[32];
    normalizeDitherName(name, norm, sizeof(norm));

    if (norm[0] == '\0'
        || strcmp(norm, "none") == 0
        || strcmp(norm, "false") == 0
        || strcmp(norm, "off") == 0
        || strcmp(norm, "0") == 0)
    {
        return 0;
    }

    char canonBuf[32];
    for (uint8_t i = 0; i < DITHER_KERNEL_COUNT; i++)
    {
        normalizeDitherName(DITHER_KERNEL_NAMES[i], canonBuf, sizeof(canonBuf));
        if (strcmp(norm, canonBuf) == 0)
            return (int8_t)(i + 1);
    }

    Serial.printf("[DITHER][ERROR] Unknown dither name '%s', falling back to default\n", name);
    return -1;
}

// Build a JSON array of supported canonical dither names, including "off".
// Returns bytes written (excluding NUL), or 0 on overflow.
size_t buildDitherOptionsJson(char *out, size_t outSize)
{
    if (!out || outSize < 3) return 0;
    int written = snprintf(out, outSize, "[\"off\"");
    if (written < 0 || (size_t)written >= outSize) return 0;
    size_t pos = (size_t)written;
    for (uint8_t i = 0; i < DITHER_KERNEL_COUNT; i++)
    {
        int n = snprintf(out + pos, outSize - pos, ",\"%s\"", DITHER_KERNEL_NAMES[i]);
        if (n < 0 || (size_t)n >= outSize - pos) return 0;
        pos += (size_t)n;
    }
    if (pos + 2 > outSize) return 0;
    out[pos++] = ']';
    out[pos] = '\0';
    return pos;
}
