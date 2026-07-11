
#include "logger.h"
#include <cstdarg>
#include <sys/time.h>

namespace dbg{

char log_buffer[1152];

// 格式化当前时间为 HH:MM:SS.mmm，写入外部提供的 buf[max 16]
static void format_time_ms(char* buf, size_t buf_size) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm* tm_info = localtime(&tv.tv_sec);
    snprintf(buf, buf_size, "%02d:%02d:%02d.%03d",
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
             (int)(tv.tv_usec / 1000));
}

SemaphoreHandle_t logger_mutex() {
    static SemaphoreHandle_t s_mutex = xSemaphoreCreateMutex();
    return s_mutex;
}

void serial_print_locked(const char* text) {
    if (text == NULL) return;
    SemaphoreHandle_t mutex = logger_mutex();
    if (mutex != NULL) {
        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            Serial.print(text);
            xSemaphoreGive(mutex);
            return;
        }
    }
    Serial.print(text);
}

void log_emit(bool auto_new_line, uint8_t color_n, const char* fmt, ...) {
    SemaphoreHandle_t mutex = logger_mutex();
    if (mutex != NULL && xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    char msg_buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg_buffer, sizeof(msg_buffer), fmt, args);
    va_end(args);

    int content_len = strlen(msg_buffer);
    char time_str[16];
    format_time_ms(time_str, sizeof(time_str));
#ifdef LOG_COLOR_ENABLE
    if (content_len > 0 && content_len < 950) {
        if (auto_new_line) {
            snprintf(log_buffer, sizeof(log_buffer), "\033[%um" DBG_SECTION_NAME " [%s] %s\033[0m\r\n",
                     (unsigned)color_n, time_str, msg_buffer);
        } else {
            snprintf(log_buffer, sizeof(log_buffer), "\033[%um%s\033[0m",
                     (unsigned)color_n, msg_buffer);
        }
    } else {
        if (auto_new_line) {
            snprintf(log_buffer, sizeof(log_buffer), "\033[%um" DBG_SECTION_NAME " [%s] %s\033[0m\r\n",
                     (unsigned)color_n, time_str, msg_buffer);
        } else {
            snprintf(log_buffer, sizeof(log_buffer), "\033[%um%s\033[0m",
                     (unsigned)color_n, msg_buffer);
        }
    }
#else
    if (content_len > 0 && content_len < 950) {
        if (auto_new_line) {
            snprintf(log_buffer, sizeof(log_buffer), DBG_SECTION_NAME " [%s] %s\r\n", time_str, msg_buffer);
        } else {
            snprintf(log_buffer, sizeof(log_buffer), "%s", msg_buffer);
        }
    } else {
        if (auto_new_line) {
            snprintf(log_buffer, sizeof(log_buffer), DBG_SECTION_NAME " [%s] %s\r\n", time_str, msg_buffer);
        } else {
            snprintf(log_buffer, sizeof(log_buffer), "%s", msg_buffer);
        }
    }
#endif

    Serial.print(log_buffer);
    webSocket.textAll(log_buffer);

    if (mutex != NULL) {
        xSemaphoreGive(mutex);
    }
}

/**
 * @brief Prints the hexadecimal representation of an array of bytes.
 * 
 * This function prints the hexadecimal representation of an array of bytes, along with a tag and the length of the array.
 * Each byte is printed as "0xXX", separated by commas.
 * 
 * @param pary Pointer to the array of bytes.
 * @param len Length of the array.
 * @param tag Tag to be printed before the array.
 */
void hex_print(uint8_t *pary, uint16_t len, const char *tag){
    if(pary == NULL) return;

    // 拼接完整 hex 字符串，一次性打印带时间戳
    char hex_buf[512];
    int offset = snprintf(hex_buf, sizeof(hex_buf), "%s [%d] bytes: [", tag, len);
    for (uint16_t i = 0; i < len && offset < (int)sizeof(hex_buf) - 4; i++) {
        offset += snprintf(hex_buf + offset, sizeof(hex_buf) - offset,
                          "%02X ", *(uint8_t*)(pary + i));
    }
    if (offset < (int)sizeof(hex_buf) - 3) {
        offset += snprintf(hex_buf + offset, sizeof(hex_buf) - offset, "]");
    }

    LOG_W("%s", hex_buf);
}
} // namespace dbg










