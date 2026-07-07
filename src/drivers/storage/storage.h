// What: Shared NVS-backed key/value storage driver for application services.
// Why: Config and status persistence should not be implemented ad hoc inside
// boot, UI, or product code once the new framework starts carrying real state.
// Role: Wraps ESP-IDF NVS open/read/write/commit behavior behind one reusable
// driver with typed helpers and one-time flash initialization.
// Benefit: Higher layers can persist settings through one stable interface while
// keeping NVS lifecycle, locking, and error handling out of business logic.
#pragma once

#include <Arduino.h>
#include <nvs.h>
#include <stdint.h>

namespace nm::drivers::storage {

bool init_flash();

class Storage final {
public:
    explicit Storage(const char* namespace_name, bool read_write = false);
    ~Storage();

    bool valid() const { return _handle != 0; }

    String get_string(const char* key, const String& default_value = "") const;
    bool set_string(const char* key, const String& value);

    uint8_t get_u8(const char* key, uint8_t default_value = 0) const;
    bool set_u8(const char* key, uint8_t value);

    uint16_t get_u16(const char* key, uint16_t default_value = 0) const;
    bool set_u16(const char* key, uint16_t value);

    uint32_t get_u32(const char* key, uint32_t default_value = 0) const;
    bool set_u32(const char* key, uint32_t value);

    bool get_bool(const char* key, bool default_value = false) const;
    bool set_bool(const char* key, bool value);

    bool commit();

private:
    const char* _namespace_name = nullptr;
    nvs_handle_t _handle = 0;
    bool _read_write = false;
    bool _dirty = false;
};

}  // namespace nm::drivers::storage
