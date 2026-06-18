#include "page_setting.h"

#include "lvgl.h"
#include "ui/assets/fonts.h"
#include "app/application.h"
#include "nvs/nvs_config.h"

void PageSetting320x240::create(lv_obj_t* parent) {
    _W = 320;
    _H = 240;
    _parent = parent;
    lv_obj_set_scroll_dir(parent, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
    _create_dynamic(parent);
    _reload_from_runtime();
}

void PageSetting320x240::_create_dynamic(lv_obj_t* parent) {
    const lv_coord_t W = _W;
    const lv_coord_t pad = 4;
    const lv_coord_t lbl_w = 64;
    const lv_coord_t ctrl_w = W - lbl_w - pad * 3 - 20;
    const lv_coord_t ctrl_x = W - pad - ctrl_w;
    const lv_coord_t row_h = 28;
    const lv_coord_t drop_h = 28;
    const lv_font_t* font = &lv_font_montserrat_14;
    lv_coord_t y = pad;

    auto mk_label = [&](const char* txt, lv_coord_t yy) {
        lv_obj_t* l = lv_label_create(parent);
        lv_label_set_text(l, txt);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_pos(l, pad, yy);
        return l;
    };

    mk_label("Brightness", y + 9);
    _slider_brightness = lv_slider_create(parent);
    lv_obj_set_size(_slider_brightness, ctrl_w, 14);
    lv_obj_set_pos(_slider_brightness, ctrl_x, y + 7);
    lv_slider_set_range(_slider_brightness, 2, 100);
    lv_obj_add_event_cb(_slider_brightness, _slider_event_cb, LV_EVENT_VALUE_CHANGED, this);
    y += row_h + pad;

    mk_label("Vcore", y + (drop_h - 14) / 2);
    _dropdown_vcore = lv_dropdown_create(parent);
    lv_obj_set_size(_dropdown_vcore, ctrl_w, drop_h);
    lv_obj_set_pos(_dropdown_vcore, ctrl_x, y);
    lv_obj_set_style_text_font(_dropdown_vcore, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_pad_top(_dropdown_vcore, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(_dropdown_vcore, 4, LV_PART_MAIN);
    if (lv_obj_t* list = lv_dropdown_get_list(_dropdown_vcore)) {
        lv_obj_set_style_text_font(list, &lv_font_montserrat_20, LV_PART_MAIN);
    }
    y += drop_h + pad;

    mk_label("Frequency", y + (drop_h - 14) / 2);
    _dropdown_freq = lv_dropdown_create(parent);
    lv_obj_set_size(_dropdown_freq, ctrl_w, drop_h);
    lv_obj_set_pos(_dropdown_freq, ctrl_x, y);
    lv_obj_set_style_text_font(_dropdown_freq, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_pad_top(_dropdown_freq, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(_dropdown_freq, 4, LV_PART_MAIN);
    if (lv_obj_t* list = lv_dropdown_get_list(_dropdown_freq)) {
        lv_obj_set_style_text_font(list, &lv_font_montserrat_20, LV_PART_MAIN);
    }
    y += drop_h + pad;

    mk_label("Screen Saver", y + (drop_h - 14) / 2);
    _dropdown_saver = lv_dropdown_create(parent);
    lv_obj_set_size(_dropdown_saver, ctrl_w, drop_h);
    lv_obj_set_pos(_dropdown_saver, ctrl_x, y);
    lv_obj_set_style_text_font(_dropdown_saver, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_pad_top(_dropdown_saver, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(_dropdown_saver, 4, LV_PART_MAIN);
    lv_dropdown_set_options(_dropdown_saver, "never\n30s\n1m\n5m\n15m\n30m\n1h\n2h\n6h");
    if (lv_obj_t* list = lv_dropdown_get_list(_dropdown_saver)) {
        lv_obj_set_style_text_font(list, &lv_font_montserrat_20, LV_PART_MAIN);
    }
    y += drop_h + pad;

    _checkbox_auto_roll = lv_checkbox_create(parent);
    lv_checkbox_set_text(_checkbox_auto_roll, "Screen Roll");
    lv_obj_set_style_text_font(_checkbox_auto_roll, font, 0);
    lv_obj_set_style_text_color(_checkbox_auto_roll, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(_checkbox_auto_roll, pad, y + 4);
    lv_obj_add_event_cb(_checkbox_auto_roll, _checkbox_event_cb, LV_EVENT_VALUE_CHANGED, this);

    _checkbox_flip = lv_checkbox_create(parent);
    lv_checkbox_set_text(_checkbox_flip, "Flip Screen");
    lv_obj_set_style_text_font(_checkbox_flip, font, 0);
    lv_obj_set_style_text_color(_checkbox_flip, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(_checkbox_flip, W / 2, y + 4);
    lv_obj_add_event_cb(_checkbox_flip, _checkbox_event_cb, LV_EVENT_VALUE_CHANGED, this);
    y += row_h + pad;

    lv_coord_t btn_w = (W - pad * 3) / 2;
    lv_coord_t btn_h = 38;

    _btn_save = lv_btn_create(parent);
    lv_obj_set_size(_btn_save, btn_w, btn_h);
    lv_obj_set_pos(_btn_save, pad, y);
    {
        lv_obj_t* lbl = lv_label_create(_btn_save);
        lv_label_set_text(lbl, "Save");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(_btn_save, _button_event_cb, LV_EVENT_CLICKED, this);

    _btn_restart = lv_btn_create(parent);
    lv_obj_set_size(_btn_restart, btn_w, btn_h);
    lv_obj_set_pos(_btn_restart, pad * 2 + btn_w, y);
    {
        lv_obj_t* lbl = lv_label_create(_btn_restart);
        lv_label_set_text(lbl, "Restart");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(_btn_restart, _button_event_cb, LV_EVENT_CLICKED, this);
    y += btn_h + pad;

    _keyboard = lv_keyboard_create(parent);
    lv_obj_set_size(_keyboard, W, 120);
    lv_obj_align(_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_keyboard, _keyboard_event_cb, LV_EVENT_CANCEL, this);
    lv_obj_add_event_cb(_keyboard, _keyboard_event_cb, LV_EVENT_READY, this);
}
