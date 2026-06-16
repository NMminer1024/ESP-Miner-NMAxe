#include "helper.h"
#include <iostream>
#include <cmath>
#include <string>
#include <sstream>
#include <iomanip>
#include "logger/logger.h"
#include <nvs_flash.h>
#include <nvs.h>
#include "utils/sha/csha256.h"
#include <time.h>




// void logo_print() {
//   delay(2000);
//   log_w("\r\n            ___          ___         ");
//   log_w("\r\n           /\\__\\        /\\__\\    ");
//   log_w("\r\n          /::|  |      /::|  |       ");
//   log_w("\r\n         /:|:|  |     /:|:|  |       ");
//   log_w("\r\n        /:/|:|  |__  /:/|:|__|__     ");
//   log_w("\r\n       /:/ |:| /\\__\\/:/ |::::\\__\\");
//   log_w("\r\n       \\/__|:|/:/  /\\/__/~~/:/  /  ");
//   log_w("\r\n           |:/:/  /       /:/  /     ");
//   log_w("\r\n           |::/  /       /:/  /      ");
//   log_w("\r\n           /:/  /       /:/  /       ");
//   log_w("\r\n           \\/__/        \\/__/      \r\n");
// }

void disable_usb_uart(){
  Serial.end();
  pinMode(19, INPUT);
  pinMode(20, INPUT);
}

bool psram_init(){
  if (esp_spiram_init() != ESP_OK) {
      LOG_W("Seems like no PSRAM available.");
      return false;
  } else {
      // 检查 PSRAM 大小
      size_t psram_size = esp_spiram_get_size();
      LOG_I("PSRAM Size: %d Mbytes", psram_size/1024/1024);
      // 检查可用的堆内存
      size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
      LOG_I("HEAP Size: %d Mbytes", free_heap/1024/1024);
        return true;
  }
}


void* psramAllocator(size_t size) {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
}

