#pragma once
#include "ui/pages/page_dashboard.h"
#include "lvgl.h"

class Pagedashboard240x320 : public PagedashboardBase {
public:
    void create(lv_obj_t* parent) override {
        _W = 240, _H = 320.Replace('W = ','').Replace(', _H = ','');
        lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(parent, 0, 0);
        lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
        _create_dynamic(parent);
    }
    const char* name() const override { return "dashboard240x320"; }
protected:
    void _create_dynamic(lv_obj_t* parent) override {
        _lb_details = lv_label_create(parent);
        lv_label_set_text(_lb_details, "dashboard (TODO)");
        lv_obj_set_style_text_color(_lb_details, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(_lb_details);
    }
};
