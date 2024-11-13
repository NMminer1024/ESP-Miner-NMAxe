#include "helper.h"
#include <iostream>
#include <cmath>
#include <string>
#include <sstream>
#include <iomanip>
#include "logger.h"
#include <nvs_flash.h>
#include <nvs.h>
#include "csha256.h"
#include <time.h>

void logo_print() {
  delay(2000);
  log_w("\r\n            ___          ___         ");
  log_w("\r\n           /\\__\\        /\\__\\    ");
  log_w("\r\n          /::|  |      /::|  |       ");
  log_w("\r\n         /:|:|  |     /:|:|  |       ");
  log_w("\r\n        /:/|:|  |__  /:/|:|__|__     ");
  log_w("\r\n       /:/ |:| /\\__\\/:/ |::::\\__\\");
  log_w("\r\n       \\/__|:|/:/  /\\/__/~~/:/  /  ");
  log_w("\r\n           |:/:/  /       /:/  /     ");
  log_w("\r\n           |::/  /       /:/  /      ");
  log_w("\r\n           /:/  /       /:/  /       ");
  log_w("\r\n           \\/__/        \\/__/      \r\n");
}

void disable_usb_uart(){
  Serial.end();
  pinMode(19, INPUT);
  pinMode(20, INPUT);
}

String gen_device_code(void){
  char chipIdStr[13] = {0,};
  uint8_t deviceCode[32];
  String ret = "";
  sprintf(chipIdStr, "%012llx", ESP.getEfuseMac()); 
  csha256(chipIdStr, 12, deviceCode);  // sha256 of chip id is device code
  for (int i = 0; i < SHA256_SIZE_BYTES; i++) {
    char hex[3] = {0,};
    sprintf(hex, "%02x", deviceCode[i]);
    ret += hex;
  }
  return ret;
}

unsigned char reverse_bits(unsigned char num)
{
    unsigned char reversed = 0;
    int i;

    for (i = 0; i < 8; i++) {
        reversed <<= 1;      // Left shift the reversed variable by 1
        reversed |= num & 1; // Use bitwise OR to set the rightmost bit of reversed to the current bit of num
        num >>= 1;           // Right shift num by 1 to get the next bit
    }

    return reversed;
}

int largest_power_of_two(int num)
{
    int power = 0;

    while (num > 1) {
        num = num >> 1;
        power++;
    }

    return (1 << power);
}

uint16_t reverse_uint16(uint16_t num){
    return (num >> 8) | (num << 8);
}

static uint8_t hex(char ch) {
    uint8_t r = (ch > 57) ? (ch - 55) : (ch - 48);
    return r & 0x0F;
}

int str_to_byte_array(const char *in, size_t in_size, uint8_t *out) {
    if(in_size == 0) return 0;
    if(in == NULL || out == NULL) return -1;
    
    int count = 0;
    if (in_size % 2) {
        while (*in && out) {
            *out = hex(*in++);
            if (!*in)
                return count;
            *out = (*out << 4) | hex(*in++);
            *out++;
            count++;
        }
        return count;
    } else {
        while (*in && out) {
            *out++ = (hex(*in++) << 4) | hex(*in++);
            count++;
        }
        return count;
    }
}

bool reverse_words(uint8_t *data, size_t len) {
    if (len % 4 != 0) {
        return false;
    }
    size_t num_blocks = len / 4;
    for (size_t i = 0; i < num_blocks / 2; ++i) {
        for (size_t j = 0; j < 4; ++j) {
            std::swap(data[i * 4 + j], data[(num_blocks - 1 - i) * 4 + j]);
        }
    }
    return true;
}

void reverse_bytes(uint8_t * data, size_t len) {
    for (int i = 0; i < len / 2; ++i) {
        uint8_t temp = data[i];
        data[i] = data[len - 1 - i];
        data[len - 1 - i] = temp;
    }
}

String formatNumber(float num, uint8_t total_bits) {
    const char *units[] = {"", "K", "M", "G", "T", "P", "E", "Z", "Y"};
    int unitIndex = 0;

    while (num >= 1000.0 && unitIndex < 8) { 
        num /= 1000.0;
        unitIndex++;
    }

    if (num >= 1000.0 && unitIndex == 8) {
        return "999.9Y";
    }

    int integerPartLength = 1;
    if (num >= 10) {
        integerPartLength = 2;
    }
    if (num >= 100) {
        integerPartLength = 3;
    }

    int decimalPlaces = total_bits - integerPartLength;
    if (decimalPlaces < 0) {
        decimalPlaces = 0;
    }

    String formattedNumber = String(num, decimalPlaces) + units[unitIndex];
    return formattedNumber;
}

String get_last_reboot_reason(){
  switch(esp_reset_reason()){
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

void swap_endian_words(const char *hex_words, uint8_t *output)
{
    size_t hex_length = strlen(hex_words);
    if (hex_length % 8 != 0)
    {
        fprintf(stderr, "Must be 4-byte word aligned\n");
        exit(EXIT_FAILURE);
    }

    size_t binary_length = hex_length / 2;

    for (size_t i = 0; i < binary_length; i += 4)
    {
        for (int j = 0; j < 4; j++)
        {
            unsigned int byte_val;
            sscanf(hex_words + (i + j) * 2, "%2x", &byte_val);
            output[i + (3 - j)] = byte_val;
        }
    }
}

String convert_time_to_local(uint32_t timestamp, int8_t timezone) {
    time_t localTime = timestamp + timezone * 3600;

    struct tm *timeinfo = localtime(&localTime);
    char timeString[30];
    strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", timeinfo);

    return String(timeString);
}

String convert_uptime_to_string(uint32_t timecnt){
    char uptime[20] = {0,};

    uint32_t days = timecnt / 86400;
    uint32_t hours = (timecnt % 86400) / 3600;
    uint32_t minutes = (timecnt % 3600) / 60;
    uint32_t seconds = timecnt % 60;
    
    sprintf(uptime, "%03dd %02d:%02d:%02d", days, hours, minutes, seconds);
    return String(uptime);
}

double le_hash_to_diff(uint8_t *hash){
    //caculate diff form byte array, little endian
	uint64_t *data64;
	double dcut64;
    static const double diffone = 26959535291011309493156476344723991336010898738574164086137773096960.0;

	data64 = (uint64_t *)((uint8_t *)hash + 24);
	dcut64 = *data64 * 6277101735386680763835789423207666416102355444464034512896.0;

	data64 = (uint64_t *)((uint8_t *)hash + 16);
	dcut64 += *data64 * 340282366920938463463374607431768211456.0;

	data64 = (uint64_t *)((uint8_t *)hash + 8);
	dcut64 += *data64 * 18446744073709551616.0;

	data64 = (uint64_t *)((uint8_t *)hash);
	dcut64 += *data64;

    if (dcut64 == 0.0 || std::abs(dcut64) < std::numeric_limits<double>::epsilon()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return diffone / dcut64;
}
