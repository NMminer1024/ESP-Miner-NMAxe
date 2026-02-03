#ifndef _HELPER_H
#define _HELPER_H
#include <Arduino.h>
#include <deque>
#include <utility> // for std::pair

#define IN
#define OUT

enum {
    TOUCH_NONE_EVT = 0,
    TOUCH_TAP_EVT,
    TOUCH_LONGPRESS_EVT,
    TOUCH_SWIPE_LEFT_EVT,
    TOUCH_SWIPE_RIGHT_EVT,
    TOUCH_SWIPE_UP_EVT,
    TOUCH_SWIPE_DOWN_EVT,
};

bool psram_init();

void* psramAllocator(size_t size);

void psramDeallocator(void* pointer);

void disable_usb_uart();

String gen_device_code(void);

int str_to_byte_array(const char *in, size_t in_size, uint8_t *out);

String get_last_reboot_reason();

String formatNumber(float num, uint8_t bits);

uint16_t reverse_uint16(uint16_t num);

bool reverse_words(uint8_t *data, size_t len);

void reverse_bytes(uint8_t * data, size_t len);

unsigned char reverse_bits(unsigned char num);

int largest_power_of_two(int num);

String convert_time_to_local(uint32_t timestamp);

String convert_uptime_to_string(uint32_t timecnt);

double le_hash_to_diff(uint8_t *hash);

int compareVersions(const String& current, const String& release);

float parseHashRateStr(const String& hashRateStr);

float parseDiffStr(const String& diffStr);

String convert_time_to_local_12h(uint32_t timestamp, String date_format = "YYYY/MM/DD");

String convert_time_to_local_24h(uint32_t timestamp, String date_format = "YYYY/MM/DD");

uint8_t guess_touch_gesture(int dx, int dy, bool flip, int threshold = 30);

template <typename T>
struct PsramAllocator {
    using value_type = T;

    PsramAllocator() = default;

    template <typename U>
    constexpr PsramAllocator(const PsramAllocator<U>&) noexcept {}

    T* allocate(std::size_t n) {
        T* ptr = static_cast<T*>(heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM));
        if (!ptr) {
            throw std::bad_alloc();
        }
        return ptr;
    }

    void deallocate(T* p, std::size_t) noexcept {
        heap_caps_free(p);
    }
};

template <typename T, typename U>
bool operator==(const PsramAllocator<T>&, const PsramAllocator<U>&) { return true; }

template <typename T, typename U>
bool operator!=(const PsramAllocator<T>&, const PsramAllocator<U>&) { return false; }

#endif 
