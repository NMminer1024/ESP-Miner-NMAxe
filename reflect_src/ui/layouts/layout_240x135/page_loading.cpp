#include "page_loading.h"
#include "ui/assets/images.h"
#include "../../../version.h"
#include "../../../utils/logger/logger.h"

// Loading page — exact legacy layout (240x135 frame, mining/loading bg).
void PageLoading240x135::create(lv_obj_t* parent) {
    _W = 240; _H = 135;
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
    _create_dynamic(parent);
    _finish_create();
    LOG_D("PageLoading240x135: created");
}

void PageLoading240x135::_create_dynamic(lv_obj_t* parent) {
    // Background image
    lv_obj_t* bg = lv_img_create(parent);
    lv_img_set_src(bg, &loading_page_img_135_240);
    lv_obj_set_pos(bg, 0, 0);

    // Version (bottom-right, static)
    lv_obj_t* ver = lv_label_create(parent);
    lv_label_set_text(ver, BOARD_CURRENT_FW_VERSION);
    lv_obj_set_style_text_font(ver, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ver, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(ver, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    // Details (bottom-left, bound to loading.details)
    _lb_details = lv_label_create(parent);
    lv_obj_set_width(_lb_details, _W - 60);
    lv_label_set_text(_lb_details, "Initializing...");
    lv_obj_set_style_text_font(_lb_details, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lb_details, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(_lb_details, LV_LABEL_LONG_DOT);
    lv_obj_align(_lb_details, LV_ALIGN_BOTTOM_LEFT, 3, 0);

    // Progress bar (center, -20)
    _bar_progress = lv_bar_create(parent);
    lv_bar_set_range(_bar_progress, 0, 100);
    lv_bar_set_value(_bar_progress, 0, LV_ANIM_ON);
    lv_obj_set_size(_bar_progress, _W * 0.9, 5);
    lv_obj_set_style_bg_opa(_bar_progress, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_bar_progress, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(_bar_progress, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_align(_bar_progress, LV_ALIGN_CENTER, 0, -20);

    // IP / slogan (center, +13; "Make it better" until IP arrives)
    _lb_ip = lv_label_create(parent);
    lv_label_set_text(_lb_ip, "Make it better");
    lv_obj_set_style_text_font(_lb_ip, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lb_ip, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(_lb_ip, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(_lb_ip, LV_ALIGN_CENTER, 0, 13);
}
