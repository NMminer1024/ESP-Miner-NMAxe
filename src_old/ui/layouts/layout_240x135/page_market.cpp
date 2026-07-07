#include "page_market.h"

#include "lvgl.h"
#include "ui/assets/fonts.h"
#include "ui/assets/images.h"

void PageMarket240x135::create(lv_obj_t* parent) {
    _W = 240;
    _H = 135;
    _parent = parent;
    _coin_font = &Inconsolata_18;
    _hr_font = &ds_digib_font_32;
    _hr_unit_font = &Inconsolata_16;
    _version_font = &lv_font_montserrat_14;
    _col_dollar_x = 73;
    _col_price_x = 86;
    _col_change_x = 185;
    _version_x = 2;
    _version_y = -3;
    _last_update_ms = 0;
    _last_switch_ms = 0;
    _max_per_page = 0;
    _total_pages = 1;
    _cur_page = 0;
    _inited = false;

    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
    _create_dynamic(parent);
}

void PageMarket240x135::_create_dynamic(lv_obj_t* parent) {
    _bg = lv_img_create(parent);
    lv_img_set_src(_bg, &black_page_img_135_240);
    lv_obj_set_pos(_bg, 0, 0);
    lv_obj_move_background(_bg);
}
