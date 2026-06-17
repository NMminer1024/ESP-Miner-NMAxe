#pragma once
#include "ui/assets/images.h"
#include "ui/pages/page_hr_health.h"
#include "lvgl.h"

class PageHr_health320x240 : public PageHr_healthBase {
public:
    void create(lv_obj_t* parent) override {
        _W = 320, _H = 240;
        lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(parent, 0, 0);
        lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
        _create_dynamic(parent);
        _finish_create();
    }
    const char* name() const override { return "hr_health320x240"; }
protected:
    void _create_dynamic(lv_obj_t* parent) override {
        { lv_obj_t* _bg = lv_img_create(parent); lv_img_set_src(_bg, &status_page_img_240_320); lv_obj_set_pos(_bg, 0, 0); lv_obj_move_background(_bg); }
        lv_obj_t* title = lv_label_create(parent);
        lv_label_set_text(title, "Hashrate");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(0xEE7D30), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

        _lb_hashrate = lv_label_create(parent);
        lv_label_set_text(_lb_hashrate, "-.-- TH/s");
        lv_obj_set_style_text_font(_lb_hashrate, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(_lb_hashrate, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(_lb_hashrate, LV_ALIGN_CENTER, 0, -30);

        _lb_efficiency = lv_label_create(parent);
        lv_label_set_text(_lb_efficiency, "Eff: --- J/TH");
        lv_obj_set_style_text_font(_lb_efficiency, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(_lb_efficiency, lv_color_hex(0xA9A9A9), 0);
        lv_obj_align(_lb_efficiency, LV_ALIGN_CENTER, 0, 10);

        _lb_shares = lv_label_create(parent);
        lv_label_set_text(_lb_shares, "Shares: --/--");
        lv_obj_set_style_text_font(_lb_shares, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(_lb_shares, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(_lb_shares, LV_ALIGN_CENTER, 0, 35);

        _lb_best_diff = lv_label_create(parent);
        lv_label_set_text(_lb_best_diff, "Best: ---");
        lv_obj_set_style_text_font(_lb_best_diff, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(_lb_best_diff, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(_lb_best_diff, LV_ALIGN_CENTER, 0, 60);
    }
};
