#include "nvs_config.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <string.h>
#include "utils/logger/logger.h"
#include "utils/helper.h"

char * nvs_config_get_string(const char * key, const char * default_value)
{
    nvs_handle handle;
    esp_err_t err;
    const char *fallback = default_value ? default_value : "";
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return strdup(fallback);
    }

    size_t size = 0;
    err = nvs_get_str(handle, key, NULL, &size);

    if (err != ESP_OK) {
        nvs_close(handle);
        return strdup(fallback);
    }

    char * out = (char *)malloc(size);
    if (!out) {
        nvs_close(handle);
        return strdup(fallback);
    }
    err = nvs_get_str(handle, key, out, &size);

    if (err != ESP_OK) {
        free(out);
        nvs_close(handle);
        return strdup(fallback);
    }

    nvs_close(handle);
    return out;
}

String nvs_config_get_string_value(const char * key, const char * default_value)
{
    char *value = nvs_config_get_string(key, default_value);
    String out = value ? String(value) : String(default_value ? default_value : "");
    if (value) free(value);
    return out;
}

void nvs_config_set_string(const char * key, const char * value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return;
    }

    err = nvs_set_str(handle, key, value);
    if (err != ESP_OK) {
        nvs_close(handle);
        return;
    }

    nvs_close(handle);
    return;
}

// Like nvs_config_set_string but returns the raw NVS error code so the caller
// can detect ESP_ERR_NVS_NOT_ENOUGH_SPACE and evict data before retrying.
esp_err_t nvs_config_try_set_string(const char * key, const char * value)
{
    nvs_handle handle;
    esp_err_t err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) nvs_commit(handle);
    nvs_close(handle);
    return err;
}


uint8_t nvs_config_get_u8(const char * key, const uint8_t default_value){
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return default_value;
    }

    uint8_t out;
    err = nvs_get_u8(handle, key, &out);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        return default_value;
    }
    return out;
}

void nvs_config_set_u8(const char * key, const uint8_t value){
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return;
    }

    err = nvs_set_u8(handle, key, value);
    if (err != ESP_OK) {
        return;
    }

    nvs_close(handle);
    return;
}

uint16_t nvs_config_get_u16(const char * key, const uint16_t default_value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return default_value;
    }

    uint16_t out;
    err = nvs_get_u16(handle, key, &out);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        return default_value;
    }
    return out;
}

void nvs_config_set_u16(const char * key, const uint16_t value)
{

    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return;
    }

    err = nvs_set_u16(handle, key, value);
    if (err != ESP_OK) {
        return;
    }

    nvs_close(handle);
    return;
}

uint32_t nvs_config_get_u32(const char * key, const uint32_t default_value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return default_value;
    }

    uint32_t out;
    err = nvs_get_u32(handle, key, &out);
    nvs_close(handle);

    if (err != ESP_OK) {
        return default_value;
    }
    return out;
}

void nvs_config_set_u32(const char * key, const uint32_t value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return;
    }

    err = nvs_set_u32(handle, key, value);
    if (err != ESP_OK) {
        return;
    }
    nvs_close(handle);
    return;
}

uint64_t nvs_config_get_u64(const char * key, const uint64_t default_value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return default_value;
    }

    uint64_t out;
    err = nvs_get_u64(handle, key, &out);

    if (err != ESP_OK) {
        return default_value;
    }

    nvs_close(handle);
    return out;
}

void nvs_config_set_u64(const char * key, const uint64_t value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return;
    }

    err = nvs_set_u64(handle, key, value);
    if (err != ESP_OK) {
        return;
    }
    nvs_close(handle);
    return;
}


