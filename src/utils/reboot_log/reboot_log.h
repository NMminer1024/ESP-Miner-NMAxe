// Reboot log: distinguish planned vs unexpected reboots and persist a short history.
//
// Storage layers
//   - RTC NOINIT RAM : carries "intent" set by code just before ESP.restart(),
//                       and a periodic mirror of uptime / min-free-heap so that
//                       even on crash we know how long the device had been running.
//                       Survives software / panic resets, lost on power-down.
//   - NVS namespace "reboot" : ring of up to REBOOT_LOG_RING_SIZE records
//                       (each sizeof(RebootRecord) bytes), built at boot from the
//                       RTC state + esp_reset_reason().
//
// Public API is intentionally tiny: call reboot_log_init() once at the very top of
// setup(), call reboot_intent_set() (or the convenience reboot_planned()) before
// any code path that triggers a software restart, and call reboot_log_tick() once
// per second from a low-frequency task (daemon thread is the natural home).
#pragma once

#include <stdint.h>
#include <stddef.h>

// ── On-disk / on-RAM constants ───────────────────────────────────────────────
#define REBOOT_LOG_RING_SIZE      10        // number of historical records kept in NVS
#define REBOOT_LOG_DETAIL_LEN     56        // bytes for human-readable detail (incl. NUL)
#define REBOOT_LOG_FW_LEN         24        // bytes for fw_version string (incl. NUL)
#define REBOOT_RECORD_MAGIC       0x474C4252u   // 'RBLG'

// ── Active reboot intent (extend by APPENDING ONLY — values are persisted) ──
enum RebootIntent : uint8_t {
    REBOOT_INTENT_NONE                = 0,  // no intent set → unknown SW restart
    REBOOT_INTENT_USER_WEB_REBOOT     = 1,  // POST /api/system/restart
    REBOOT_INTENT_OTA_FINISHED        = 2,
    REBOOT_INTENT_FACTORY_RESET       = 3,
    REBOOT_INTENT_WIFI_CONFIG_TIMEOUT = 4,
    REBOOT_INTENT_WIFI_RECONNECT_FAIL = 5,
    REBOOT_INTENT_LOW_HASHRATE        = 6,
    REBOOT_INTENT_POOL_TIMEOUT        = 7,
    REBOOT_INTENT_ASIC_FROZEN         = 8,
    REBOOT_INTENT_CONFIG_CHANGED      = 9,
    REBOOT_INTENT_SELFTEST_TRIGGERED  = 10,
    REBOOT_INTENT_DAEMON_GENERIC      = 11, // fallback when reboot_xsem fired with no prior intent
    // Append new values here, do NOT renumber existing ones.
    REBOOT_INTENT__COUNT
};

// ── Coarse classification used by the web UI ─────────────────────────────────
enum RebootClass : uint8_t {
    REBOOT_CLASS_COLD       = 0,   // power-on (cold boot)
    REBOOT_CLASS_PLANNED    = 1,   // SW restart with a known intent
    REBOOT_CLASS_UNKNOWN_SW = 2,   // SW restart but no intent recorded
    REBOOT_CLASS_CRASH      = 3,   // panic / WDT
    REBOOT_CLASS_POWER      = 4,   // brown-out
    REBOOT_CLASS_EXTERNAL   = 5,   // RST pin / deep sleep / unknown
};

// ── Persistent record (NVS blob, fixed 128 bytes) ────────────────────────────
struct __attribute__((packed)) RebootRecord {
    uint32_t magic;                                 // REBOOT_RECORD_MAGIC
    uint32_t boot_index;                            // monotonic boot counter
    uint32_t epoch_ts;                              // unix time at boot (0 if unknown)
    uint32_t uptime_last_sec;                       // how long the previous run lasted
    uint32_t free_heap_min;                         // minimum free heap during previous run
    uint8_t  reset_reason;                          // esp_reset_reason_t value
    uint8_t  intent;                                // RebootIntent
    uint8_t  cls;                                   // RebootClass (precomputed)
    uint8_t  reserved0;
    char     fw_version[REBOOT_LOG_FW_LEN];         // firmware version string of the previous run
    char     detail[REBOOT_LOG_DETAIL_LEN];         // human-readable detail
    uint32_t reserved1[5];
    uint32_t crc32;                                 // CRC of all preceding bytes
};
static_assert(sizeof(RebootRecord) == 128, "RebootRecord must stay exactly 128 bytes");

// ── Public API ───────────────────────────────────────────────────────────────

// Build a record for the *previous* boot from RTC state + esp_reset_reason() and
// push it into the NVS ring. Must be called once, very early (top of setup()).
void reboot_log_init();

// Stamp the upcoming reboot with an intent + free-form detail (printf-style).
// Safe to call from any task; cheap (RTC write only).
void reboot_intent_set(RebootIntent intent, const char* detail_fmt, ...);

// Same as reboot_intent_set(), but only writes if no intent has been set yet
// during the current boot. Useful as a fallback at the central reboot site.
void reboot_intent_set_if_unset(RebootIntent intent, const char* detail_fmt, ...);

// Periodic mirror of uptime / minimum free heap into RTC, so that even a crash
// that bypasses NVS leaves "ran for X seconds, min heap Y" behind. Intended to
// be called ~1 Hz from the daemon thread. Cheap (RTC write + CRC).
void reboot_log_tick();

// Read the most recent record (if any). Returns true if out was filled.
bool reboot_log_get_last(RebootRecord* out);

// Read up to max_n most recent records (newest first). Returns the count written.
size_t reboot_log_get_recent(RebootRecord* out, size_t max_n);

// Drop all stored history. Does not reset the boot index.
void reboot_log_clear();

// String helpers (stable, safe to embed in JSON without escaping).
const char* reboot_intent_str(uint8_t v);
const char* reboot_reset_reason_str(uint8_t v);
const char* reboot_class_str(uint8_t v);
