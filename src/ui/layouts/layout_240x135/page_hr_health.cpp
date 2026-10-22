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
    _hr_font = &ds_digib_font_36;
    _hr_unit_font = &ds_digib_font_20;
    _show_asic_pie = false;

    // Lucky Statistics (session data, mirrors the AxeOS card): session Top-10
    // share samples by difficulty, sorted descending. Bars = share difficulty
    // (top-3 = gold/silver/bronze), red line = network difficulty. Lost on reboot.
    // _chart_x leaves a left gutter for the log Y-axis tick labels.
    _chart_x = 30;
    _chart_y = 52;
    _chart_w = 200;
    _chart_h = 58;
    _bar_gap = 3;
    _lucky_points = 10;      // 10 bars: session top-3 pinned + newest, chronological
    _lucky_axis_ticks = 4;
    _lucky_bar_values = false;   // too cramped for per-bar value labels on 135px

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

    // Tiny title in the dead space right of the hashrate unit.
    _lb_title = lv_label_create(parent);
    lv_label_set_text(_lb_title, "Lucky");
    lv_obj_set_style_text_font(_lb_title, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(_lb_title, lv_color_hex(0xFFC107), LV_PART_MAIN);
    lv_obj_align(_lb_title, LV_ALIGN_TOP_RIGHT, -4, 2);

    // Legend doubles as the block-solve warning strip.
    _lb_legend = lv_label_create(parent);
    lv_label_set_text(_lb_legend, "");
    lv_obj_set_style_text_font(_lb_legend, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(_lb_legend, lv_color_hex(0x808080), LV_PART_MAIN);
    lv_obj_align(_lb_legend, LV_ALIGN_TOP_MID, 0, 120);
}
