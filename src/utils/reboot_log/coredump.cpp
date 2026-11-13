#include "coredump.h"

#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <esp_err.h>
#include <esp_core_dump.h>
#include <esp_partition.h>
#include <nvs.h>

#include "../logger/logger.h"

// ── Crash identity storage (NVS namespace "crashid") ────────────────────────
// See coredump.h: every field describes the image that crashed, never the one
// running now. Kept in its own namespace so it survives OTA and cannot be
// overwritten by the reboot-history ring.
#define CRASH_ID_MAGIC 0x44495843u   // 'CXID'
static const char* NS_CRASHID = "crashid";
static const char* K_CRASHID  = "id";

typedef struct __attribute__((packed)) {
    uint32_t magic;
    char     fw_version[24];
    char     app_sha256[COREDUMP_SHA_SHORT_LEN];
    uint32_t uptime_sec;
    uint32_t min_free_heap;
    uint32_t epoch_ts;
    uint32_t dump_size;
    uint32_t crc32;
} crash_id_blob_t;

static uint32_t crash_id_crc(const crash_id_blob_t* b) {
    const uint8_t* p = (const uint8_t*)b;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < offsetof(crash_id_blob_t, crc32); ++i) {
        crc ^= p[i];
        for (int k = 0; k < 8; ++k) {
            uint32_t mask = -(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static bool crash_id_load(crash_id_blob_t* out) {
    nvs_handle_t h;
    if (nvs_open(NS_CRASHID, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = sizeof(*out);
    esp_err_t err = nvs_get_blob(h, K_CRASHID, out, &len);
    nvs_close(h);
    if (err != ESP_OK || len != sizeof(*out)) return false;
    if (out->magic != CRASH_ID_MAGIC) return false;
    return out->crc32 == crash_id_crc(out);
}

static bool crash_id_save(const crash_id_blob_t* b) {
    crash_id_blob_t tmp = *b;
    tmp.magic = CRASH_ID_MAGIC;
    tmp.crc32 = crash_id_crc(&tmp);

    nvs_handle_t h;
    if (nvs_open(NS_CRASHID, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(h, K_CRASHID, &tmp, sizeof(tmp));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

static void crash_id_erase() {
    nvs_handle_t h;
    if (nvs_open(NS_CRASHID, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, K_CRASHID);
    nvs_commit(h);
    nvs_close(h);
}

// ── Presence ────────────────────────────────────────────────────────────────
bool coredump_present(size_t* out_size_bytes, bool* out_crc_ok) {
    size_t addr = 0, size = 0;
    if (esp_core_dump_image_get(&addr, &size) != ESP_OK) return false;
    if (size == 0) return false;
    // CRC failure does NOT mean "no coredump": the panic handler may have been
    // interrupted mid-write (common with TWDT crashes where flash bus contention
    // is high). We still surface the dump so the user sees the device crashed.
    bool crc_ok = (esp_core_dump_image_check() == ESP_OK);
    if (out_crc_ok)    *out_crc_ok    = crc_ok;
    if (out_size_bytes) *out_size_bytes = size;
    return true;
}

// ── Summary ────────────────────────────────────────────────────────────────
bool coredump_get_summary(coredump_summary_lite_t* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));

    // The ESP-IDF struct is hefty (esp_core_dump_summary_t with full BT array),
    // so allocate on the heap to avoid blowing a small task stack.
    auto* full = (esp_core_dump_summary_t*)malloc(sizeof(esp_core_dump_summary_t));
    if (!full) {
        LOG_W("[coredump] alloc summary failed");
        return false;
    }

    esp_err_t err = esp_core_dump_get_summary(full);
    if (err != ESP_OK) {
        LOG_I("[coredump] no summary (err=0x%x)", err);
        free(full);
        return false;
    }

    out->valid = true;
    strncpy(out->task, full->exc_task, COREDUMP_TASK_LEN - 1);
    out->pc = full->exc_pc;

    // Copy backtrace (clamped to our small array).
    uint32_t depth = full->exc_bt_info.depth;
    if (depth > COREDUMP_BT_MAX) depth = COREDUMP_BT_MAX;
    out->bt_depth     = (uint8_t)depth;
    out->bt_corrupted = full->exc_bt_info.corrupted;
    for (uint32_t i = 0; i < depth; ++i) {
        out->bt[i] = full->exc_bt_info.bt[i];
    }

    // SHA256 of the crashing app image, hex-formatted for direct JSON embedding.
    // Each byte → 2 hex chars; APP_ELF_SHA256_SZ already includes its own NUL,
    // and the SDK stores it as ASCII bytes (not raw hash). Just copy as string.
    size_t sha_in = strnlen((const char*)full->app_elf_sha256, APP_ELF_SHA256_SZ);
    if (sha_in >= COREDUMP_SHA_LEN) sha_in = COREDUMP_SHA_LEN - 1;
    memcpy(out->app_sha256, full->app_elf_sha256, sha_in);
    out->app_sha256[sha_in] = '\0';

    free(full);
    return true;
}

// ── Raw read (download) ────────────────────────────────────────────────────
bool coredump_read(size_t offset, uint8_t* buf, size_t len) {
    if (buf == nullptr || len == 0) return false;
    size_t addr = 0, size = 0;
    if (esp_core_dump_image_get(&addr, &size) != ESP_OK) return false;
    if (size == 0 || offset >= size) return false;
    if (offset + len > size) len = size - offset;
    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, "coredump");
    if (part == nullptr) return false;
    // esp_core_dump_image_get() reports an ABSOLUTE flash address, while
    // esp_partition_read() wants an offset from the start of the partition.
    // Passing the absolute address straight through makes the read fail with
    // ESP_ERR_INVALID_ARG and the download streams 0 bytes (observed on
    // hardware). Accept either form so a future IDF that switches to
    // partition-relative addressing keeps working.
    size_t base = addr;
    if (addr >= part->address && addr < part->address + part->size) {
        base = addr - part->address;
    }
    return esp_partition_read(part, base + offset, buf, len) == ESP_OK;
}

// Offset of the ELF core inside the dump image.
//
// esp_core_dump_image_get() returns the image starting AT the IDF header (20
// bytes on IDF 4.4 / ESP32-S3, measured on hardware: the image begins with a
// u32 size field and the ELF magic sits at +0x14). gdb will not load a file
// whose first bytes are not an ELF header, so the download endpoint needs to
// skip it. Discovering the offset by scanning keeps this correct if the header
// grows, and returning 0 keeps the endpoint working for a non-ELF image.
size_t coredump_elf_offset(void) {
    static const uint8_t ELF_MAGIC[4] = {0x7F, 'E', 'L', 'F'};
    uint8_t probe[64];
    if (!coredump_read(0, probe, sizeof(probe))) return 0;
    for (size_t i = 0; i + sizeof(ELF_MAGIC) <= sizeof(probe); ++i) {
        if (memcmp(probe + i, ELF_MAGIC, sizeof(ELF_MAGIC)) == 0) return i;
    }
    return 0;
}

// ── Erase ──────────────────────────────────────────────────────────────────
bool coredump_erase() {
    esp_err_t err = esp_core_dump_image_erase();
    if (err != ESP_OK) {
        LOG_W("[coredump] erase failed (err=0x%x)", err);
        return false;
    }
    // The identity describes this dump; once the dump is gone it is stale and
    // must not be attached to a future dump from a different image.
    crash_id_erase();
    LOG_I("[coredump] erased (identity cleared)");
    return true;
}

// ── Crash identity ─────────────────────────────────────────────────────────
void coredump_capture_crash_identity(bool crashed_this_boot,
                                     const char* running_fw_version,
                                     const char* running_sha256_short,
                                     uint32_t uptime_sec,
                                     uint32_t min_free_heap,
                                     uint32_t epoch_ts) {
    size_t sz = 0;
    if (!coredump_present(&sz, nullptr) || sz == 0) {
        crash_id_erase();
        return;
    }

    // The dump's own SHA is the only trustworthy identity it carries.
    coredump_summary_lite_t s;
    if (!coredump_get_summary(&s) || !s.valid) {
        // Unreadable summary: keep whatever we already had instead of
        // replacing it with guesses.
        LOG_W("[coredump] identity: summary unreadable, keeping previous");
        return;
    }

    char dump_sha[COREDUMP_SHA_SHORT_LEN] = {0};
    strncpy(dump_sha, s.app_sha256, COREDUMP_SHA_SHORT_LEN - 1);

    crash_id_blob_t existing;
    const bool have_existing = crash_id_load(&existing);

    // Ordinary reboot over a dump that is already described: leave it alone.
    // Rebuilding here would replace the crash run's uptime/heap with the
    // values of the run that just started.
    if (!crashed_this_boot && have_existing &&
        strncmp(existing.app_sha256, dump_sha, COREDUMP_SHA_SHORT_LEN - 1) == 0) {
        return;
    }

    crash_id_blob_t b;
    memset(&b, 0, sizeof(b));
    strncpy(b.app_sha256, dump_sha, COREDUMP_SHA_SHORT_LEN - 1);
    b.dump_size = (uint32_t)sz;

    const bool same_image = running_sha256_short && running_sha256_short[0] &&
                            strncmp(dump_sha, running_sha256_short,
                                    COREDUMP_SHA_SHORT_LEN - 1) == 0;
    if (same_image) {
        // Same image as the one running now, so its version string is valid
        // even when this boot is not the one that crashed.
        strncpy(b.fw_version, running_fw_version ? running_fw_version : "",
                sizeof(b.fw_version) - 1);
        if (crashed_this_boot) {
            // Only a crash boot can attribute uptime/heap to the crashing run.
            b.uptime_sec    = uptime_sec;
            b.min_free_heap = min_free_heap;
            b.epoch_ts      = epoch_ts;
        }
    }
    // else: the dump was produced by a different image (typically an OTA
    // leftover). It carries no version string - verified by scanning a real
    // dump: no "v3.2.02*" text and no esp_app_desc magic - so the version is
    // genuinely unrecoverable and stays empty rather than being invented.

    if (crash_id_save(&b)) {
        LOG_I("[coredump] identity: sha=%s fw=%s uptime=%us%s",
              b.app_sha256, b.fw_version[0] ? b.fw_version : "(unknown)",
              (unsigned)b.uptime_sec,
              same_image ? "" : " (legacy dump from another image)");
    }
}

bool coredump_get_crash_identity(coredump_crash_identity_t* out,
                                 const char* running_sha256_short) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));

    size_t sz = 0;
    if (!coredump_present(&sz, nullptr) || sz == 0) return false;

    crash_id_blob_t b;
    if (crash_id_load(&b)) {
        out->valid      = true;
        out->dump_size  = b.dump_size;
        memcpy(out->fw_version, b.fw_version, sizeof(out->fw_version));
        memcpy(out->app_sha256, b.app_sha256, sizeof(out->app_sha256));
        out->uptime_sec    = b.uptime_sec;
        out->min_free_heap = b.min_free_heap;
        out->epoch_ts      = b.epoch_ts;
    } else {
        // No identity (dump predates this feature, or capture was skipped).
        // Still expose the dump's own SHA, which needs no stored metadata.
        out->dump_size = (uint32_t)sz;
        coredump_summary_lite_t s;
        if (coredump_get_summary(&s) && s.valid) {
            strncpy(out->app_sha256, s.app_sha256, COREDUMP_SHA_SHORT_LEN - 1);
        }
    }

    out->belongs_to_running_image =
        running_sha256_short && running_sha256_short[0] &&
        out->app_sha256[0] &&
        strncmp(out->app_sha256, running_sha256_short,
                COREDUMP_SHA_SHORT_LEN - 1) == 0;
    return true;
}
