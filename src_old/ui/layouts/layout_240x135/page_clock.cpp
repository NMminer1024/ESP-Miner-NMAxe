#include "page_clock.h"

#include "lvgl.h"
#include "ui/assets/fonts.h"

static lv_obj_t* make_clock_label(lv_obj_t* parent, const lv_font_t* font, uint32_t color,
                                  lv_align_t align, lv_coord_t x, lv_coord_t y, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_align(label, align, x, y);
    lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
    return label;
}

void PageClock240x135::create(lv_obj_t* parent) {
    _W = 240;
    _H = 135;
    _parent = parent;
    _last_sync_ms = 0;
    _last_price_text = "";

    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
    _create_dynamic(parent);
}

void PageClock240x135::_create_dynamic(lv_obj_t* parent) {
    _lb_hr = make_clock_label(parent, &ds_digib_font_56, 0xEE7D30, LV_ALIGN_TOP_LEFT, 0, 0, " ");
    _lb_hr_unit = make_clock_label(parent, &ds_digib_font_20, 0x808080, LV_ALIGN_TOP_LEFT, 95, 26, " ");
    _lb_hits = make_clock_label(parent, &ds_digib_font_56, 0xEE7D30, LV_ALIGN_TOP_RIGHT, -30, 0, " ");
    lv_obj_set_width(_lb_hits, 75);
    _lb_hits_unit = make_clock_label(parent, &ds_digib_font_20, 0x808080, LV_ALIGN_TOP_LEFT, 190, 26, "hits");
    _lb_time = make_clock_label(parent, &ds_digib_font_56, 0xFFFFFF, LV_ALIGN_CENTER, 0, 15, "--:--");
    _lb_ampm = make_clock_label(parent, &lv_font_montserrat_14, 0xFFFFFF, LV_ALIGN_CENTER, 0, 15, "");
    _lb_date = make_clock_label(parent, &ds_digib_font_24, 0x808080, LV_ALIGN_BOTTOM_RIGHT, 0, 0, "----/--/--");
    _lb_price = make_clock_label(parent, &ds_digib_font_24, 0x808080, LV_ALIGN_BOTTOM_LEFT, 0, 0, "");
}
