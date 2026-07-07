#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstring>

#ifdef log_i
#undef log_i
#endif

#ifdef log_d
#undef log_d
#endif

#ifdef log_w
#undef log_w
#endif

#ifdef log_e
#undef log_e
#endif

namespace dbg {

extern char log_buffer[1152];

SemaphoreHandle_t logger_mutex();
void serial_print_locked(const char* text);
void log_emit(bool auto_new_line, uint8_t color_n, const char* fmt, ...);
void hex_print(uint8_t* data, uint16_t len, const char* tag);

#define FILENAME (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)
#define DBG_SECTION_NAME ""

#define ERROR 0
#define WARNING 1
#define INFO 2
#define LOG 3
#define DEBUG 4

#ifndef DBG_LEVEL
#define DBG_LEVEL LOG
#endif

#define dbg_log_line(auto_new_line, lvl, color_n, fmt, ...) \
    do {                                                    \
        dbg::log_emit((auto_new_line), (color_n), (fmt), ##__VA_ARGS__); \
    } while (0)

#if (DBG_LEVEL >= DEBUG)
#define LOG_D(fmt, ...) dbg_log_line(true, "D", 0, fmt, ##__VA_ARGS__)
#define log_d(fmt, ...) dbg_log_line(false, "D", 0, fmt, ##__VA_ARGS__)
#else
#define LOG_D(...)
#define log_d(...)
#endif

#if (DBG_LEVEL >= LOG)
#define LOG_L(fmt, ...) dbg_log_line(true, "L", 36, fmt, ##__VA_ARGS__)
#define log_l(fmt, ...) dbg_log_line(false, "L", 36, fmt, ##__VA_ARGS__)
#else
#define LOG_L(...)
#define log_l(...)
#endif

#if (DBG_LEVEL >= INFO)
#define LOG_I(fmt, ...) dbg_log_line(true, "I", 32, fmt, ##__VA_ARGS__)
#define log_i(fmt, ...) dbg_log_line(false, "I", 32, fmt, ##__VA_ARGS__)
#else
#define LOG_I(...)
#define log_i(...)
#endif

#if (DBG_LEVEL >= WARNING)
#define LOG_W(fmt, ...) dbg_log_line(true, "W", 33, fmt, ##__VA_ARGS__)
#define log_w(fmt, ...) dbg_log_line(false, "W", 33, fmt, ##__VA_ARGS__)
#else
#define LOG_W(...)
#define log_w(...)
#endif

#if (DBG_LEVEL >= ERROR)
#define LOG_E(fmt, ...) dbg_log_line(true, "E", 31, fmt, ##__VA_ARGS__)
#define log_e(fmt, ...) dbg_log_line(false, "E", 31, fmt, ##__VA_ARGS__)
#define LOG_E_LOC(fmt, ...) LOG_E("%s:%d:[%s]=> " fmt, FILENAME, __LINE__, __func__, ##__VA_ARGS__)
#else
#define LOG_E(...)
#define log_e(...)
#define LOG_E_LOC(...)
#endif

}  // namespace dbg
