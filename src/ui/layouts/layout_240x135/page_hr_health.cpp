#include "page_hr_health.h"

#include "lvgl.h"
#include "ui/assets/images.h"

void PageHr_health240x135::create(lv_obj_t* parent) {
    _W = 240;
    _H = 135;
    _parent = parent;
    _lb_hr_x = 75;
    _lb_hr_y = -4;
    _lb_hr_unit_x = 138;
    _lb_hr_unit_y = 8;
    _lb_scale_x = -125;
    _lb_scale_y = 5;
    _chart_x = 15;
    _chart_y = 8;
    _chart_w = _W - 14;
    _chart_h = _H - 48;
    _hr_font = &ds_digib_font_36;
    _hr_unit_font = &ds_digib_font_20;
    _scale_font = &lv_font_montserrat_14;
    _chart_tick_font = &lv_font_montserrat_10;
    _show_asic_pie = false;

    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
    _create_dynamic(parent);
}

void PageHr_health240x135::_create_dynamic(lv_obj_t* parent) {
    lv_obj_t* bg = lv_img_create(parent);
    lv_img_set_src(bg, &status_page_img_135_240);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_move_background(bg);

    _lb_hr = lv_label_create(parent);
    lv_obj_set_width(_lb_hr, _W / 2);
    lv_label_set_text(_lb_hr, " ");
    lv_obj_set_style_text_font(_lb_hr, _hr_font, LV_PART_MAIN);
    lv_obj_set_style_text_color(_lb_hr, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_label_set_long_mode(_lb_hr, LV_LABEL_LONG_DOT);
    lv_obj_align(_lb_hr, LV_ALIGN_TOP_MID, _lb_hr_x, _lb_hr_y);

    _lb_hr_unit = lv_label_create(parent);
    lv_obj_set_width(_lb_hr_unit, _W / 2);
    lv_label_set_text(_lb_hr_unit, " ");
    lv_obj_set_style_text_font(_lb_hr_unit, _hr_unit_font, LV_PART_MAIN);
    lv_obj_set_style_text_color(_lb_hr_unit, lv_color_hex(0x808080), LV_PART_MAIN);
    lv_label_set_long_mode(_lb_hr_unit, LV_LABEL_LONG_DOT);
    lv_obj_align(_lb_hr_unit, LV_ALIGN_TOP_MID, _lb_hr_unit_x, _lb_hr_unit_y);

    const BoardSpecConfig& spec = MinerApp::instance().spec();
    uint16_t scale = (uint16_t)(spec.ui.hashrate_dist_page.max_x_hr / spec.ui.hashrate_dist_page.max_x_bars);
    String scale_str = "Scale : " + String(scale) + " GH/s";
    uint16_t width = lv_txt_get_width(scale_str.c_str(), scale_str.length(), _scale_font, 0, LV_TEXT_FLAG_NONE);
    _lb_scale = lv_label_create(parent);
    lv_obj_set_width(_lb_scale, width);
    lv_label_set_text(_lb_scale, scale_str.c_str());
    lv_obj_set_style_text_font(_lb_scale, _scale_font, LV_PART_MAIN);
    lv_obj_set_style_text_color(_lb_scale, lv_color_hex(0xFFA500), LV_PART_MAIN);
    lv_label_set_long_mode(_lb_scale, LV_LABEL_LONG_DOT);
    lv_obj_align(_lb_scale, LV_ALIGN_TOP_RIGHT, _lb_scale_x, _lb_scale_y);

    _create_chart();
}
