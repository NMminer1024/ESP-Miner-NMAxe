#pragma once
#include "ui/assets/images.h"
#include "ui/pages/page_config.h"
#include "lvgl.h"

class PageConfig240x135 : public PageConfigBase {
public:
    void create(lv_obj_t* parent) override {
        _W = 240, _H = 135;
        lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(parent, 0, 0);
        lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
        _create_dynamic(parent);
        _finish_create();
    }
    const char* name() const override { return "config240x135"; }
protected:
    void _create_dynamic(lv_obj_t* parent) override {
        { lv_obj_t* _bg = lv_img_create(parent); lv_img_set_src(_bg, &config_page_img_135_240); lv_obj_set_pos(_bg, 0, 0); lv_obj_move_background(_bg); }
        lv_obj_t* title = lv_label_create(parent);
        lv_label_set_text(title, "WiFi Setup");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(0xEE7D30), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

        _lb_ssid = lv_label_create(parent);
        lv_label_set_long_mode(_lb_ssid, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(_lb_ssid, _W - 16);
        lv_label_set_text(_lb_ssid, "SSID: ---");
        lv_obj_set_style_text_font(_lb_ssid, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(_lb_ssid, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(_lb_ssid, LV_ALIGN_CENTER, 0, -20);

        _lb_ip = lv_label_create(parent);
        lv_label_set_text(_lb_ip, "IP: ---");
        lv_obj_set_style_text_font(_lb_ip, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(_lb_ip, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(_lb_ip, LV_ALIGN_CENTER, 0, 10);

        _lb_timeout = lv_label_create(parent);
        lv_label_set_text(_lb_timeout, "");
        lv_obj_set_style_text_font(_lb_timeout, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(_lb_timeout, lv_color_hex(0xA9A9A9), 0);
        lv_obj_align(_lb_timeout, LV_ALIGN_BOTTOM_MID, 0, -12);
    }
};
