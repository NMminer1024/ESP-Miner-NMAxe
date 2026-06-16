#include "page_loading.h"
#include "../../../utils/logger/logger.h"

void PageLoading240x135::create(lv_obj_t* parent) {
    _W = 135; _H = 240;
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
    _create_dynamic(parent);
    _finish_create();
    LOG_D("PageLoading240x135: created");
}

void PageLoading240x135::_create_dynamic(lv_obj_t* parent) {
    _bar_progress = lv_bar_create(parent);
    lv_obj_set_size(_bar_progress, 120, 15);
    lv_obj_set_style_bg_color(_bar_progress, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_bar_progress, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
    lv_bar_set_range(_bar_progress, 0, 100);
    lv_bar_set_value(_bar_progress, 0, LV_ANIM_OFF);
    lv_obj_align(_bar_progress, LV_ALIGN_CENTER, 0, 15);

    _lb_details = lv_label_create(parent);
    lv_obj_set_width(_lb_details, 130);
    lv_label_set_text(_lb_details, "Initializing...");
    lv_obj_set_style_text_color(_lb_details, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(_lb_details, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_lb_details, LV_ALIGN_CENTER, 0, -10);

    _lb_ip = lv_label_create(parent);
    lv_obj_set_width(_lb_ip, 130);
    lv_label_set_text(_lb_ip, "---.---.---.---");
    lv_obj_set_style_text_color(_lb_ip, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_align(_lb_ip, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_lb_ip, LV_ALIGN_CENTER, 0, 40);
}
