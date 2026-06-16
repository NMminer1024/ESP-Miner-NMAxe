// Coredump access (Plan B): thin wrapper around esp_core_dump_* APIs.
//
// Arduino-ESP32 ships with CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y and ELF format,
// so a panic / WDT will automatically save a coredump to the "coredump" partition.
// This module only exposes:
//   - presence / size queries
//   - a high-level "summary" (task name + PC + short backtrace) for the UI
//   - raw flash read of N bytes at offset (for HTTP streaming download)
//   - erase, called after the user has saved the dump
//
// Notes
//   - All getters are read-only and safe to call from any non-IRAM task.
//   - Only one in-flight download at a time is expected (single user, embedded UI),
//     so no global mutex; the underlying esp_partition_read is itself thread-safe.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Lightweight summary mirrored from esp_core_dump_summary_t. Limited backtrace
// depth keeps the JSON response small enough for ArduinoJson on a 240KB heap.
#define COREDUMP_BT_MAX     16
#define COREDUMP_TASK_LEN   16
#define COREDUMP_SHA_LEN    65   // 64 hex chars + NUL

typedef struct {
    bool      valid;                       // true if esp_core_dump_get_summary succeeded
    char      task[COREDUMP_TASK_LEN];     // crashing task name
    uint32_t  pc;                          // program counter at fault
    uint32_t  bt[COREDUMP_BT_MAX];         // backtrace PCs (truncated)
    uint8_t   bt_depth;                    // valid entries in bt[]
    bool      bt_corrupted;                // hardware reported BT may be partial
    char      app_sha256[COREDUMP_SHA_LEN];// SHA256 of crashing fw image (hex)
} coredump_summary_lite_t;

// Returns true and fills *out_size_bytes if a coredump image is present in
// flash (even if its CRC is bad). Use *out_crc_ok to distinguish a clean dump
// from one that may have been cut short by a flash-write failure during the crash.
bool coredump_present(size_t* out_size_bytes, bool* out_crc_ok = nullptr);

// Best-effort summary. Returns false if no dump or the SDK refused (e.g. checksum
// mismatch). On failure *out is zeroed.
bool coredump_get_summary(coredump_summary_lite_t* out);

// Erase the partition. Returns true on success.
bool coredump_erase();

#ifdef __cplusplus
}
#endif
