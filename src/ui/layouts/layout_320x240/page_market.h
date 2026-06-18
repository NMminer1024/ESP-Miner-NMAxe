#pragma once
#include "ui/pages/page_market.h"
#include "lvgl.h"

class PageMarket320x240 : public PageMarketBase {
public:
    void create(lv_obj_t* parent) override {
        _W = 320, _H = 240;
        lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(parent, 0, 0);
        lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
        _create_dynamic(parent);
        _finish_create();
    }
    const char* name() const override { return "market320x240"; }
protected:
    void _create_dynamic(lv_obj_t* parent) override {
        _lb_symbol = lv_label_create(parent);
        lv_label_set_text(_lb_symbol, "---");
        lv_obj_set_style_text_font(_lb_symbol, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(_lb_symbol, lv_color_hex(0xEE7D30), 0);
        lv_obj_align(_lb_symbol, LV_ALIGN_CENTER, 0, -60);

        _lb_price = lv_label_create(parent);
        lv_label_set_text(_lb_price, "$--");
        lv_obj_set_style_text_font(_lb_price, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(_lb_price, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(_lb_price, LV_ALIGN_CENTER, 0, 0);

        _lb_change = lv_label_create(parent);
        lv_label_set_text(_lb_change, "--%");
        lv_obj_set_style_text_font(_lb_change, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(_lb_change, lv_color_hex(0xA9A9A9), 0);
        lv_obj_align(_lb_change, LV_ALIGN_CENTER, 0, 65);
    }
};
