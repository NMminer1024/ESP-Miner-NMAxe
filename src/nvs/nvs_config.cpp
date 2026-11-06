#include "nvs_config.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <string.h>
#include "utils/logger/logger.h"
#include "utils/helper.h"
#include "version.h"   // BOARD_CURRENT_FW_VERSION — eviction prefers dropping other builds' rows

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

esp_err_t nvs_config_set_string(const char * key, const char * value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        LOG_E("NVS open failed for key '%s': %s", key, esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, key, value);
    nvs_close(handle);
    if (err != ESP_OK) {
        // Loud on purpose: this used to be silent, so a full/fragmented NVS
        // looked exactly like a successful save (web said "Saved", reboot kept
        // the old value).
        LOG_E("NVS write FAILED for key '%s' (value len=%u): %s (0x%x)",
              key, (unsigned)(value ? strlen(value) : 0),
              esp_err_to_name(err), (unsigned)err);
    }
    return err;
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
    // Close on BOTH paths. Returning early here leaked a handle every time a
    // write failed, and the failure we now surface (ESP_ERR_NVS_NOT_ENOUGH_SPACE)
    // is exactly the one that repeats, so the leak would compound.
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
    // Close on both paths: see nvs_config_set_u8().
    nvs_close(handle);
    return;
}

// Like nvs_config_set_u16 but returns the NVS error so a caller can react.
// The benchmark result ring stores its head/count as u16 keys, and a silent
// failure there leaves the ring metadata disagreeing with the slots that were
// actually written - the rows exist but are unreachable, or the reader walks
// stale indices. Those two keys are therefore the one place that must not use
// the fire-and-forget setter.
esp_err_t nvs_config_try_set_u16(const char * key, const uint16_t value)
{
    nvs_handle handle;
    esp_err_t err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u16(handle, key, value);
    if (err == ESP_OK) nvs_commit(handle);
    nvs_close(handle);
    return err;
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
    // Close on both paths: see nvs_config_set_u8().
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
    nvs_close(handle);

    if (err != ESP_OK) {
        return default_value;
    }

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
    // Close on both paths: see nvs_config_set_u8().
    nvs_close(handle);
    return;
}

esp_err_t nvs_config_try_set_u64(const char * key, const uint64_t value)
{
    nvs_handle handle;
    esp_err_t err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u64(handle, key, value);
    if (err == ESP_OK) nvs_commit(handle);
    nvs_close(handle);
    return err;
}


