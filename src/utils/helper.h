#ifndef _HELPER_H
#define _HELPER_H
#include <Arduino.h>

#define IN
#define OUT

void logo_print() ;

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

bool validateInput(const String &input);

#endif 
