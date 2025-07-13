#ifndef DISPLAY_H
#define DISPLAY_H
#include <Arduino.h>
void tft_bl_ctrl(int8_t percent);
void ui_switch_next_page_cb();
void ui_thread_entry(void *args);
#endif // DISPLAY_H
