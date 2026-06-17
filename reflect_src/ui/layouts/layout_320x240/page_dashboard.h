#pragma once
#include "ui/pages/page_dashboard.h"
#include "lvgl.h"

class PageDashboard320x240 : public PageDashboardBase {
public:
    void create(lv_obj_t* parent) override {
        _W = 240, _H = 320;
        lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(parent, 0, 0);
        lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
        _create_dynamic(parent);
        _finish_create();
    }
    const char* name() const override { return "dashboard320x240"; }
protected:
    lv_obj_t* _mkrow(lv_obj_t* parent, const char* init, lv_coord_t y) {
        lv_obj_t* l = lv_label_create(parent);
        lv_label_set_text(l, init);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 8, y);
        return l;
    }
    void _create_dynamic(lv_obj_t* parent) override {
        lv_obj_t* title = lv_label_create(parent);
        lv_label_set_text(title, "Dashboard");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(0xEE7D30), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
        lv_coord_t y = 20 + 36;
        _lb_power      = _mkrow(parent, "Power: --- W", y); y += 36;
        _lb_vbus       = _mkrow(parent, "Vbus:  --- V", y); y += 36;
        _lb_ibus       = _mkrow(parent, "Ibus:  --- A", y); y += 36;
        _lb_asic_temp  = _mkrow(parent, "ASIC:  --- C", y); y += 36;
        _lb_vcore_temp = _mkrow(parent, "VRM:   --- C", y); y += 36;
        _lb_freq       = _mkrow(parent, "Freq:  --- MHz", y); y += 36;
        _lb_vcore      = _mkrow(parent, "Vcore: --- mV", y);
    }
};
