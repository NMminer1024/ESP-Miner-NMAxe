#include "utils/helper.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <time.h>

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "utils/logger/logger.h"
#include "utils/sha/csha256.h"

void disable_usb_uart() {
    Serial.end();
    pinMode(19, INPUT);
    pinMode(20, INPUT);
}

bool psram_init() {
    if (esp_spiram_init() != ESP_OK) {
        LOG_W("Seems like no PSRAM available.");
        return false;
    }

    const size_t psram_size = esp_spiram_get_size();
    const size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    LOG_I("PSRAM Size: %u Mbytes", static_cast<unsigned>(psram_size / 1024 / 1024));
    LOG_I("HEAP Size: %u Mbytes", static_cast<unsigned>(free_heap / 1024 / 1024));
    return true;
}

void* psramAllocator(size_t size) {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void psramDeallocator(void* pointer) {
    heap_caps_free(pointer);
}

String gen_device_code() {
    char chip_id_str[13] = {};
    uint8_t device_code[SHA256_SIZE_BYTES] = {};
    String ret;

    snprintf(chip_id_str, sizeof(chip_id_str), "%012llx", ESP.getEfuseMac());
    csha256(chip_id_str, 12, device_code);
    for (int i = 0; i < SHA256_SIZE_BYTES; ++i) {
        char hex[3] = {};
        snprintf(hex, sizeof(hex), "%02x", device_code[i]);
        ret += hex;
    }
    return ret;
}

unsigned char reverse_bits(unsigned char num) {
    unsigned char reversed = 0;
    for (int i = 0; i < 8; ++i) {
        reversed <<= 1;
        reversed |= num & 1;
        num >>= 1;
    }
    return reversed;
}

int largest_power_of_two(int num) {
    int power = 0;
    while (num > 1) {
        num >>= 1;
        ++power;
    }
    return 1 << power;
}

uint16_t reverse_uint16(uint16_t num) {
    return (num >> 8) | (num << 8);
}

static uint8_t hex_nibble(char ch) {
    uint8_t r = (ch > '9') ? (ch - 'A' + 10) : (ch - '0');
    if (ch >= 'a' && ch <= 'f') {
        r = ch - 'a' + 10;
    }
    return r & 0x0F;
}

int str_to_byte_array(const char* in, size_t in_size, uint8_t* out) {
    if (in_size == 0) {
        return 0;
    }
    if (in == nullptr || out == nullptr) {
        return -1;
    }

    int count = 0;
    if (in_size % 2) {
        while (*in != '\0') {
            *out = hex_nibble(*in++);
            if (*in == '\0') {
                return count;
            }
            *out = (*out << 4) | hex_nibble(*in++);
            ++out;
            ++count;
        }
        return count;
    }

    while (*in != '\0') {
        *out++ = (hex_nibble(*in++) << 4) | hex_nibble(*in++);
        ++count;
    }
    return count;
}

bool reverse_words(uint8_t* data, size_t len) {
    if (data == nullptr || len % 4 != 0) {
        return false;
    }
    const size_t words = len / 4;
    for (size_t i = 0; i < words / 2; ++i) {
        for (size_t j = 0; j < 4; ++j) {
            std::swap(data[i * 4 + j], data[(words - 1 - i) * 4 + j]);
        }
    }
    return true;
}

void reverse_bytes(uint8_t* data, size_t len) {
    if (data == nullptr) {
        return;
    }
    for (size_t i = 0; i < len / 2; ++i) {
        std::swap(data[i], data[len - 1 - i]);
    }
}

String formatNumber(float num, uint8_t total_bits) {
    const char* units[] = {"", "K", "M", "G", "T", "P", "E", "Z", "Y"};
    int unit_index = 0;
    while (num >= 1000.0f && unit_index < 8) {
        num /= 1000.0f;
        ++unit_index;
    }
    if (num >= 1000.0f && unit_index == 8) {
        return "999.9Y";
    }

    int integer_len = 1;
    if (num >= 100) {
        integer_len = 3;
    } else if (num >= 10) {
        integer_len = 2;
    }

    int decimals = total_bits - integer_len;
    if (decimals < 0) {
        decimals = 0;
    }
    return String(num, decimals) + units[unit_index];
}

String get_last_reboot_reason() {
    switch (esp_reset_reason()) {
        case ESP_RST_UNKNOWN: return "Unknown";
        case ESP_RST_POWERON: return "Power on";
        case ESP_RST_EXT: return "External reset";
        case ESP_RST_SW: return "Software reset";
        case ESP_RST_PANIC: return "Exception reset";
        case ESP_RST_INT_WDT: return "Watchdog int reset";
        case ESP_RST_TASK_WDT: return "Task watchdog reset";
        case ESP_RST_WDT: return "Watchdog reset";
        case ESP_RST_DEEPSLEEP: return "Deep sleep";
        case ESP_RST_BROWNOUT: return "Brownout";
        case ESP_RST_SDIO: return "SDIO reset";
        default: return "Unknown";
    }
}

String convert_time_to_local(uint32_t timestamp) {
    time_t local_time = timestamp;
    struct tm* timeinfo = localtime(&local_time);
    char time_string[30] = {};
    strftime(time_string, sizeof(time_string), "%d-%m-%Y %I:%M:%S %p", timeinfo);
    return String(time_string);
}

String convert_time_to_local_12h(uint32_t timestamp, String date_format) {
    time_t local_time = timestamp;
    struct tm* timeinfo = localtime(&local_time);
    char time_string[30] = {};
    if (date_format == "MM-DD-YYYY") {
        strftime(time_string, sizeof(time_string), "%m-%d-%Y %I:%M %p", timeinfo);
    } else if (date_format == "YYYY-MM-DD") {
        strftime(time_string, sizeof(time_string), "%Y-%m-%d %I:%M %p", timeinfo);
    } else if (date_format == "DD/MM/YYYY") {
        strftime(time_string, sizeof(time_string), "%d/%m/%Y %I:%M %p", timeinfo);
    } else if (date_format == "MM/DD/YYYY") {
        strftime(time_string, sizeof(time_string), "%m/%d/%Y %I:%M %p", timeinfo);
    } else {
        strftime(time_string, sizeof(time_string), "%Y/%m/%d %I:%M %p", timeinfo);
    }
    return String(time_string);
}

String convert_time_to_local_24h(uint32_t timestamp, String date_format) {
    time_t local_time = timestamp;
    struct tm* timeinfo = localtime(&local_time);
    char time_string[30] = {};
    if (date_format == "MM-DD-YYYY") {
        strftime(time_string, sizeof(time_string), "%m-%d-%Y %H:%M", timeinfo);
    } else if (date_format == "YYYY-MM-DD") {
        strftime(time_string, sizeof(time_string), "%Y-%m-%d %H:%M", timeinfo);
    } else if (date_format == "DD/MM/YYYY") {
        strftime(time_string, sizeof(time_string), "%d/%m/%Y %H:%M", timeinfo);
    } else if (date_format == "MM/DD/YYYY") {
        strftime(time_string, sizeof(time_string), "%m/%d/%Y %H:%M", timeinfo);
    } else {
        strftime(time_string, sizeof(time_string), "%Y/%m/%d %H:%M", timeinfo);
    }
    return String(time_string);
}

String convert_uptime_to_string(uint32_t timecnt) {
    char uptime[20] = {};
    const uint32_t days = timecnt / 86400;
    const uint32_t hours = (timecnt % 86400) / 3600;
    const uint32_t minutes = (timecnt % 3600) / 60;
    const uint32_t seconds = timecnt % 60;
    snprintf(uptime, sizeof(uptime), "%03ud %02u:%02u:%02u",
             static_cast<unsigned>(days),
             static_cast<unsigned>(hours),
             static_cast<unsigned>(minutes),
             static_cast<unsigned>(seconds));
    return String(uptime);
}

double le_hash_to_diff(uint8_t* hash) {
    if (hash == nullptr) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    static const double diffone = 26959535291011309493156476344723991336010898738574164086137773096960.0;
    auto* data64 = reinterpret_cast<uint64_t*>(hash + 24);
    double dcut64 = *data64 * 6277101735386680763835789423207666416102355444464034512896.0;
    data64 = reinterpret_cast<uint64_t*>(hash + 16);
    dcut64 += *data64 * 340282366920938463463374607431768211456.0;
    data64 = reinterpret_cast<uint64_t*>(hash + 8);
    dcut64 += *data64 * 18446744073709551616.0;
    data64 = reinterpret_cast<uint64_t*>(hash);
    dcut64 += *data64;

    if (dcut64 == 0.0 || std::abs(dcut64) < std::numeric_limits<double>::epsilon()) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double result = diffone / dcut64;
    if (std::isinf(result)) {
        LOG_E("diff calculate error, infinite result");
        return std::numeric_limits<double>::infinity();
    }
    return result;
}

int compareVersions(const String& current, const String& release) {
    if (release == "" || release == "None") {
        return -2;
    }

    String v1 = current.substring(1);
    String v2 = release.substring(1);
    int major1 = 0, minor1 = 0, patch1 = 0;
    int major2 = 0, minor2 = 0, patch2 = 0;
    sscanf(v1.c_str(), "%d.%d.%d", &major1, &minor1, &patch1);
    sscanf(v2.c_str(), "%d.%d.%d", &major2, &minor2, &patch2);

    if (major1 != major2) return major1 < major2 ? -1 : 1;
    if (minor1 != minor2) return minor1 < minor2 ? -1 : 1;
    if (patch1 != patch2) return patch1 < patch2 ? -1 : 1;
    return 0;
}

float parseHashRateStr(const String& hashRateStr) {
    String s = hashRateStr;
    s.trim();
    const int unit_pos = s.indexOf("H/s");
    if (unit_pos < 0) {
        return 0.0f;
    }

    String unit = s.substring(unit_pos - 1);
    String value = s.substring(0, unit_pos - 1);
    value.trim();
    unit.trim();
    const float number = value.toFloat();
    if (unit.equalsIgnoreCase("TH/s")) return number * 1e12f;
    if (unit.equalsIgnoreCase("GH/s")) return number * 1e9f;
    if (unit.equalsIgnoreCase("MH/s")) return number * 1e6f;
    if (unit.equalsIgnoreCase("KH/s")) return number * 1e3f;
    if (unit.equalsIgnoreCase("H/s")) return number;
    return 0.0f;
}

float parseDiffStr(const String& diffStr) {
    String s = diffStr;
    s.trim();
    if (s.isEmpty()) {
        return 0.0f;
    }

    const char unit = s.charAt(s.length() - 1);
    if (isdigit(unit)) {
        return s.toFloat();
    }

    const float value = s.substring(0, s.length() - 1).toFloat();
    switch (unit) {
        case 'K': return value * 1e3f;
        case 'M': return value * 1e6f;
        case 'G': return value * 1e9f;
        case 'T': return value * 1e12f;
        case 'P': return value * 1e15f;
        default: return 0.0f;
    }
}