bool nvs_config_delete_key(const char * key)
{
    nvs_handle handle;
    esp_err_t err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        LOG_E("NVS open failed while deleting '%s': %s", key, esp_err_to_name(err));
        return false;
    }

    err = nvs_erase_key(handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return false;   // already absent - not an error worth logging
    }
    if (err != ESP_OK) {
        // Erasing is an append of a tombstone entry, so a full partition can
        // refuse to delete. Silence here is how "clear results" reported success
        // while every row stayed in NVS consuming space.
        LOG_E("NVS ERASE FAILED for key '%s': %s (0x%x)", key, esp_err_to_name(err), (unsigned)err);
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

// How many stored rows one append may sacrifice before it gives up.
//
// Eviction is meant to converge in one or two steps: dropping the head frees
// exactly the slot the next write targets. Needing more means the entry does not
// fit at all (fragmentation, or a value larger than the partition can hold), and
// continuing to delete rows would trade the user's whole history for a result
// that never gets saved. Four is deliberately small: it still absorbs the
// "middle hole too small" case (which then falls back to head eviction) while
// keeping the worst case bounded.
static constexpr uint8_t BM_APPEND_MAX_DROPS = 4;

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

// Write the ring's head/count, reporting a failure instead of swallowing it.
//
// These two u16 keys are the reader's ONLY description of the ring. If a write
// silently fails (the old nvs_config_set_u16 returned void and discarded the
// error), the slots and the metadata diverge: rows get written but stay
// unreachable, or the reader walks an index range that re-reads the same slot.
// Both look like "my results vanished" or "the table wobbles", with nothing in
// the log - the exact failure mode this ring already caused once.
//
// A failed metadata write cannot be repaired here (there is no transaction), so
// the contract is: keep the write that succeeded visible and make the mismatch
// loud enough to diagnose.
static void bm_r_write_meta(uint16_t start, uint16_t cnt, const char* what) {
    esp_err_t e1 = nvs_config_try_set_u16(NVS_CONFIG_BM_R_COUNT, cnt);
    esp_err_t e2 = nvs_config_try_set_u16(NVS_CONFIG_BM_R_START, start);
    if (e1 != ESP_OK || e2 != ESP_OK) {
        LOG_E("[BM] Ring metadata write FAILED while %s (start=%u cnt=%u): count=%s start=%s. "
              "Stored results and the index may now disagree; clear the results to recover.",
              what, (unsigned)start, (unsigned)cnt,
              esp_err_to_name(e1), esp_err_to_name(e2));
    }
}

// Remove the entry at logical position `idx` (0 = oldest) and compact the ring by
// shifting everything after it one slot toward the head. Shared by the duplicate
// trim in the append path and by bm_result_delete_freq().
//
// Compaction MUST be space-neutral, because this runs precisely when NVS is out
// of room (that is why a row is being removed at all). The original version
// shifted every entry BEFORE freeing anything, so each copy needed a brand-new
// NVS entry while the source was still occupying its own - and it wrote through
// nvs_config_set_string(), which discards the error. On a full partition those
// copies therefore failed SILENTLY: on Nexus (2026-11-06) deleting the 775MHz row
// also vaporised an unrelated 565MHz row, with nothing in the log to say so.
//
// Order is now: free the victim's slot, then move each entry with
// write-then-delete-source, so the ring never needs more than the single hole we
// just created. A failed copy keeps its source (no data loss) and is reported.
static void bm_r_remove_at(uint16_t start, uint16_t& cnt, uint16_t idx) {
    // 1. Free the victim's own slot first: that is the working room for the shift.
    char key[12];
    bm_r_key(key, sizeof(key), (start + idx) % BM_RESULT_MAX_ENTRIES);
    nvs_config_delete_key(key);

    // 2. Pull every later entry one slot toward the head.
    bool moved_all = true;
    for (uint16_t j = idx + 1; j < cnt; j++) {
        char dst[12], src[12];
        bm_r_key(dst, sizeof(dst), (start + j - 1) % BM_RESULT_MAX_ENTRIES);
        bm_r_key(src, sizeof(src), (start + j) % BM_RESULT_MAX_ENTRIES);
        char* mv = nvs_config_get_string(src, "");
        if (!mv) continue;
        if (strlen(mv) == 0) { free(mv); continue; }   // already-empty slot
        esp_err_t e = nvs_config_try_set_string(dst, mv);
        free(mv);
        if (e == ESP_OK) {
            nvs_config_delete_key(src);   // source is now duplicated in dst
        } else {
            // Keep the source: the entry still exists exactly once, one slot
            // further along than intended. Loud, because the next read will
            // show a hole where the caller expects a compacted ring.
            moved_all = false;
            LOG_E("[BM] Ring compaction at slot %u failed (%s) — entry kept in place; "
                  "the stored rows are intact but the ring is not compacted.",
                  (unsigned)j, esp_err_to_name(e));
        }
    }

    // 3. The tail slot is normally empty by now (its source was deleted after a
    //    successful copy). Only clear it when the whole shift went through: if a
    //    copy failed, the tail can still be the ONLY copy of that row, and
    //    deleting it here would be exactly the silent data loss this function
    //    was rewritten to stop.
    if (moved_all) {
        bm_r_key(key, sizeof(key), (start + cnt - 1) % BM_RESULT_MAX_ENTRIES);
        nvs_config_delete_key(key);
    } else {
        LOG_E("[BM] Ring left uncompacted after a failed shift; %u slot(s) still hold data "
              "beyond the recorded count. Download the results before clearing them.",
              (unsigned)(cnt > 0 ? cnt : 0));
    }
    cnt--;
    bm_r_write_meta(start, cnt, "removing an entry");
}

// Read the "fw" string value of an entry. Returns false when the tag is absent
// (rows written before the per-row firmware stamp existed).
static bool bm_r_parse_fw(const char* entry_json, char* out, size_t out_len) {
    static const char kKey[] = "\"fw\":\"";
    const char* p = strstr(entry_json, kKey);
    if (!p) return false;
    p += sizeof(kKey) - 1;
    const char* q = strchr(p, '"');
    if (!q) return false;
    size_t n = (size_t)(q - p);
    if (n >= out_len) n = out_len - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

// Choose which row to drop when the ring has no room left, and say why.
//
// The historical rule was "drop the oldest", and entries are appended in
// insertion order, so an over-full ring silently ate the lowest frequencies
// first. On Nexus (v3.2.02k) that removed 40 of the 48 stored baseline rows
// (325..715MHz) while keeping 725..795MHz, which reads — correctly — as data
// corruption, and it kept a 795MHz/1230mV over-current setting around for the
// user to apply by hand.
//
// "Oldest" also has to mean oldest by TIMESTAMP, not by ring position. An
// upsert rewrites a row IN PLACE and refreshes its ts while leaving its position
// untouched, so the ring head is often the row that was measured most recently.
// Judged by position, a freshly measured low frequency was the first casualty on
// Nexus (2026-11-06): 585MHz was dropped seconds after being measured while every
// v3.2.02k-era row above it survived — the opposite of "drop the least useful".
//
// Preference order, cheapest-to-lose first:
//   1. FAILED tombstone (fail=1)      — its only job is invalidating a frequency
//   2. no measured hashrate           — aborted / half-measured round
//   3. another build's row            — no "fw" tag, or a tag that is not this
//                                       build: its safety verdict was computed
//                                       by different code, so it cannot be
//                                       trusted or compared here. This also
//                                       drains the legacy backlog left by an
//                                       upgrade before any current row is lost.
//   4. the OLDEST ts                  — nothing else distinguishes them; ts is
//                                       refreshed on every upsert, so this is the
//                                       row whose MEASUREMENT is oldest.
static uint16_t bm_r_pick_victim(uint16_t start, uint16_t cnt, const char** why) {
    static const char* kWhy[3] = {
        "failed row",
        "row without a measured hashrate",
        "row from another firmware build",
    };
    // Within every class the OLDEST MEASUREMENT wins, not the first in ring
    // order: an upsert rewrites a row in place and refreshes its ts, so ring
    // position says when a frequency was first stored, not when it was measured.
    int16_t best[3]    = {-1, -1, -1};
    long    best_ts[3] = {0, 0, 0};
    int16_t oldest_i   = -1;
    long    oldest_ts  = 0;
    for (uint16_t i = 0; i < cnt; i++) {
        char key[12];
        bm_r_key(key, sizeof(key), (start + i) % BM_RESULT_MAX_ENTRIES);
        char* e = nvs_config_get_string(key, "");
        if (!e) continue;
        if (strlen(e) > 0) {
            // A row with no/invalid ts (ts < 0) sorts first: an untimestamped row
            // cannot be shown to be recent. Such a row is also the cheapest to
            // lose, so treating it as "oldest" is right in both readings.
            const long ts = bm_r_json_int(e, "\"ts\":");
            int cls = -1;
            if (bm_r_json_int(e, "\"fail\":") == 1) {
                cls = 0;
            } else if (bm_r_json_double(e, "\"avgHR\":") <= 0.0) {
                cls = 1;
            } else {
                char fw[24] = "";
                if (!bm_r_parse_fw(e, fw, sizeof(fw)) ||
                    strcmp(fw, BOARD_CURRENT_FW_VERSION) != 0) {
                    cls = 2;
                }
            }
            if (cls >= 0 && (best[cls] < 0 || ts < best_ts[cls])) {
                best[cls]    = (int16_t)i;
                best_ts[cls] = ts;
            }
            if (oldest_i < 0 || ts < oldest_ts) {
                oldest_i  = (int16_t)i;
                oldest_ts = ts;
            }
        }
        free(e);
    }
    for (int c = 0; c < 3; c++) {
        if (best[c] >= 0) { *why = kWhy[c]; return (uint16_t)best[c]; }
    }
    if (oldest_i < 0) {
        *why = "oldest entry";
        return 0;
    }
    *why = "oldest measurement";
    return (uint16_t)oldest_i;
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
                    // The new entry is longer than the stored one (the result now
                    // carries peak currents, per-phase data, fan duties and the fw
                    // tag), so NVS can refuse the in-place write while the OLD row
                    // still occupies its space. Falling straight through to the
                    // append path left that row alive and added a second one: TWO
                    // rows for the same frequency (Nexus, after the ring reached
                    // capacity: 725MHz and 775MHz each kept a factory row next to
                    // the fresh row). Free our own slot and retry in place first —
                    // one row per frequency is the ring's invariant.
                    LOG_W("[BM] In-place overwrite of freq=%u failed (%s) — freeing the old row and retrying.",
                          nf, esp_err_to_name(err));
                    nvs_config_delete_key(key);
                    err = nvs_config_try_set_string(key, entry_json);
                    if (err == ESP_OK) {
                        replaced = true;
                    } else {
                        // Not enough room even after freeing this row: fall back to
                        // append (which evicts). The slot stays empty; bm_result_read_all()
                        // skips empty slots, so the table stays correct — it just runs
                        // with one slot less until the results are cleared.
                        LOG_W("[BM] Retry for freq=%u still failed (%s) — appending instead.",
                              nf, esp_err_to_name(err));
                        break;
                    }
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

    uint8_t drops = 0;      // rows sacrificed in this call (bounded, see below)
    bool evict_head_next = false;   // set when a freed middle slot could not be refilled
    for (uint8_t attempt = 0; attempt <= BM_RESULT_MAX_ENTRIES; attempt++) {
        uint16_t pos = (cnt < BM_RESULT_MAX_ENTRIES) ? (start + cnt) % BM_RESULT_MAX_ENTRIES : start;
        char key[12];
        bm_r_key(key, sizeof(key), pos);
        err = nvs_config_try_set_string(key, entry_json);
        if (err == ESP_OK) {
            // The slot is written; the metadata has to follow, or the row just
            // written is invisible to the reader (or the ring re-reads a stale
            // slot). bm_r_write_meta logs loudly if it cannot.
            if (cnt < BM_RESULT_MAX_ENTRIES) {
                cnt++;
                bm_r_write_meta(start, cnt, "appending a result");
            } else {
                uint16_t ns = (uint16_t)((start + 1) % BM_RESULT_MAX_ENTRIES);
                bm_r_write_meta(ns, cnt, "advancing the ring head");
            }
            return ESP_OK;
        }
        if (err != ESP_ERR_NVS_NOT_ENOUGH_SPACE && err != ESP_ERR_NVS_VALUE_TOO_LONG) break;
        if (cnt == 0) break;

        // Bounded give-up. Each iteration below DELETES a stored row, so an
        // unbounded loop trades the whole history for one result that may never
        // fit (fragmentation, or an entry too large for the partition). Real
        // eviction converges after one or two drops; needing more than this means
        // something else is wrong and the honest answer is "not saved".
        if (drops >= BM_APPEND_MAX_DROPS) {
            LOG_E("[BM] Gave up after dropping %u row(s): the new %u-byte result still does not fit "
                  "in the results partition. Nothing further was discarded — clear old results "
                  "or free NVS space to recover.", (unsigned)drops, (unsigned)strlen(entry_json));
            err = ESP_ERR_NVS_NOT_ENOUGH_SPACE;
            break;
        }

        // No room: drop the least useful row (see bm_r_pick_victim). Evicting MUST
        // free the physical key as well - the old code only advanced the ring head,
        // so the loop could walk the ring down to zero entries and still fail while
        // the log claimed entries had been evicted.
        const char* why = "";
        const uint16_t idx = evict_head_next ? 0 : bm_r_pick_victim(start, cnt, &why);
        if (evict_head_next) why = "oldest entry (previous hole was too small)";
        evict_head_next = false;
        const uint16_t vpos = (start + idx) % BM_RESULT_MAX_ENTRIES;
        char vkey[12];
        bm_r_key(vkey, sizeof(vkey), vpos);
        uint16_t vfreq = 0;
        char* vold = nvs_config_get_string(vkey, "");
        const bool have_vf = vold && bm_r_parse_freq(vold, vfreq);
        free(vold);
        nvs_config_delete_key(vkey);
        drops++;
        if (idx == 0) {
            start = (start + 1) % BM_RESULT_MAX_ENTRIES;
            cnt--;
            bm_r_write_meta(start, cnt, "evicting the oldest row");
        }
        char vdesc[32];
        if (have_vf) snprintf(vdesc, sizeof(vdesc), "%uMHz", (unsigned)vfreq);
        else         snprintf(vdesc, sizeof(vdesc), "an unreadable row");
        LOG_W("[BM] NVS full: dropped %s (%s, %u entries left). The results partition is 24KB, "
              "so only a limited number of rows fit — download the table if you need the full history.",
              vdesc, why, (unsigned)cnt);
        if (idx != 0) {
            // Reuse the freed slot: the row is replaced in place, so the ring neither
            // shrinks nor has to shift every later entry (a shift needs free space,
            // which is exactly what is missing).
            if (nvs_config_try_set_string(vkey, entry_json) == ESP_OK) return ESP_OK;

            // The hole could not hold the NEW entry. That is the normal case when
            // upgrading: a row written by an older build is ~170 bytes while the
            // current one is ~400, so a hole sized for the old row can never fit
            // the new one. The slot stays empty and is still counted — harmless
            // (bm_result_read_all skips empty strings), but retrying middle
            // victims at random would drop several rows before converging.
            //
            // Head eviction is the one path that ALWAYS frees the exact slot the
            // next write targets: pos = (start+cnt) mod MAX stays the same when
            // both start and cnt move by one. So ask for it explicitly.
            LOG_W("[BM] Freed slot for %s is too small for the new entry (%u bytes) — "
                  "next eviction will come from the ring head.",
                  vdesc, (unsigned)strlen(entry_json));
            evict_head_next = true;
        }
        err = ESP_ERR_NVS_NOT_ENOUGH_SPACE;
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

// Replace a bare `nan` / `inf` JSON value with `null`.
//
// Firmware up to v3.2.02k could store a non-finite temperature
// ("avgVcoreTemp":nan) straight from a failed telemetry read. Bare nan/inf are
// NOT valid JSON, and one such row makes the whole array unparseable — which
// took out the benchmark page AND the firmware's own soak/finish deserialization.
// Results outlive an upgrade, so a repaired build must clean what the previous
// one wrote instead of trusting it; otherwise the table stays broken until the
// offending frequency is re-measured or the results are cleared.
//
// Entries are flat objects with numeric values plus the "fw" string, and ":nan"
// can only appear as a value, so a simple value-position scan is safe.
static void bm_r_sanitize_json(String& j) {
    String out;
    out.reserve(j.length() + 8);
    const int n = (int)j.length();
    for (int i = 0; i < n; i++) {
        if (j[i] != ':') { out += j[i]; continue; }
        int k = i + 1;
        if (k < n && j[k] == '-') k++;
        if (k + 2 < n) {
            const char a = j[k], b = j[k + 1], c = j[k + 2];
            const bool is_nan = (a == 'n' && b == 'a' && c == 'n');
            const bool is_inf = (a == 'i' && b == 'n' && c == 'f');
            if (is_nan || is_inf) {
                out += ":null";
                i = k + 2;      // skip the malformed literal
                continue;
            }
        }
        out += j[i];
    }
    j = out;
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
                String ent(e);
                bm_r_sanitize_json(ent);
                out += ent;
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

// Mark a stored result as having passed the soak check, recording the duty each
// fan settled at. Rewrites the entry in place (results are keyed by frequency),
// so the sweep's measurements are preserved and only the verification fields are
// added.
//
// fan_need_vcore < 0 means the board has no second (VRM) fan, so the field is
// omitted rather than written as 0 — a 0 % reading would claim the point needs
// no VRM cooling at all.
esp_err_t bm_result_mark_soak(uint16_t freq, uint16_t fan_need, int fan_need_vcore) {
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

        // Overwrite, never append.
        //
        // Soak is a mandatory stage of every benchmark run, not a cache: the same
        // point is re-soaked whenever a sweep finishes with it in the top
        // candidates, and there is no "already verified, skip it" shortcut. This
        // function therefore has to be idempotent. It used to insert the fields
        // unconditionally, so soaking a point twice produced a duplicate key —
        //   ...,"soak":1,"fanNeed":60,"soak":1,"fanNeed":38,"ts":...
        // — which is ambiguous for every reader (ArduinoJson keeps one of them,
        // and which one is unspecified), grew the entry on each run, and made the
        // fanNeed shown in the UI a coin toss between this run's value and a
        // stale one.
        //
        // Drop every existing soak segment first: cut the prefix at the FIRST
        // occurrence, which collapses any number of accumulated duplicates (they
        // are all inserted at the same spot, immediately before "ts").
        size_t prefix = (size_t)(ts - e);
        const char* first = strstr(e, ",\"soak\":1");
        if (first != nullptr && first < ts) {
            const size_t cut = (size_t)(first - e);
            if (cut < prefix) prefix = cut;
        }

        char* out = (char*)malloc(prefix + 96 + strlen(ts) + 1);
        if (!out) { free(e); return ESP_ERR_NO_MEM; }
        memcpy(out, e, prefix);
        int n = snprintf(out + prefix, 96, ",\"soak\":1,\"fanNeed\":%u", fan_need);
        if (fan_need_vcore >= 0) {
            n += snprintf(out + prefix + n, 96 - n, ",\"fanNeedVcore\":%d", fan_need_vcore);
        }
        strcpy(out + prefix + n, ts);

        esp_err_t err = nvs_config_try_set_string(key, out);
        free(out);
        free(e);
        if (err != ESP_OK) return err;
        s_soak_marks_may_exist = true;
        if (fan_need_vcore >= 0) {
            LOG_W("[BM] freq=%u marked as soak-verified (fanNeed=%u%% ASIC / %d%% VRM)",
                  freq, fan_need, fan_need_vcore);
        } else {
            LOG_W("[BM] freq=%u marked as soak-verified (fanNeed=%u%%)", freq, fan_need);
        }
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

// Empty every result slot, reporting slots that could NOT be erased.
//
// Erasing a key is a WRITE in NVS: it appends a tombstone entry, so a partition
// that is nearly full can refuse to delete. The old void version ignored that
// return value, which is how a board ended up with 66 result rows still in NVS
// after a "clear" that reported success - and those rows then blocked every
// subsequent config save with ESP_ERR_NVS_NOT_ENOUGH_SPACE.
//
// The ring metadata is only dropped when every slot is gone: keeping start/count
// while slots survive would keep the ring readable (and retryable) instead of
// silently turning the leftovers into unreachable orphans.
uint16_t bm_result_clear(void) {
    uint16_t failed = 0;
    for (uint16_t i = 0; i < BM_RESULT_MAX_ENTRIES; i++) {
        char key[12];
        bm_r_key(key, sizeof(key), i);
        char* e = nvs_config_get_string(key, "");
        const bool present = e && e[0] != '\0';
        free(e);
        if (!present) continue;
        if (!nvs_config_delete_key(key)) {
            failed++;
            LOG_E("[BM] Clear results: could not erase %s - NVS needs a free entry to delete. "
                  "It still occupies space; free some space and retry.", key);
        }
    }
    if (failed == 0) {
        nvs_config_delete_key(NVS_CONFIG_BM_R_START);
        nvs_config_delete_key(NVS_CONFIG_BM_R_COUNT);
        nvs_config_delete_key(NVS_CONFIG_BM_RESULT);  // legacy
        s_soak_marks_may_exist = false;
    } else {
        LOG_E("[BM] Clear results INCOMPLETE: %u row(s) remain in NVS", (unsigned)failed);
    }
    return failed;
}

// Delete result slots that the ring window does not describe.
//
// A slot outside [start, start+cnt) is unreachable: GET /api/benchmark and every
// firmware consumer walk the ring by index, so those rows cannot be shown,
// downloaded or applied - they are pure NVS cost. Measured on a customer board
// (v3.2.02m, 24 KB partition): 66 such rows held ~386 of 598 used entries, and
// their presence was enough to make every config write fail with
// ESP_ERR_NVS_NOT_ENOUGH_SPACE even though the UI reported no results at all.
//
// Safe by construction: the live window is computed exactly the way
// bm_result_read_all() computes it, so a row that the reader can return is never
// touched here - and if the metadata is unusable, nothing is swept at all.
uint16_t bm_result_sweep_orphans(void) {
    // Read the ring metadata RAW so "key absent" can be told apart from "key == 0".
    // The fallback-getter would return 0 for a missing key, silently turning a
    // corrupt/lost index into "window starts at slot 0, count 0" and making every
    // stored row look unreachable.
    uint16_t start = 0, cnt = 0;
    bool     have_start = false, have_cnt = false;
    nvs_handle h_meta;
    if (nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &h_meta) == ESP_OK) {
        have_start = (nvs_get_u16(h_meta, NVS_CONFIG_BM_R_START, &start) == ESP_OK);
        have_cnt   = (nvs_get_u16(h_meta, NVS_CONFIG_BM_R_COUNT, &cnt)   == ESP_OK);
        nvs_close(h_meta);
    }
    if (!have_start && !have_cnt) {
        start = 0; cnt = 0;              // ring never used / already cleared: every slot is garbage
    } else if (!(have_start && have_cnt)) {
        LOG_W("[BM] Orphan sweep skipped: ring metadata is incomplete (start=%d cnt=%d). "
              "Guessing the window could delete reachable results.", (int)have_start, (int)have_cnt);
        return 0;
    } else if (start >= BM_RESULT_MAX_ENTRIES || cnt > BM_RESULT_MAX_ENTRIES) {
        LOG_W("[BM] Orphan sweep skipped: ring metadata out of range (start=%u cnt=%u).",
              (unsigned)start, (unsigned)cnt);
        return 0;
    }
    uint16_t freed = 0;

    for (uint16_t i = 0; i < BM_RESULT_MAX_ENTRIES; i++) {
        const uint16_t off  = (uint16_t)((i + BM_RESULT_MAX_ENTRIES - start) % BM_RESULT_MAX_ENTRIES);
        if (cnt > 0 && off < cnt) continue;   // reachable row - keep it

        char key[12];
        bm_r_key(key, sizeof(key), i);
        char* e = nvs_config_get_string(key, "");
        const bool present = e && e[0] != '\0';
        uint16_t   freq    = 0;
        const bool have_f  = present && bm_r_parse_freq(e, freq);
        free(e);
        if (!present) continue;

        if (nvs_config_delete_key(key)) {
            freed++;
            if (have_f) LOG_W("[BM] Reclaimed unreachable result slot %s (freq=%u MHz) - freed NVS space",
                              key, (unsigned)freq);
            else        LOG_W("[BM] Reclaimed unreachable result slot %s", key);
        } else {
            LOG_E("[BM] Could not reclaim unreachable slot %s - NVS is too full to even erase", key);
        }
    }
    if (freed) {
        LOG_W("[BM] Orphan sweep freed %u result slot(s) that no ring index pointed at", (unsigned)freed);
    }
    return freed;
}
