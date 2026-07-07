#include "page_dashboard.h"

#include "lvgl.h"
#include "ui/assets/images.h"

void PageDashboard240x135::create(lv_obj_t* parent) {
    _W = 240;
    _H = 135;
    _parent = parent;
    _show_miner_diff = false;
    _show_vcore_temp_ring = false;
    _miner_img_dsc = nullptr;
    _img_miner_x = 0;
    _img_miner_y = 60;
    _lb_hr_x = 75;
    _lb_hr_y = -4;
    _lb_hr_unit_x = 138;
    _lb_hr_unit_y = 8;
    _ring_angle_full = 230;

    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
    _create_dynamic(parent);
}

void PageDashboard240x135::_create_dynamic(lv_obj_t* parent) {
    lv_obj_t* bg = lv_img_create(parent);
    lv_img_set_src(bg, &status_page_img_135_240);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_move_background(bg);

    _lb_hr = lv_label_create(parent);
    lv_obj_set_width(_lb_hr, _W / 2);
    lv_label_set_text(_lb_hr, " ");
    lv_obj_set_style_text_font(_lb_hr, &ds_digib_font_36, LV_PART_MAIN);
    lv_obj_set_style_text_color(_lb_hr, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_label_set_long_mode(_lb_hr, LV_LABEL_LONG_DOT);
    lv_obj_align(_lb_hr, LV_ALIGN_TOP_MID, _lb_hr_x, _lb_hr_y);

    _lb_hr_unit = lv_label_create(parent);
    lv_obj_set_width(_lb_hr_unit, _W / 2);
    lv_label_set_text(_lb_hr_unit, " ");
    lv_obj_set_style_text_font(_lb_hr_unit, &ds_digib_font_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(_lb_hr_unit, lv_color_hex(0x808080), LV_PART_MAIN);
    lv_label_set_long_mode(_lb_hr_unit, LV_LABEL_LONG_DOT);
    lv_obj_align(_lb_hr_unit, LV_ALIGN_TOP_MID, _lb_hr_unit_x, _lb_hr_unit_y);

    const uint8_t arc_r = 30;
    const uint8_t arc_line_width = 8;
    _ring_oc.cfg = {
        0, 4, arc_r, arc_line_width, 0, 0, _ring_angle_full,
        lv_color_hex(0xC0C0C0), lv_color_hex(0x0080FF),
        " ", &lv_font_montserrat_14, lv_color_hex(0xFFFFFF),
        "OC", &lv_font_montserrat_14, lv_color_hex(0xD3D3D3)
    };
    _ring_pwr.cfg = {
        (lv_coord_t)(2 * arc_r + 10), 4, arc_r, arc_line_width, 0, 0, _ring_angle_full,
        lv_color_hex(0xC0C0C0), lv_color_hex(0x0080FF),
        " ", &lv_font_montserrat_14, lv_color_hex(0xFFFFFF),
        "Power", &lv_font_montserrat_14, lv_color_hex(0xD3D3D3)
    };
    _ring_vc_req.cfg = {
        0, (lv_coord_t)(4 + 2 * arc_r + 8), arc_r, arc_line_width, 0, 0, _ring_angle_full,
        lv_color_hex(0xC0C0C0), lv_color_hex(0x0080FF),
        " ", &lv_font_montserrat_14, lv_color_hex(0xFFFFFF),
        "Vc req", &lv_font_montserrat_14, lv_color_hex(0xD3D3D3)
    };
    _ring_vc_real.cfg = {
        (lv_coord_t)(2 * arc_r + 10), (lv_coord_t)(4 + 2 * arc_r + 8), arc_r, arc_line_width, 0, 0, _ring_angle_full,
        lv_color_hex(0xC0C0C0), lv_color_hex(0x0080FF),
        " ", &lv_font_montserrat_14, lv_color_hex(0xFFFFFF),
        "Vc real", &lv_font_montserrat_14, lv_color_hex(0xD3D3D3)
    };
    _ring_asic_temp.cfg = {
        140, 40, (lv_coord_t)(3 * arc_r / 2), (lv_coord_t)(arc_line_width * 2), 0, 0, _ring_angle_full,
        lv_color_hex(0xC0C0C0), lv_color_hex(0x0080FF),
        " ", &lv_font_montserrat_20, lv_color_hex(0xFFFFFF),
        "ASIC Temp", &lv_font_montserrat_14, lv_color_hex(0xD3D3D3)
    };
}
