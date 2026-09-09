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

static void bm_r_key(char* out, size_t out_len, uint16_t idx) {
    snprintf(out, out_len, "bm_r_%03u", idx);
}

static esp_err_t bm_result_append_ring(const char* entry_json) {
    uint16_t start = nvs_config_get_u16(NVS_CONFIG_BM_R_START, 0);
    uint16_t cnt   = nvs_config_get_u16(NVS_CONFIG_BM_R_COUNT, 0);
    esp_err_t err  = ESP_OK;
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
            // Partition full despite the ring cap (other keys grew) — evict oldest and retry
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

void bm_result_clear(void) {
    for (uint16_t i = 0; i < BM_RESULT_MAX_ENTRIES; i++) {
        char key[12];
        bm_r_key(key, sizeof(key), i);
        nvs_config_delete_key(key);
    }
    nvs_config_delete_key(NVS_CONFIG_BM_R_START);
    nvs_config_delete_key(NVS_CONFIG_BM_R_COUNT);
    nvs_config_delete_key(NVS_CONFIG_BM_RESULT);  // legacy
}
