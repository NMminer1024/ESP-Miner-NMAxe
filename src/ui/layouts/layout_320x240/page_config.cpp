#include "page_config.h"

#include "../../../version.h"
#include "ui/assets/images.h"

void PageConfig320x240::create(lv_obj_t* parent) {
    _W = 320;
    _H = 240;
    _parent = parent;
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
    _create_dynamic(parent);
}

void PageConfig320x240::_create_dynamic(lv_obj_t* parent) {
    lv_obj_t* bg = lv_img_create(parent);
    lv_img_set_src(bg, &black_page_img_240_320);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_move_background(bg);

    _lb_logo = lv_img_create(parent);
    lv_img_set_src(_lb_logo, &logo_worker_nmqaxepp);
    lv_obj_align(_lb_logo, LV_ALIGN_TOP_LEFT, 50, 50);
    lv_obj_add_flag(_lb_logo, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_HIDDEN);

    _lb_version = lv_label_create(parent);
    lv_obj_set_width(_lb_version, 80);
    lv_label_set_text(_lb_version, BOARD_CURRENT_FW_VERSION);
    lv_obj_set_style_text_font(_lb_version, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(_lb_version, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_label_set_long_mode(_lb_version, LV_LABEL_LONG_DOT);
    lv_obj_align(_lb_version, LV_ALIGN_BOTTOM_LEFT, 4, -4);

    _lb_timeout = lv_label_create(parent);
    lv_obj_set_width(_lb_timeout, 60);
    lv_label_set_text(_lb_timeout, "");
    lv_obj_set_style_text_font(_lb_timeout, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(_lb_timeout, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_align(_lb_timeout, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_label_set_long_mode(_lb_timeout, LV_LABEL_LONG_DOT);
    lv_obj_align(_lb_timeout, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
}
