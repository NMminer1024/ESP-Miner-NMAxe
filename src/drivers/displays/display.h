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
    UI_PAGE_HITS
};

typedef struct{
    lv_obj_t        *obj;
    const lv_font_t *font; 
    struct {
        lv_coord_t x, y;
    } coord;
}ui_element_t;



// ring UI status configuration
typedef struct {
    lv_coord_t x;                    // X坐标
    lv_coord_t y;                    // Y坐标
    lv_coord_t radius;               // 半径
    lv_coord_t line_width;           // 线宽
    uint16_t angle_start;            // 起始角度
    uint16_t angle_end;              // 终止角度
    uint16_t angle_full;             // 满刻度角度(如230度)
    lv_color_t bg_color;             // 背景颜色
    lv_color_t indicator_color;      // 指示器颜色
    const char* center_text;         // 中心显示文本
    const lv_font_t* center_font;    // 中心文本字体
    lv_color_t center_text_color;    // 中心文本颜色
    const char* title_text;          // 标题文本
    const lv_font_t* title_font;     // 标题字体
    lv_color_t title_text_color;     // 标题文本颜色
} ui_ring_config_t;

// 圆环UI对象结构体
typedef struct {
    lv_obj_t* arc;                   // 圆弧对象
    lv_obj_t* label_center;          // 中心标签
    lv_obj_t* label_title;           // 标题标签
} ui_ring_obj_t;

// Pie chart sector configuration
typedef struct {
    uint16_t angle;          // Sector angle (0-360)
    lv_color_t color;        // Sector color
    const char* label;       // Sector label (optional)
} pie_sector_t;

// Pie chart object structure (max 8 sectors)
#define PIE_CHART_MAX_SECTORS 8
typedef struct {
    lv_obj_t* arcs[PIE_CHART_MAX_SECTORS];           // Arc objects for each sector
    lv_obj_t* labels[PIE_CHART_MAX_SECTORS];         // Label objects for each sector
    lv_obj_t* center_circle;                         // Center circle object
    uint8_t sector_count;                            // Number of sectors
    lv_coord_t center_x;                             // Center X coordinate
    lv_coord_t center_y;                             // Center Y coordinate
    lv_coord_t radius;                               // Pie chart radius
} ui_pie_chart_t;



void tft_bl_ctrl(int8_t percent);
void ui_switch_next_page_cb();
void ui_switch_next_page_cb(uint8_t tp_evt);
void display_thread_entry(void *args);

#endif // DISPLAY_H