bool nvs_config_delete_key(const char * key)
{
    nvs_handle handle;
    esp_err_t err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return false;
    }

    err = nvs_erase_key(handle, key);
    if (err != ESP_OK) {
        nvs_close(handle);
        return false;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

bool erase_all_nvs(void){
    esp_err_t err;
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        LOG_W("NVS partition is full or has invalid version, erasing...");
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            LOG_E("Failed to erase NVS partition: %s", esp_err_to_name(err));
            return false;
        }
        err = nvs_flash_init();
        if (err != ESP_OK) {
            LOG_E("Failed to initialize NVS after erase: %s", esp_err_to_name(err));
            return false;
        }
    } else if (err != ESP_OK) {
        LOG_E("Failed to initialize NVS: %s", esp_err_to_name(err));
        return false;
    }



    err = nvs_flash_erase();
    if (err != ESP_OK) {
        LOG_E("Failed to erase NVS partition: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_flash_init();
    if (err != ESP_OK) {
        LOG_E("Failed to initialize NVS after erase: %s", esp_err_to_name(err));
        return false;
    }
    LOG_I("NVS partition erased and reinitialized successfully");
    return true;
}
// ── Benchmark result ring storage ────────────────────────────────────────────
// Entries are stored as individual keys bm_r_000..bm_r_NNN (a single NVS string
// value cannot span flash pages and caps at ~4KB ≈ 28 entries). Ring state is
// bm_r_start (oldest index) + bm_r_cnt (entry count); appends past the cap
// overwrite the oldest entry — same LRU semantics as the legacy single-string
// eviction. Read side reassembles the identical "[{...},{...}]" JSON string.
//
// Upsert semantics: freq alone is the unique key of an entry. Writing an
// entry whose freq matches an existing one (regardless of vcore) overwrites
// it in place (ring position and count unchanged, ts refreshed) — each
// frequency keeps only its latest result, e.g. 325MHz/1124mV replaces a
// stored 325MHz/1125mV entry.

static void bm_r_key(char* out, size_t out_len, uint16_t idx) {
    snprintf(out, out_len, "bm_r_%03u", idx);
}

// Entries are flat JSON objects; freq is a plain integer, so a light
// strstr+atoi parse is enough (no ArduinoJson needed in this hot path).
static bool bm_r_parse_freq(const char* entry_json, uint16_t& freq) {
    const char* f = strstr(entry_json, "\"freq\":");
    if (!f) return false;
    freq = (uint16_t)atoi(f + 7);
    return true;
}

// Lightweight field readers for a flat result object. A missing key returns -1
// (int) / 0.0 (double) so callers can tell "absent" from a real 0.
static long bm_r_json_int(const char* entry_json, const char* key) {
    const char* p = strstr(entry_json, key);
    if (!p) return -1;
    p += strlen(key);
    while (*p == ' ') p++;
    return atol(p);
}

static double bm_r_json_double(const char* entry_json, const char* key) {
    const char* p = strstr(entry_json, key);
    if (!p) return 0.0;
    p += strlen(key);
    while (*p == ' ') p++;
    return atof(p);
}

// Whether any stored result MAY carry a soak verdict.
//
// bm_result_clear_soak() walks the whole ring (up to 80 NVS string reads). Its
// trigger is the fan-policy PATCH, which runs on the async_tcp task - i.e. on the
// HTTP response path - so paying that cost for a board that was never soaked is
// wasteful and risks a slow response. Remember in RAM that the ring is clean.
// Initialised to true because the ring survives a reboot: the first call after
// boot has to actually look before it can conclude there is nothing to do.
static volatile bool s_soak_marks_may_exist = true;

// Remove the entry at logical position `idx` (0 = oldest) and compact the ring by
// shifting everything after it one slot toward the head. Shared by the duplicate
// trim in the append path and by bm_result_delete_freq().
static void bm_r_remove_at(uint16_t start, uint16_t& cnt, uint16_t idx) {
    for (uint16_t j = idx + 1; j < cnt; j++) {
        char dst[12], src[12];
        bm_r_key(dst, sizeof(dst), (start + j - 1) % BM_RESULT_MAX_ENTRIES);
        bm_r_key(src, sizeof(src), (start + j) % BM_RESULT_MAX_ENTRIES);
        char* mv = nvs_config_get_string(src, "");
        if (mv) { nvs_config_set_string(dst, mv); free(mv); }
    }
    char last[12];
    bm_r_key(last, sizeof(last), (start + cnt - 1) % BM_RESULT_MAX_ENTRIES);
    nvs_config_delete_key(last);
    cnt--;
    nvs_config_set_u16(NVS_CONFIG_BM_R_COUNT, cnt);
}

static esp_err_t bm_result_append_ring(const char* entry_json) {
    uint16_t start = nvs_config_get_u16(NVS_CONFIG_BM_R_START, 0);
    uint16_t cnt   = nvs_config_get_u16(NVS_CONFIG_BM_R_COUNT, 0);
    esp_err_t err  = ESP_OK;

    // Upsert: same freq -> overwrite the first matching slot in place and
    // drop any further same-freq entries (legacy duplicates from pre-freq-key
    // builds), keeping the one-entry-per-frequency invariant.
    uint16_t nf = 0;
    if (bm_r_parse_freq(entry_json, nf)) {
        bool replaced = false;
        for (uint16_t i = 0; i < cnt; i++) {
            uint16_t pos = (start + i) % BM_RESULT_MAX_ENTRIES;
            char key[12];
            bm_r_key(key, sizeof(key), pos);
            char* old = nvs_config_get_string(key, "");
            if (!old) continue;
            uint16_t of = 0;
            bool match = bm_r_parse_freq(old, of) && of == nf;
            free(old);
            if (!match) continue;
            if (!replaced) {
                err = nvs_config_try_set_string(key, entry_json);
                if (err == ESP_OK) {
                    replaced = true;
                    LOG_W("[BM] freq=%u entry exists (any vcore) — overwritten with latest result.", nf);
                } else {
                    // In-place write failed (e.g. NVS full and new value is longer)
                    // — fall through to the append+evict path below.
                    LOG_W("[BM] In-place overwrite failed (%s), falling back to append.", esp_err_to_name(err));
                    break;
                }
            } else {
                // Extra duplicate of the same freq — drop it and compact the
                // ring by shifting every entry after it one slot toward oldest.
                bm_r_remove_at(start, cnt, i);
                LOG_W("[BM] Dropped duplicate freq=%u entry (%u entries remaining).", nf, cnt);
                i--; // positions shifted left; re-check the same index
            }
        }
        if (replaced) return ESP_OK;
    }

    while (true) {
        uint16_t pos = (cnt < BM_RESULT_MAX_ENTRIES) ? (start + cnt) % BM_RESULT_MAX_ENTRIES : start;
        char key[12];
        bm_r_key(key, sizeof(key), pos);
        err = nvs_config_try_set_string(key, entry_json);
        if (err == ESP_OK) {
            if (cnt < BM_RESULT_MAX_ENTRIES) nvs_config_set_u16(NVS_CONFIG_BM_R_COUNT, cnt + 1);
            else                             nvs_config_set_u16(NVS_CONFIG_BM_R_START, (start + 1) % BM_RESULT_MAX_ENTRIES);
            break;
        }
        if ((err == ESP_ERR_NVS_NOT_ENOUGH_SPACE || err == ESP_ERR_NVS_VALUE_TOO_LONG) && cnt > 0) {
            // Evicting MUST free the physical key. This previously only advanced the
            // ring head, leaving every entry in NVS: the loop could walk the ring
            // down to zero entries and still fail with NOT_ENOUGH_SPACE, so the
            // result was silently dropped while the log claimed entries had been
            // evicted. Deleting also gives NVS a hole it can garbage-collect.
            char evict_key[12];
            bm_r_key(evict_key, sizeof(evict_key), start);
            nvs_config_delete_key(evict_key);
            start = (start + 1) % BM_RESULT_MAX_ENTRIES;
            cnt--;
            nvs_config_set_u16(NVS_CONFIG_BM_R_START, start);
            nvs_config_set_u16(NVS_CONFIG_BM_R_COUNT, cnt);
            LOG_W("[BM] NVS full, evicted oldest entry (%u entries remaining).", cnt);
            continue;
        }
        break;
    }
    return err;
}

// One-time migration: split the legacy bm_result string into ring entries.
// Entries are flat JSON objects with no nested braces, so '{'..'}' spans split them.
static void bm_result_migrate_legacy(void) {
    char* legacy = nvs_config_get_string(NVS_CONFIG_BM_RESULT, "");
    if (!legacy) return;
    String s(legacy);
    free(legacy);
    if (s.length() > 2) {
        int pos = 0;
        while (true) {
            int b = s.indexOf('{', pos);
            if (b < 0) break;
            int e = s.indexOf('}', b);
            if (e < 0) break;
            String entry = s.substring(b, e + 1);
            bm_result_append_ring(entry.c_str());
            pos = e + 1;
        }
        LOG_W("[BM] Migrated legacy bm_result into ring storage.");
    }
    nvs_config_delete_key(NVS_CONFIG_BM_RESULT);
}

esp_err_t bm_result_append(const char* entry_json) {
    bm_result_migrate_legacy();
    return bm_result_append_ring(entry_json);
}

char* bm_result_read_all(void) {
    bm_result_migrate_legacy();
    uint16_t start = nvs_config_get_u16(NVS_CONFIG_BM_R_START, 0);
    uint16_t cnt   = nvs_config_get_u16(NVS_CONFIG_BM_R_COUNT, 0);
    String out = "[";
    for (uint16_t i = 0; i < cnt; i++) {
        char key[12];
        bm_r_key(key, sizeof(key), (start + i) % BM_RESULT_MAX_ENTRIES);
        char* e = nvs_config_get_string(key, "");
        if (e) {
            if (strlen(e) > 0) {
                if (out.length() > 1) out += ",";
                out += e;
            }
            free(e);
        }
    }
    out += "]";
    return strdup(out.c_str());
}

void bm_result_restore_all(const char* json_array) {
    if (!json_array) return;
    // Restored entries are arbitrary JSON and may already carry "soak":1, so the
    // RAM shortcut must not assume the ring is clean.
    s_soak_marks_may_exist = true;
    String s(json_array);
    int pos = 0;
    while (true) {
        int b = s.indexOf('{', pos);
        if (b < 0) break;
        int e = s.indexOf('}', b);
        if (e < 0) break;
        String entry = s.substring(b, e + 1);
        bm_result_append_ring(entry.c_str());
        pos = e + 1;
    }
}

// Delete every stored entry for one frequency. Results are keyed by frequency
// alone, so this removes exactly the row the user asked to forget and leaves the
// remaining entries in their original order.
// Returns ESP_ERR_NOT_FOUND when that frequency is not stored.
esp_err_t bm_result_delete_freq(uint16_t freq) {
    bm_result_migrate_legacy();
    uint16_t start = nvs_config_get_u16(NVS_CONFIG_BM_R_START, 0);
    uint16_t cnt   = nvs_config_get_u16(NVS_CONFIG_BM_R_COUNT, 0);

    bool removed = false;
    for (uint16_t i = 0; i < cnt; i++) {
        char key[12];
        bm_r_key(key, sizeof(key), (start + i) % BM_RESULT_MAX_ENTRIES);
        char* e = nvs_config_get_string(key, "");
        if (!e) continue;
        uint16_t of = 0;
        bool match = bm_r_parse_freq(e, of) && of == freq;
        free(e);
        if (!match) continue;
        bm_r_remove_at(start, cnt, i);
        removed = true;
        i--;   // positions shifted left; the next entry is now at this index
    }
    if (!removed) return ESP_ERR_NOT_FOUND;
    LOG_W("[BM] Deleted stored result(s) for freq=%u (%u entries remaining).", freq, cnt);
    return ESP_OK;
}

// Pick a safer operating point from the MEASURED, qualified benchmark results.
//
// Used by the over-temperature step-down. The old behaviour shaved 5 mV off the
// requested Vcore while leaving the frequency alone, which has two problems:
//   - it lands on values that were never tested (1175 -> 1145), and a cube that
//     is only marginal at 1175 mV can start producing HW errors at 1145 mV. The
//     unit then looks "unstable" precisely when the thermal path was trying to
//     help.
//   - nothing records the move, so every reboot walks the same ramp again.
// A benchmark entry, by contrast, is a (freq, vcore) PAIR that was actually run
// and passed the safety margin, so stepping to one is guaranteed to be a
// combination the board has already survived.
//
// "Safer" = neither dimension above the current point AND at least one strictly
// below, so the call can never pick something equal to or more aggressive than
// where we already are. Among those, the entry with the highest measured average
// hashrate wins: the smallest performance loss that still sheds heat.
//
// Returns false when no qualified entry is strictly below the current point (no
// benchmark data, everything is legacy, or we are already at the bottom) - the
// caller must then fall back to its own step.
bool bm_result_pick_safer_point(uint16_t cur_freq, uint16_t cur_vcore,
                                uint16_t* out_freq, uint16_t* out_vcore) {
    if (out_freq == nullptr || out_vcore == nullptr) return false;
    bm_result_migrate_legacy();

    const uint16_t start = nvs_config_get_u16(NVS_CONFIG_BM_R_START, 0);
    const uint16_t cnt   = nvs_config_get_u16(NVS_CONFIG_BM_R_COUNT, 0);

    bool     found  = false;
    uint16_t best_f = 0, best_v = 0;
    double   best_hr = -1.0;

    for (uint16_t i = 0; i < cnt; i++) {
        char key[12];
        bm_r_key(key, sizeof(key), (start + i) % BM_RESULT_MAX_ENTRIES);
        char* e = nvs_config_get_string(key, "");
        if (!e) continue;

        // Only points that finished a real measurement AND passed the margin.
        // A tombstone (fail=1) has avgHR=0 and must never be selected, and a
        // legacy entry has no "q" at all - bm_r_json_int reports -1 for absent,
        // so the == 1 test rejects those without a special case.
        const long q      = bm_r_json_int(e, "\"q\":");
        const long fail   = bm_r_json_int(e, "\"fail\":");
        const long f      = bm_r_json_int(e, "\"freq\":");
        const long v      = bm_r_json_int(e, "\"vcore\":");
        const double hr   = bm_r_json_double(e, "\"avgHR\":");
        free(e);

        if (q != 1 || fail != 0) continue;
        if (f <= 0 || v <= 0 || hr <= 0.0) continue;
        if (f > (long)cur_freq || v > (long)cur_vcore) continue;      // not safer
        if (f == (long)cur_freq && v == (long)cur_vcore) continue;    // current point
        if (hr > best_hr) {
            best_hr = hr;
            best_f  = (uint16_t)f;
            best_v  = (uint16_t)v;
            found   = true;
        }
    }

    if (!found) return false;
    *out_freq  = best_f;
    *out_vcore = best_v;
    return true;
}

// Mark a stored result as having passed the soak check, recording the fan duty
// that held it at a steady temperature. Rewrites the entry in place (results are
// keyed by frequency), so the sweep's measurements are preserved and only the
// verification fields are added.
esp_err_t bm_result_mark_soak(uint16_t freq, uint16_t fan_need) {
    bm_result_migrate_legacy();
    uint16_t start = nvs_config_get_u16(NVS_CONFIG_BM_R_START, 0);
    uint16_t cnt   = nvs_config_get_u16(NVS_CONFIG_BM_R_COUNT, 0);

    for (uint16_t i = 0; i < cnt; i++) {
        char key[12];
        bm_r_key(key, sizeof(key), (start + i) % BM_RESULT_MAX_ENTRIES);
        char* e = nvs_config_get_string(key, "");
        if (!e) continue;

        uint16_t of = 0;
        const bool match = bm_r_parse_freq(e, of) && of == freq;
        if (!match) { free(e); continue; }

        // Splice the new fields in before "ts" so the existing key order and every
        // other value stay untouched.
        char* ts = strstr(e, ",\"ts\":");
        if (ts == nullptr) { free(e); return ESP_ERR_INVALID_STATE; }

        const size_t prefix = (size_t)(ts - e);
        char* out = (char*)malloc(prefix + 64 + strlen(ts) + 1);
        if (!out) { free(e); return ESP_ERR_NO_MEM; }
        memcpy(out, e, prefix);
        int n = snprintf(out + prefix, 64, ",\"soak\":1,\"fanNeed\":%u", fan_need);
        strcpy(out + prefix + n, ts);

        esp_err_t err = nvs_config_try_set_string(key, out);
        free(out);
        free(e);
        if (err != ESP_OK) return err;
        s_soak_marks_may_exist = true;
        LOG_W("[BM] freq=%u marked as soak-verified (fanNeed=%u%%)", freq, fan_need);
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

// Drop every soak verdict. A soak says "this point stays inside its limits under
// THE fan policy that was in force at the time", so changing that policy (auto vs
// manual, target temperature, manual duty) makes all existing verdicts stale. They
// are removed rather than left to be trusted, because trusting one would select a
// point the current cooling cannot sustain — the exact failure the soak prevents.
void bm_result_clear_soak(void) {
    if (!s_soak_marks_may_exist) return;   // nothing to clear, skip the ring walk
    bm_result_migrate_legacy();
    uint16_t start = nvs_config_get_u16(NVS_CONFIG_BM_R_START, 0);
    uint16_t cnt   = nvs_config_get_u16(NVS_CONFIG_BM_R_COUNT, 0);
    uint16_t cleared = 0;

    for (uint16_t i = 0; i < cnt; i++) {
        char key[12];
        bm_r_key(key, sizeof(key), (start + i) % BM_RESULT_MAX_ENTRIES);
        char* e = nvs_config_get_string(key, "");
        if (!e) continue;

        char* s = strstr(e, ",\"soak\":1");
        if (s != nullptr) {
            char* t = strstr(s + 1, ",\"ts\":");
            if (t != nullptr) {
                memmove(s, t, strlen(t) + 1);
                nvs_config_set_string(key, e);
                cleared++;
            }
        }
        free(e);
    }
    s_soak_marks_may_exist = false;
    if (cleared > 0) {
        LOG_W("[BM] %u soak verdict(s) discarded - they were measured under the previous fan policy",
              (unsigned)cleared);
    }
}

void bm_result_clear(void) {
    for (uint16_t i = 0; i < BM_RESULT_MAX_ENTRIES; i++) {
        char key[12];
        bm_r_key(key, sizeof(key), i);
        nvs_config_delete_key(key);
    }
    nvs_config_delete_key(NVS_CONFIG_BM_R_START);
    nvs_config_delete_key(NVS_CONFIG_BM_R_COUNT);
    nvs_config_delete_key(NVS_CONFIG_BM_RESULT);  // legacy
    s_soak_marks_may_exist = false;
}
