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

// Short form of the app image SHA used by the rest of the API (16 hex chars).
#define COREDUMP_SHA_SHORT_LEN 17

// ── Crash identity ───────────────────────────────────────────────────────────
// A coredump stores the crashing image's ELF SHA256 but no version string, so
// reporting "Firmware: <version>" from the running image produced a lie: after
// an OTA the old dump was labelled with the new firmware (measured on hardware:
// dump sha 21dc69fe..., running sha aaa60b73..., displayed v3.2.02f).
//
// Crash identity is captured while the crashing image is still the running one
// (i.e. at the boot that follows the panic) and persisted in NVS, so it stays
// correct across later reboots and OTAs. Every field below describes the image
// that CRASHED, never the image that is running now.
typedef struct {
    bool     valid;                              // false => no dump, or identity unknown (e.g. OTA leftover)
    bool     belongs_to_running_image;           // crash sha == running sha
    char     fw_version[24];                     // firmware version of the crashing image ("" when unknown)
    char     app_sha256[COREDUMP_SHA_SHORT_LEN]; // crashing image, 16 hex chars (always the dump's own value)
    uint32_t uptime_sec;                         // how long the crashing run lasted (0 when unknown)
    uint32_t min_free_heap;                      // min free internal heap during that run (bytes, 0 when unknown)
    uint32_t epoch_ts;                           // unix time the crash was detected (0 when unknown)
    uint32_t dump_size;                          // size of the stored dump image
} coredump_crash_identity_t;

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

// Raw flash read of the image, for HTTP streaming download. `offset` is relative
// to the start of the image as reported by esp_core_dump_image_get(), which
// still has the IDF image header in front of the ELF. Returns false when no
// dump exists or the flash read failed.
bool coredump_read(size_t offset, uint8_t* buf, size_t len);

// Offset of the ELF core inside that image (the IDF header in front of it is
// not part of the ELF, and gdb refuses a file that starts with it). Measured 20
// bytes on IDF 4.4/ESP32-S3; the value is discovered by scanning for the ELF
// magic instead of being hardcoded, and 0 is returned when no ELF is found
// (e.g. an unexpected compressed image), so callers fall back to the raw bytes.
size_t coredump_elf_offset(void);

// Erase the partition. Returns true on success.
bool coredump_erase();

// ── Crash identity ───────────────────────────────────────────────────────────
//
// Call once per boot, after reboot_log_init() has decided whether this boot
// followed a crash. Rules (idempotent, safe to call on every boot):
//
//   no dump            -> identity is cleared (nothing to describe)
//   after a crash      -> identity is (re)written from the *current* run, which
//                         is exactly the run that crashed, because a panic
//                         reboot loads the same image
//   not a crash        -> identity is only rebuilt when it does not already
//                         match the stored dump; this preserves the original
//                         crash context across ordinary reboots
//
// When the stored dump belongs to a DIFFERENT image than the one now running
// (the classic "OTA left an old dump behind" case) the version cannot be
// recovered - the dump carries no version string, verified by scanning it -
// so fw_version is left empty and belongs_to_running_image is false.
void coredump_capture_crash_identity(bool crashed_this_boot,
                                     const char* running_fw_version,
                                     const char* running_sha256_short,
                                     uint32_t uptime_sec,
                                     uint32_t min_free_heap,
                                     uint32_t epoch_ts);

// Read the persisted identity. Returns false when there is no dump or no
// identity stored. `running_sha256_short` (optional) lets the caller learn
// whether the dump belongs to the image that is running now.
bool coredump_get_crash_identity(coredump_crash_identity_t* out,
                                 const char* running_sha256_short = nullptr);

#ifdef __cplusplus
}
#endif
