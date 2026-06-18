#pragma once
#include "ui/pages/page_clock.h"
#include "ui/assets/fonts.h"
#include "lvgl.h"

class PageClock320x240 : public PageClockBase {
public:
    void create(lv_obj_t* parent) override {
        _W = 320; _H = 240;
        lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(parent, 0, 0);
        lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
        _create_dynamic(parent);
        _finish_create();
    }
    const char* name() const override { return "clock320x240"; }
protected:
    lv_obj_t* _mk(lv_obj_t* p, const lv_font_t* f, uint32_t col, lv_align_t al,
                  lv_coord_t x, lv_coord_t y, const char* t) {
        lv_obj_t* l = lv_label_create(p);
        lv_label_set_text(l, t);
        lv_obj_set_style_text_font(l, f, LV_PART_MAIN);
        lv_obj_set_style_text_color(l, lv_color_hex(col), LV_PART_MAIN);
        lv_obj_align(l, al, x, y);
        lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);
        return l;
    }
    void _create_dynamic(lv_obj_t* parent) override {
        _lb_hr      = _mk(parent, &ds_digib_font_56, 0xFFFFFF, LV_ALIGN_TOP_LEFT,     0,   0,  " ");
        _lb_hr_unit = _mk(parent, &ds_digib_font_20, 0xFFFFFF, LV_ALIGN_TOP_LEFT,     100, 26, " ");
        _lb_hits    = _mk(parent, &ds_digib_font_56, 0xEE7D30, LV_ALIGN_TOP_RIGHT,    -10, 0,  " ");
        _mk(parent, &ds_digib_font_20, 0xFFFFFF, LV_ALIGN_TOP_LEFT, 280, 26, "hits");
        _lb_time    = _mk(parent, &ds_digib_font_120, 0xFFFFFF, LV_ALIGN_CENTER,      0,   0,  "--:--");
        _lb_date    = _mk(parent, &ds_digib_font_24, 0xA9A9A9, LV_ALIGN_BOTTOM_RIGHT, 0,   0,  "----/--/--");
        _lb_price   = _mk(parent, &ds_digib_font_24, 0xFFFFFF, LV_ALIGN_BOTTOM_LEFT,  0,   0,  "");
    }
};
