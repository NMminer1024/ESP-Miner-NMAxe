#include "nvs_config.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <string.h>
#include "global.h"
#include "logger.h"
#include "helper.h"
#include "display.h"

char * nvs_config_get_string(const char * key, const char * default_value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return strdup(default_value);
    }

    size_t size = 0;
    err = nvs_get_str(handle, key, NULL, &size);

    if (err != ESP_OK) {
        return strdup(default_value);
    }

    char * out = (char *)malloc(size);
    err = nvs_get_str(handle, key, out, &size);

    if (err != ESP_OK) {
        free(out);
        return strdup(default_value);
    }

    nvs_close(handle);
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
        return;
    }

    nvs_close(handle);
    return;
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

void clear_g_board(void){
    esp_err_t err;
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        LOG_W("NVS partition is full or has invalid version, erasing...");
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            LOG_E("Failed to erase NVS partition: %s", esp_err_to_name(err));
            return;
        }
        err = nvs_flash_init();
        if (err != ESP_OK) {
            LOG_E("Failed to initialize NVS after erase: %s", esp_err_to_name(err));
            return;
        }
    } else if (err != ESP_OK) {
        LOG_E("Failed to initialize NVS: %s", esp_err_to_name(err));
        return;
    }
    err = nvs_flash_erase();
    if (err != ESP_OK) {
        LOG_E("Failed to erase NVS partition: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_flash_init();
    if (err != ESP_OK) {
        LOG_E("Failed to initialize NVS after erase: %s", esp_err_to_name(err));
        return;
    }
    LOG_I("NVS partition erased and reinitialized successfully");
}