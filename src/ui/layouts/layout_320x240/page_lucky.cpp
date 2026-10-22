#include "page_lucky.h"

#include "lvgl.h"
#include "ui/assets/images.h"

void PageLucky320x240::create(lv_obj_t* parent) {
    _W = 320;
    _H = 240;
    _parent = parent;
    _lb_hr_x = 95;
    _lb_hr_y = -4;
    _lb_hr_unit_x = 195;
    _lb_hr_unit_y = 23;
    _hr_font = &ds_digib_font_56;
    _hr_unit_font = &ds_digib_font_20;
    // ASIC share-distribution pie temporarily disabled — this page now shows
    // Lucky Statistics (session share-difficulty bars + network-difficulty
    // line). Pie code path kept intact; flip to true to restore.
    _show_asic_pie = false;
    _pie.center_x = 100;
    _pie.center_y = 65;
    _pie.radius = 53;
    _pie_cfg[0] = PieSectorConfig(90, lv_color_hex(0xA7A9AC), "A1\r25%");
    _pie_cfg[1] = PieSectorConfig(90, lv_color_hex(0xF05A28), "A2\r25%");
    _pie_cfg[2] = PieSectorConfig(90, lv_color_hex(0x00ADEF), "A3\r25%");
    _pie_cfg[3] = PieSectorConfig(90, lv_color_hex(0x2E3192), "A4\r25%");

    // Lucky Statistics chart geometry: full-width plot under the hashrate.
    // Session data (proximity_history, lost on reboot): 10 bars = session
    // top-3 pinned + newest samples, kept in chronological order.
    // _chart_x leaves a left gutter for the log Y-axis tick labels.
    _chart_x = 34;
    _chart_y = 68;
    _chart_w = 272;
    _chart_h = 150;
    _bar_gap = 6;
    _lucky_points = 10;      // 10 bars: session top-3 pinned + newest, chronological
    _lucky_axis_ticks = 6;
    _lucky_bar_values = true;   // show share difficulty above the medal bars

    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
    _create_dynamic(parent);
}

void PageLucky320x240::_create_dynamic(lv_obj_t* parent) {
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

    // Lucky Statistics title (top-left empty area) + legend. Chart itself is
    // created lazily in _on_update once geometry is set above.
    _lb_title = lv_label_create(parent);
    lv_label_set_text(_lb_title, "Lucky Statistics");
    lv_obj_set_style_text_font(_lb_title, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(_lb_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(_lb_title, LV_ALIGN_TOP_LEFT, 14, 14);

    _lb_legend = lv_label_create(parent);
    lv_label_set_text(_lb_legend, "");
    lv_obj_set_style_text_font(_lb_legend, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(_lb_legend, lv_color_hex(0x808080), LV_PART_MAIN);
    lv_obj_align(_lb_legend, LV_ALIGN_TOP_MID, 0, 224);
}
