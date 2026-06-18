#include "page_loading.h"
#include "ui/assets/images.h"
#include "../../../version.h"
#include "../../../utils/logger/logger.h"

void PageLoading320x240::create(lv_obj_t* parent) {
    _W = 320; _H = 240;
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
    _create_dynamic(parent);
    _finish_create();
    LOG_D("PageLoading320x240: created");
}

void PageLoading320x240::_create_dynamic(lv_obj_t* parent) {
    lv_obj_t* bg = lv_img_create(parent);
    lv_img_set_src(bg, &loading_page_img_240_320);
    lv_obj_set_pos(bg, 0, 0);

    lv_obj_t* ver = lv_label_create(parent);
    lv_label_set_text(ver, BOARD_CURRENT_FW_VERSION);
    lv_obj_set_style_text_font(ver, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ver, lv_color_hex(0xFFFFFF), 0);
    lv_coord_t ver_width = lv_txt_get_width(BOARD_CURRENT_FW_VERSION,
                                            strlen(BOARD_CURRENT_FW_VERSION),
                                            &lv_font_montserrat_16, 0, LV_TEXT_FLAG_NONE);
    lv_obj_set_width(ver, ver_width);
    lv_obj_align(ver, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    _lb_details = lv_label_create(parent);
    lv_obj_set_width(_lb_details, _W - ver_width);
    lv_label_set_text(_lb_details, "Initializing...");
    lv_obj_set_style_text_font(_lb_details, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lb_details, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(_lb_details, LV_LABEL_LONG_DOT);
    lv_obj_align(_lb_details, LV_ALIGN_BOTTOM_LEFT, 3, 0);

    _bar_progress = lv_bar_create(parent);
    lv_obj_add_flag(_bar_progress, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_bar_set_range(_bar_progress, 0, 100);
    lv_bar_set_value(_bar_progress, 0, LV_ANIM_ON);
    lv_obj_set_size(_bar_progress, _W * 0.9, 6);
    lv_obj_set_style_bg_opa(_bar_progress, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_bar_progress, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(_bar_progress, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_align(_bar_progress, LV_ALIGN_CENTER, 0, -20);

    _lb_progress = lv_label_create(parent);
    lv_label_set_text(_lb_progress, "0%");
    lv_obj_set_style_text_font(_lb_progress, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_lb_progress, lv_color_hex(0xFFFFFF), 0);

    _lb_ip = lv_label_create(parent);
    lv_label_set_text(_lb_ip, "Make it better");
    lv_obj_set_style_text_font(_lb_ip, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lb_ip, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(_lb_ip, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(_lb_ip, LV_ALIGN_CENTER, 0, 25);
    _ip_font = &lv_font_montserrat_20;
    _ip_max_width = _W;
    _set_scrolling_label_text(_lb_ip, "Make it better", _ip_font, _ip_max_width);

    _lb_pool = lv_label_create(parent);
    lv_label_set_text(_lb_pool, "");
    lv_obj_set_style_text_font(_lb_pool, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lb_pool, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(_lb_pool, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(_lb_pool, LV_ALIGN_CENTER, 0, 65);
    _pool_font = &lv_font_montserrat_20;
    _pool_max_width = _W;
    _set_scrolling_label_text(_lb_pool, "", _pool_font, _pool_max_width);
}