void psramDeallocator(void* pointer) {
    heap_caps_free(pointer);
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

void swap_endian_words(const char *hex_words, uint8_t *output){
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

String convert_time_to_local(uint32_t timestamp) {
    time_t localTime = timestamp;

    struct tm *timeinfo = localtime(&localTime);
    char timeString[30] = {0,};

    strftime(timeString, sizeof(timeString), "%d-%m-%Y %I:%M:%S %p", timeinfo);

    return String(timeString);
}


String convert_time_to_local_12h(uint32_t timestamp, String date_format) {
    time_t localTime = timestamp;

    struct tm *timeinfo = localtime(&localTime);
    char timeString[30] = {0,};
    if(date_format == "MM-DD-YYYY")
        strftime(timeString, sizeof(timeString), "%m-%d-%Y %I:%M %p", timeinfo);
    else if(date_format == "YYYY-MM-DD")
        strftime(timeString, sizeof(timeString), "%Y-%m-%d %I:%M %p", timeinfo);
    else if(date_format == "YYYY-DD-MM")
        strftime(timeString, sizeof(timeString), "%Y-%d-%m %I:%M %p", timeinfo);
    else if(date_format == "DD-MM-YYYY")
        strftime(timeString, sizeof(timeString), "%d-%m-%Y %I:%M %p", timeinfo);
    else if(date_format == "DD/MM/YYYY")
        strftime(timeString, sizeof(timeString), "%d/%m/%Y %I:%M %p", timeinfo);
    else if(date_format == "MM/DD/YYYY")
        strftime(timeString, sizeof(timeString), "%m/%d/%Y %I:%M %p", timeinfo);
    else if(date_format == "YYYY/MM/DD")
        strftime(timeString, sizeof(timeString), "%Y/%m/%d %I:%M %p", timeinfo);
    else if(date_format == "YYYY/DD/MM")
        strftime(timeString, sizeof(timeString), "%Y/%d/%m %I:%M %p", timeinfo);
    else // default DD-MM-YYYY
        strftime(timeString, sizeof(timeString), "%d-%m-%Y %I:%M %p", timeinfo);

    return String(timeString);
}

String convert_time_to_local_24h(uint32_t timestamp, String date_format) {
    time_t localTime = timestamp;

    struct tm *timeinfo = localtime(&localTime);
    char timeString[30] = {0,};
    if(date_format == "MM-DD-YYYY")
        strftime(timeString, sizeof(timeString), "%m-%d-%Y %H:%M:", timeinfo);
    else if(date_format == "YYYY-MM-DD")
        strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M", timeinfo);
    else if(date_format == "YYYY-DD-MM")
        strftime(timeString, sizeof(timeString), "%Y-%d-%m %H:%M", timeinfo);
    else if(date_format == "DD-MM-YYYY")
        strftime(timeString, sizeof(timeString), "%d-%m-%Y %H:%M", timeinfo);
    else if(date_format == "DD/MM/YYYY")
        strftime(timeString, sizeof(timeString), "%d/%m/%Y %H:%M", timeinfo);
    else if(date_format == "MM/DD/YYYY")
        strftime(timeString, sizeof(timeString), "%m/%d/%Y %H:%M", timeinfo);
    else if(date_format == "YYYY/MM/DD")
        strftime(timeString, sizeof(timeString), "%Y/%m/%d %H:%M", timeinfo);
    else if(date_format == "YYYY/DD/MM")
        strftime(timeString, sizeof(timeString), "%Y/%d/%m %H:%M", timeinfo);
    else // default DD-MM-YYYY
        strftime(timeString, sizeof(timeString), "%d-%m-%Y %H:%M", timeinfo);
        
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

    double result = diffone / dcut64;
    if (std::isinf(result)) {
        LOG_E("diff calculate error, infinite result!!!");
        return std::numeric_limits<double>::infinity();
    }

    return result;
}

int compareVersions(const String& current, const String& release) {
    if((release == "") || (release == "None")) return -2; // have no release version get yet

    String v1 = current.substring(1);
    String v2 = release.substring(1);
    
    int major1 = 0, minor1 = 0, patch1 = 0;
    int major2 = 0, minor2 = 0, patch2 = 0;
    
    // parse version1
    int dotPos1 = v1.indexOf('.');
    if (dotPos1 != -1) {
        major1 = v1.substring(0, dotPos1).toInt();
        int dotPos2 = v1.indexOf('.', dotPos1 + 1);
        if (dotPos2 != -1) {
            minor1 = v1.substring(dotPos1 + 1, dotPos2).toInt();
            patch1 = v1.substring(dotPos2 + 1).toInt();
        } else {
            minor1 = v1.substring(dotPos1 + 1).toInt();
        }
    } else {
        major1 = v1.toInt();
    }
    
    // parse version2
    int dotPos3 = v2.indexOf('.');
    if (dotPos3 != -1) {
        major2 = v2.substring(0, dotPos3).toInt();
        int dotPos4 = v2.indexOf('.', dotPos3 + 1);
        if (dotPos4 != -1) {
            minor2 = v2.substring(dotPos3 + 1, dotPos4).toInt();
            patch2 = v2.substring(dotPos4 + 1).toInt();
        } else {
            minor2 = v2.substring(dotPos3 + 1).toInt();
        }
    } else {
        major2 = v2.toInt();
    }
    
    if (major1 < major2) return -1;
    if (major1 > major2) return 1;
    

    if (minor1 < minor2) return -1;
    if (minor1 > minor2) return 1;
    

    if (patch1 < patch2) return -1;
    if (patch1 > patch2) return 1;
    

    return 0;
}



float parseHashRateStr(const String& hashRateStr) {
    float hashRate = 0.0;
    int len = hashRateStr.length();

    int unitPos = hashRateStr.indexOf("H/s");
    if (unitPos == -1) {
        return hashRate; 
    }


    String valueStr = ""; 
    String unitStr = ""; 
    if(isDigit(hashRateStr.substring(unitPos-1, unitPos).c_str()[0])){
        valueStr = hashRateStr.substring(0, unitPos);
        valueStr.trim(); 

        unitStr = hashRateStr.substring(unitPos, len);
        unitStr.trim();
    }else{
        valueStr = hashRateStr.substring(0, unitPos - 1);
        valueStr.trim(); 
        unitStr = hashRateStr.substring(unitPos-1, len);
        unitStr.trim(); 
    }

    float value = valueStr.toFloat();

    if (unitStr == "TH/s" || unitStr == "tH/s" || unitStr == "th/s" || unitStr == "Th/s") {
        hashRate = value * 1e12;
    }else if (unitStr == "GH/s" || unitStr == "gH/s" || unitStr == "gh/s" || unitStr == "Gh/s") {
        hashRate = value * 1e9;
    } else if (unitStr == "MH/s" || unitStr == "mH/s" || unitStr == "mh/s" || unitStr == "Mh/s") {
        hashRate = value * 1e6;
    } else if (unitStr == "KH/s" || unitStr == "kH/s" || unitStr == "Kh/s" || unitStr == "kh/s") {
        hashRate = value * 1e3;
    } else if (unitStr == "H/s" || unitStr == "h/s") {
        hashRate = value;
    } else{
        return 0.0; 
    }

    return hashRate;
}

float parseDiffStr(const String& diffStr) {
    float diff = 0.0;
    int len = diffStr.length();

    // 查找单位的位置
    String unitStr = String(diffStr.charAt(len-1)); // 单位部分
    if(isdigit(unitStr.c_str()[0])){ // 如果最后一个字符是数字，则没有单位部分
        diff = diffStr.toFloat(); // 直接转换为浮点数
        return diff;
    }

    // 提取数值部分
    String valueStr = diffStr.substring(0, len - 1); // 去除最后一个字符（单位）
    valueStr.trim(); // 去除前后的空格

    // 将数值部分转换为浮点数
    diff = valueStr.toFloat();

    if(unitStr == "K"){ // 单位为K，则乘以1000
        diff *= 1e3;
    }else if(unitStr == "M"){ // 单位为M，则乘以1000000
        diff *= 1e6;
    }else if(unitStr == "G"){ // 单位为G，则乘以1000000000
        diff *= 1e9;
    }else if(unitStr == "T"){ // 单位为T，则乘以1000000000000
        diff *= 1e12;
    }else if(unitStr == "P"){ // 单位为P，则乘以1000000000000000
        diff *= 1e15;
    }else {
        return 0.0; // 无效的单位
    }

    return diff;
}

uint8_t guess_touch_gesture(int dx, int dy, int threshold){
    if(abs(dx) > threshold || abs(dy) > threshold){
        if(abs(dx) > abs(dy)){
            return (dx > 0) ? TOUCH_SWIPE_RIGHT_EVT : TOUCH_SWIPE_LEFT_EVT;
        } else {
            return (dy > 0) ? TOUCH_SWIPE_DOWN_EVT : TOUCH_SWIPE_UP_EVT;
        }
    }else{
        return TOUCH_TAP_EVT;
    }
}