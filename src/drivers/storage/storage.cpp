// What: ESP-NVS storage implementation shared by config and future state stores.
// Why: The framework needs one place to own NVS flash init, namespace handles,
// and typed read/write helpers instead of scattering that logic across services.
// Role: Provides a small typed wrapper over `nvs_open/get/set/commit`.
// Benefit: Higher layers stay focused on domain values while this driver keeps
// NVS locking and error handling consistent across the project.
#include "drivers/storage/storage.h"

#include <freertos/semphr.h>
#include <nvs_flash.h>
#include <vector>

namespace nm::drivers::storage {

namespace {

SemaphoreHandle_t g_nvs_mutex = nullptr;

SemaphoreHandle_t storage_mutex() {
    if (g_nvs_mutex == nullptr) {
        g_nvs_mutex = xSemaphoreCreateRecursiveMutex();
    }
    return g_nvs_mutex;
}

template <typename T>
T read_value(
    nvs_handle_t handle,
    const char* key,
    T default_value,
    esp_err_t (*getter)(nvs_handle_t, const char*, T*)) {
    if (handle == 0 || key == nullptr) {
        return default_value;
    }

    T value{};
    return getter(handle, key, &value) == ESP_OK ? value : default_value;
}

bool write_denied(const char* namespace_name) {
    Serial.printf("[storage] namespace '%s' is not writable\n",
                  namespace_name != nullptr ? namespace_name : "unknown");
    return false;
}

template <typename T>
bool write_value(
    nvs_handle_t handle,
    const char* namespace_name,
    bool read_write,
    bool& dirty,
    const char* key,
    T value,
    esp_err_t (*setter)(nvs_handle_t, const char*, T)) {
    if (!read_write || handle == 0 || key == nullptr) {
        return write_denied(namespace_name);
    }

    const esp_err_t err = setter(handle, key, value);
    if (err != ESP_OK) {
        Serial.printf("[storage] set '%s/%s' failed: %s\n",
                      namespace_name != nullptr ? namespace_name : "unknown",
                      key,
                      esp_err_to_name(err));
        return false;
    }

    dirty = true;
    return true;
}

}  // namespace

bool init_flash() {
    static bool initialized = false;
    static bool init_ok = false;

    if (initialized) {
        return init_ok;
    }

    const esp_err_t first_try = nvs_flash_init();
    esp_err_t err = first_try;

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        Serial.printf("[storage] nvs init error '%s', erasing partition\n", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }

    if (err != ESP_OK) {
        Serial.printf("[storage] nvs init failed: %s\n", esp_err_to_name(err));
        init_ok = false;
    } else {
        init_ok = true;
    }

    initialized = true;
    return init_ok;
}

Storage::Storage(const char* namespace_name, bool read_write)
    : _namespace_name(namespace_name), _read_write(read_write) {
    xSemaphoreTakeRecursive(storage_mutex(), portMAX_DELAY);

    if (!init_flash()) {
        return;
    }

    const esp_err_t err = nvs_open(
        _namespace_name,
        _read_write ? NVS_READWRITE : NVS_READONLY,
        &_handle);
    if (err != ESP_OK) {
        Serial.printf("[storage] nvs_open('%s') failed: %s\n",
                      _namespace_name != nullptr ? _namespace_name : "unknown",
                      esp_err_to_name(err));
        _handle = 0;
    }
}

Storage::~Storage() {
    if (_handle != 0) {
        if (_read_write && _dirty) {
            commit();
        }
        nvs_close(_handle);
        _handle = 0;
    }

    xSemaphoreGiveRecursive(storage_mutex());
}

String Storage::get_string(const char* key, const String& default_value) const {
    if (_handle == 0 || key == nullptr) {
        return default_value;
    }

    size_t length = 0;
    if (nvs_get_str(_handle, key, nullptr, &length) != ESP_OK || length == 0) {
        return default_value;
    }

    std::vector<char> buffer(length, '\0');
    if (nvs_get_str(_handle, key, buffer.data(), &length) != ESP_OK) {
        return default_value;
    }

    return String(buffer.data());
}

bool Storage::set_string(const char* key, const String& value) {
    if (!_read_write || _handle == 0 || key == nullptr) {
        return write_denied(_namespace_name);
    }

    const esp_err_t err = nvs_set_str(_handle, key, value.c_str());
    if (err != ESP_OK) {
        Serial.printf("[storage] set '%s/%s' failed: %s\n",
                      _namespace_name != nullptr ? _namespace_name : "unknown",
                      key,
                      esp_err_to_name(err));
        return false;
    }

    _dirty = true;
    return true;
}

uint8_t Storage::get_u8(const char* key, uint8_t default_value) const {
    return read_value<uint8_t>(_handle, key, default_value, nvs_get_u8);
}

bool Storage::set_u8(const char* key, uint8_t value) {
    return write_value<uint8_t>(_handle, _namespace_name, _read_write, _dirty, key, value, nvs_set_u8);
}

uint16_t Storage::get_u16(const char* key, uint16_t default_value) const {
    return read_value<uint16_t>(_handle, key, default_value, nvs_get_u16);
}

bool Storage::set_u16(const char* key, uint16_t value) {
    return write_value<uint16_t>(_handle, _namespace_name, _read_write, _dirty, key, value, nvs_set_u16);
}

uint32_t Storage::get_u32(const char* key, uint32_t default_value) const {
    return read_value<uint32_t>(_handle, key, default_value, nvs_get_u32);
}

bool Storage::set_u32(const char* key, uint32_t value) {
    return write_value<uint32_t>(_handle, _namespace_name, _read_write, _dirty, key, value, nvs_set_u32);
}

bool Storage::get_bool(const char* key, bool default_value) const {
    return get_u8(key, default_value ? 1u : 0u) != 0;
}

bool Storage::set_bool(const char* key, bool value) {
    return set_u8(key, value ? 1u : 0u);
}

bool Storage::commit() {
    if (!_read_write || _handle == 0) {
        return false;
    }
    if (!_dirty) {
        return true;
    }

    const esp_err_t err = nvs_commit(_handle);
    if (err != ESP_OK) {
        Serial.printf("[storage] commit '%s' failed: %s\n",
                      _namespace_name != nullptr ? _namespace_name : "unknown",
                      esp_err_to_name(err));
        return false;
    }

    _dirty = false;
    return true;
}

}  // namespace nm::drivers::storage
