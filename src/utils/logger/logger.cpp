
#include "logger.h"
#include <cstdarg>

namespace dbg{

char log_buffer[1152];

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
    if (content_len > 0 && content_len < 950) {
        if (auto_new_line) {
            char prefix[] = "\033[00m" DBG_SECTION_NAME " ";
            prefix[3] = (char)('0' + (color_n / 10));
            prefix[4] = (char)('0' + (color_n % 10));
            int prefix_len = strlen(prefix);
            memmove(log_buffer + prefix_len, msg_buffer, content_len + 1);
            memcpy(log_buffer, prefix, prefix_len);
            strcpy(log_buffer + prefix_len + content_len, "\033[0m\r\n");
        } else {
            char prefix[] = "\033[00m";
            prefix[3] = (char)('0' + (color_n / 10));
            prefix[4] = (char)('0' + (color_n % 10));
            int prefix_len = strlen(prefix);
            memmove(log_buffer + prefix_len, msg_buffer, content_len + 1);
            memcpy(log_buffer, prefix, prefix_len);
            strcpy(log_buffer + prefix_len + content_len, "\033[0m");
        }
    } else {
        if (auto_new_line) {
            snprintf(log_buffer, sizeof(log_buffer), "\033[%um" DBG_SECTION_NAME " %s\033[0m\r\n",
                     (unsigned)color_n, msg_buffer);
        } else {
            snprintf(log_buffer, sizeof(log_buffer), "\033[%um%s\033[0m",
                     (unsigned)color_n, msg_buffer);
        }
    }

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
    log_w("%s [%d] bytes: [",tag, len);   
    for(uint16_t i = 0 ; i<len; i++)
    {
      log_i("0x%02x", *(uint8_t*)(pary + i));
      if(i != len-1) log_i(","); 
    }
    log_w("]\r\n"); 
}

// void hex_print(uint8_t *pary, uint16_t len, const char *tag){
//     if(pary == NULL) return;
//     log_w("%s[",tag);   
//     for(uint16_t i = 0 ; i<len-1; i++)
//     {
//       log_i("%02X", *(uint8_t*)(pary + i));
//       // if(i != len-1) log_i(","); 
//     }
//     log_w("%02X]\r\n", *(uint8_t*)(pary + len-1));
// }



} // namespace dbg










