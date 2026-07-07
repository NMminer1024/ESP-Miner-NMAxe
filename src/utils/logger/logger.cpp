#include "utils/logger/logger.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace dbg {

char log_buffer[1152];

SemaphoreHandle_t logger_mutex() {
    static SemaphoreHandle_t s_mutex = xSemaphoreCreateMutex();
    return s_mutex;
}

void serial_print_locked(const char* text) {
    if (text == nullptr) {
        return;
    }

    SemaphoreHandle_t mutex = logger_mutex();
    if (mutex != nullptr && xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        Serial.print(text);
        xSemaphoreGive(mutex);
        return;
    }

    Serial.print(text);
}

void log_emit(bool auto_new_line, uint8_t color_n, const char* fmt, ...) {
    SemaphoreHandle_t mutex = logger_mutex();
    if (mutex != nullptr && xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    char msg_buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg_buffer, sizeof(msg_buffer), fmt, args);
    va_end(args);

    if (auto_new_line) {
        snprintf(log_buffer, sizeof(log_buffer), "\033[%um%s%s\033[0m\r\n",
                 static_cast<unsigned>(color_n), DBG_SECTION_NAME, msg_buffer);
    } else {
        snprintf(log_buffer, sizeof(log_buffer), "\033[%um%s\033[0m",
                 static_cast<unsigned>(color_n), msg_buffer);
    }

    Serial.print(log_buffer);

    if (mutex != nullptr) {
        xSemaphoreGive(mutex);
    }
}

void hex_print(uint8_t* data, uint16_t len, const char* tag) {
    if (data == nullptr) {
        return;
    }

    log_w("%s [%u] bytes: [", tag != nullptr ? tag : "hex", static_cast<unsigned>(len));
    for (uint16_t i = 0; i < len; ++i) {
        log_i("0x%02x", data[i]);
        if (i != len - 1) {
            log_i(",");
        }
    }
    LOG_W("]");
}

}  // namespace dbg
