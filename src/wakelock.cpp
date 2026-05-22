#include "homeplate.h"

// Wake locks: long-running operations (display refresh, HTTP fetch, image
// render, OTA) hold a wake lock for their actual duration so the sleep task
// doesn't deep-sleep mid-op. Each lock carries an absolute expiration; if
// the owner crashes or hangs past it, anyWakeLocksHeld() drops the lock so
// the device sleeps instead of draining the battery — sacrificing one op,
// not the day's worth of charge.

// Fixed-size array (no malloc): the sleep task walks this from a code path
// that should not touch the heap, and an ESP32 running for days is better
// off without fragmentation. Sized for the max number of concurrent ops
// (activity + http + display + ota + headroom).
static constexpr size_t MAX_WAKELOCKS = 8;

struct WakeLockEntry {
    const char *name;       // borrowed pointer; callers pass string literals
    uint32_t acquiredAt;    // millis() at acquire — for diagnostic "held Xms"
    uint32_t expiresAt;     // millis() deadline; safety net, not estimate
    bool active;
};

static WakeLockEntry locks[MAX_WAKELOCKS];
static SemaphoreHandle_t wakeLockMutex = xSemaphoreCreateMutex();

// Claim a slot and return its index as the handle. maxHoldSec is the
// hard upper bound on how long this op should ever take — pick ~3x the
// realistic worst case. On overflow or contention we return
// WAKELOCK_INVALID and the op proceeds unprotected (degrades to today's
// pre-wakelock behavior — better than blocking the caller).
WakeLockHandle acquireWakeLock(const char *name, uint32_t maxHoldSec)
{
    if (xSemaphoreTake(wakeLockMutex, 10 / portTICK_PERIOD_MS) != pdTRUE) {
        Serial.printf("[WAKELOCK][ERROR] mutex timeout acquiring '%s'\n", name);
        return WAKELOCK_INVALID;
    }
    for (size_t i = 0; i < MAX_WAKELOCKS; i++) {
        if (!locks[i].active) {
            uint32_t now = millis();
            locks[i].name = name;
            locks[i].acquiredAt = now;
            locks[i].expiresAt = now + maxHoldSec * 1000;
            locks[i].active = true;
            xSemaphoreGive(wakeLockMutex);
            return (WakeLockHandle)i;
        }
    }
    xSemaphoreGive(wakeLockMutex);
    // Overflow is a code smell — either a leak (forgotten release) or
    // genuine new concurrency that warrants raising MAX_WAKELOCKS.
    Serial.printf("[WAKELOCK][ERROR] full, dropping '%s' (raise MAX_WAKELOCKS)\n", name);
    return WAKELOCK_INVALID;
}

// Release a previously-acquired lock. Safe to call with WAKELOCK_INVALID
// (so callers don't need to branch on acquire failure) and safe on a slot
// the sleep task already auto-expired — in that case we log "late
// release" as a hint that maxHoldSec was too tight for this call site.
void releaseWakeLock(WakeLockHandle handle)
{
    if (handle == WAKELOCK_INVALID) return;
    if (handle < 0 || (size_t)handle >= MAX_WAKELOCKS) return;
    if (xSemaphoreTake(wakeLockMutex, 10 / portTICK_PERIOD_MS) != pdTRUE) {
        Serial.printf("[WAKELOCK][ERROR] mutex timeout releasing handle %d\n", handle);
        return;
    }
    WakeLockEntry &e = locks[handle];
    if (!e.active) {
        // Slot was already cleared by anyWakeLocksHeld() — the op finished
        // but missed its window. Pair this log with the forced-release log
        // to confirm it was a timeout (not a hang) and raise the budget.
        uint32_t now = millis();
        uint32_t heldMs = now - e.acquiredAt;
        uint32_t limitMs = e.expiresAt - e.acquiredAt;
        Serial.printf("[WAKELOCK] '%s' released %ums after expiration (limit %ums)\n",
                      e.name ? e.name : "?", heldMs, limitMs);
    } else {
        e.active = false;
    }
    xSemaphoreGive(wakeLockMutex);
}

// Called by the sleep task before deep-sleeping. Returns true if any
// live lock remains; expired locks are dropped (with a diagnostic log)
// rather than blocking sleep forever — the whole point of the safety
// net. Mutex-timeout returns false so a wedged mutex doesn't pin the
// device awake either.
bool anyWakeLocksHeld()
{
    if (xSemaphoreTake(wakeLockMutex, 10 / portTICK_PERIOD_MS) != pdTRUE) {
        return false;
    }
    uint32_t now = millis();
    bool held = false;
    for (size_t i = 0; i < MAX_WAKELOCKS; i++) {
        WakeLockEntry &e = locks[i];
        if (!e.active) continue;
        // Wraparound-safe deadline check: millis() rolls over every ~49
        // days, so a direct `now >= expiresAt` would mis-fire across the
        // boundary. Casting the unsigned difference to int32_t treats it
        // as a signed delta — positive means expired, negative means
        // still in the future.
        if ((int32_t)(now - e.expiresAt) >= 0) {
            uint32_t heldMs = now - e.acquiredAt;
            uint32_t limitMs = e.expiresAt - e.acquiredAt;
            Serial.printf("[WAKELOCK] forced release of '%s' held %ums (limit %ums)\n",
                          e.name ? e.name : "?", heldMs, limitMs);
            e.active = false;
            continue;
        }
        held = true;
    }
    xSemaphoreGive(wakeLockMutex);
    return held;
}
