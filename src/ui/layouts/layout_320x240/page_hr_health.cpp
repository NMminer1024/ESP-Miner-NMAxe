#include "page_hr_health.h"

#include "lvgl.h"
#include "ui/assets/images.h"

void PageHr_health320x240::create(lv_obj_t* parent) {
    _W = 320;
    _H = 240;
    _parent = parent;
    _lb_hr_x = 95;
    _lb_hr_y = -4;
    _lb_hr_unit_x = 195;
    _lb_hr_unit_y = 23;
    _lb_scale_x = 0;
    _lb_scale_y = 45;
    _chart_x = 15;
    _chart_y = 8;
    _chart_w = _W - 14;
    _chart_h = _H - 48;
    _hr_font = &ds_digib_font_56;
    _hr_unit_font = &ds_digib_font_20;
    _scale_font = &lv_font_montserrat_16;
    _chart_tick_font = &lv_font_montserrat_12;
    _show_asic_pie = true;
    _pie.center_x = 100;
    _pie.center_y = 65;
    _pie.radius = 53;
    _pie_cfg[0] = PieSectorConfig(90, lv_color_hex(0xA7A9AC), "A1\r25%");
    _pie_cfg[1] = PieSectorConfig(90, lv_color_hex(0xF05A28), "A2\r25%");
    _pie_cfg[2] = PieSectorConfig(90, lv_color_hex(0x00ADEF), "A3\r25%");
    _pie_cfg[3] = PieSectorConfig(90, lv_color_hex(0x2E3192), "A4\r25%");

    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
    _create_dynamic(parent);
}

void PageHr_health320x240::_create_dynamic(lv_obj_t* parent) {
    lv_obj_t* bg = lv_img_create(parent);
    lv_img_set_src(bg, &status_page_img_240_320);
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
