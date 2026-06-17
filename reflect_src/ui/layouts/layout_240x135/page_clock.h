#pragma once
#include "ui/pages/page_clock.h"
#include "lvgl.h"

class PageClock240x135 : public PageClockBase {
public:
    void create(lv_obj_t* parent) override {
        _W = 240, _H = 135;
        lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(parent, 0, 0);
        lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
        _create_dynamic(parent);
        _finish_create();
    }
    const char* name() const override { return "clock240x135"; }
protected:
    void _create_dynamic(lv_obj_t* parent) override {
        _lb_time = lv_label_create(parent);
        lv_label_set_text(_lb_time, "--:--");
        lv_obj_set_style_text_font(_lb_time, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(_lb_time, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(_lb_time, LV_ALIGN_CENTER, 0, -10);

        _lb_date = lv_label_create(parent);
        lv_label_set_text(_lb_date, "----/--/--");
        lv_obj_set_style_text_font(_lb_date, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(_lb_date, lv_color_hex(0xA9A9A9), 0);
        lv_obj_align(_lb_date, LV_ALIGN_CENTER, 0, 40);
    }
};
