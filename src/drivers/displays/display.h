#ifndef DISPLAY_H
#define DISPLAY_H
#include <Arduino.h>
#include "lvgl.h"
enum{
    UI_PAGE_LOADING = 0,
    UI_PAGE_CONFIG,
    UI_PAGE_MINER,
    UI_PAGE_DASHBOARD,
    UI_PAGE_HR_HEALTH,
    UI_PAGE_BIG_DIGIT,
};

typedef struct{
    lv_obj_t        *obj;
    const lv_font_t *font; 
    struct {
        lv_coord_t x, y;
    } coord;
}ui_element_t;

void tft_bl_ctrl(int8_t percent);
void ui_switch_next_page_cb();
void ui_thread_entry(void *args);
#endif // DISPLAY_H
