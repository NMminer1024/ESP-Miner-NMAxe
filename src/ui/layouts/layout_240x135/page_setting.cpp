#include "page_setting.h"

#include "lvgl.h"
#include "ui/assets/fonts.h"

void PageSetting240x135::create(lv_obj_t* parent) {
    _W = 240;
    _H = 135;
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
    _create_dynamic(parent);
}

void PageSetting240x135::_create_dynamic(lv_obj_t* parent) {
    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, "SWARM");
    lv_obj_set_style_text_font(title, &Inconsolata_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

    const lv_coord_t W = _W;
    const lv_coord_t right_pad = 5;
    const lv_coord_t col1_x = 5;
    const lv_coord_t col2_x = 122;
    const lv_coord_t col_left_w = col2_x - col1_x - 4;
    const lv_coord_t right_col_w = W - col2_x - right_pad;
    const lv_coord_t row1_lbl_y = 26;
    const lv_coord_t row1_data_y = 38;
    const lv_coord_t row2_lbl_y = 74;
    const lv_coord_t row2_data_y = 86;
    const lv_coord_t unit_y_off = 12;

    auto mk_sub = [&](const char* txt, lv_coord_t x, lv_coord_t y, lv_coord_t w) {
        lv_obj_t* l = lv_label_create(parent);
        lv_label_set_text(l, txt);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_width(l, w);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(l, x, y);
        return l;
    };

    auto mk_val = [&](lv_coord_t x, lv_coord_t y, lv_coord_t w) {
        lv_obj_t* l = lv_label_create(parent);
        lv_label_set_text(l, "0");
        lv_obj_set_style_text_font(l, &ds_digib_font_32, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0xEE7D30), 0);
        lv_obj_set_width(l, w);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(l, x, y);
        return l;
    };

    mk_sub("Workers", col1_x, row1_lbl_y, col_left_w);
    _lb_workers = mk_val(col1_x, row1_data_y, col_left_w);

    mk_sub("Hashrate", col2_x, row1_lbl_y, right_col_w);
    _lb_total_hr = mk_val(col2_x, row1_data_y, right_col_w);

    lv_obj_t* lb_hr_unit = lv_label_create(parent);
    lv_label_set_text(lb_hr_unit, "H/s");
    lv_obj_set_style_text_font(lb_hr_unit, &Inconsolata_16, 0);
    lv_obj_set_style_text_color(lb_hr_unit, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_pos(lb_hr_unit, W - right_pad - 23, row1_data_y + unit_y_off - 1);

    mk_sub("Best Session", col1_x, row2_lbl_y, col_left_w);
    _lb_best_session = mk_val(col1_x, row2_data_y, col_left_w);

    mk_sub("Best Ever", col2_x, row2_lbl_y, right_col_w);
    _lb_best_ever = mk_val(col2_x, row2_data_y, right_col_w);
}
