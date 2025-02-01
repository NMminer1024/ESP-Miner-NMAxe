#ifndef _HELPER_H
#define _HELPER_H
#include <Arduino.h>
#include <deque>
#include <utility> // for std::pair


#define IN
#define OUT

void logo_print() ;

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

String convert_time_to_local(uint32_t timestamp, int8_t timezone);

String convert_uptime_to_string(uint32_t timecnt);

double le_hash_to_diff(uint8_t *hash);





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
