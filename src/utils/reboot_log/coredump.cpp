#include "coredump.h"

#include <string.h>
#include <stdio.h>
#include <esp_err.h>
#include <esp_core_dump.h>
#include <esp_partition.h>

#include "../logger/logger.h"

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
    LOG_I("[coredump] erased");
    return true;
}
