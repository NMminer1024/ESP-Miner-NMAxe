#pragma once
#include "ui/pages/page_setting.h"
#include "lvgl.h"

class PageSetting240x135 : public PageSettingBase {
public:
    void create(lv_obj_t* parent) override {
        _W = 135, _H = 240;
        lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(parent, 0, 0);
        lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
        _create_dynamic(parent);
        _finish_create();
    }
    const char* name() const override { return "setting240x135"; }
protected:
    lv_obj_t* _mkrow(lv_obj_t* parent, const char* init, lv_coord_t y) {
        lv_obj_t* l = lv_label_create(parent);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(l, _W - 16);
        lv_label_set_text(l, init);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 8, y);
        return l;
    }
    void _create_dynamic(lv_obj_t* parent) override {
        lv_obj_t* title = lv_label_create(parent);
        lv_label_set_text(title, "Swarm");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(0xEE7D30), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);
        lv_coord_t y = 16 + 30;
        _lb_workers   = _mkrow(parent, "Workers: -",      y); y += 30;
        _lb_total_hr  = _mkrow(parent, "HR: --- ",        y); y += 30;
        _lb_best_diff = _mkrow(parent, "Best: ---",       y); y += 30;
        _lb_neighbors = _mkrow(parent, "Neighbors: -",    y); y += 30;
        _lb_ip        = _mkrow(parent, "IP: ---",         y);
    }
};
