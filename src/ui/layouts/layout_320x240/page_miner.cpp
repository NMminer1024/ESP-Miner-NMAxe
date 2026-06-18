#include "page_miner.h"
#include "ui/assets/images.h"
#include "ui/assets/fonts.h"
#include "../../../version.h"
#include "../../../utils/logger/logger.h"

// Miner page (NMQAxe++, 320x240) — exact legacy element layout (incl. swarm bar).
void PageMiner320x240::create(lv_obj_t* parent) {
    _W = 320; _H = 240;
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
    _create_dynamic(parent);
    _finish_create();
    LOG_D("PageMiner320x240: created");
}

void PageMiner320x240::_create_dynamic(lv_obj_t* parent) {
    auto mk = [&](const lv_font_t* f, uint32_t col, lv_align_t al, lv_coord_t x, lv_coord_t y,
                  lv_coord_t w, const char* txt, lv_label_long_mode_t lm) -> lv_obj_t* {
        lv_obj_t* l = lv_label_create(parent);
        if (w > 0) lv_obj_set_width(l, w);
        lv_label_set_text(l, txt);
        lv_obj_set_style_text_font(l, f, LV_PART_MAIN);
        lv_obj_set_style_text_color(l, lv_color_hex(col), LV_PART_MAIN);
        lv_label_set_long_mode(l, lm);
        lv_obj_align(l, al, x, y);
        lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);
        return l;
    };
    const lv_coord_t SW = _W;

    lv_obj_t* bg = lv_img_create(parent);
    lv_img_set_src(bg, &mining_page_img_240_320);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t* logo = lv_img_create(parent);
    lv_img_set_src(logo, &logo_worker_nmqaxepp);
    lv_obj_align(logo, LV_ALIGN_TOP_LEFT, 70, 44);
    lv_obj_add_flag(logo, LV_OBJ_FLAG_EVENT_BUBBLE);

    _lb_blk_hit    = mk(&ds_digib_font_56, 0xEE7D30, LV_ALIGN_TOP_MID,    20, 65,  SW, " ", LV_LABEL_LONG_SCROLL_CIRCULAR);
    _lb_hashrate   = mk(&ds_digib_font_52, 0xFFFFFF, LV_ALIGN_BOTTOM_MID, 62, -44, 150," ", LV_LABEL_LONG_SCROLL_CIRCULAR);
    _lb_hr_unit    = mk(&ds_digib_font_24, 0xFFFFFF, LV_ALIGN_TOP_MID,    268,172, SW, " ", LV_LABEL_LONG_DOT);
    _lb_price      = mk(&ds_digib_font_24, 0xFFFFFF, LV_ALIGN_LEFT_MID,   60, 25,  SW, "",  LV_LABEL_LONG_DOT);
    _lb_ver        = mk(&ds_digib_font_18, 0xFFFFFF, LV_ALIGN_TOP_LEFT,   16, 38,  SW, &BOARD_CURRENT_FW_VERSION[1], LV_LABEL_LONG_DOT);
    _lb_power      = mk(&Inconsolata_26,   0xFFFFFF, LV_ALIGN_TOP_LEFT,   20, 169, (lv_coord_t)(SW/2.2), " ", LV_LABEL_LONG_DOT);
    _lb_ip         = mk(&ds_digib_font_18, 0xFFFFFF, LV_ALIGN_TOP_LEFT,   208,2,   (lv_coord_t)(SW/2.5), " ", LV_LABEL_LONG_SCROLL_CIRCULAR);
    _lb_uptime_hms = mk(&ds_digib_font_18, 0xFFFFFF, LV_ALIGN_TOP_LEFT,   123,2,   88, " ", LV_LABEL_LONG_DOT);
    _lb_uptime_day = mk(&ds_digib_font_18, 0xFFFFFF, LV_ALIGN_TOP_LEFT,   87, 2,   88, " ", LV_LABEL_LONG_DOT);
    _lb_diff       = mk(&ds_digib_font_24, 0xFFFFFF, LV_ALIGN_TOP_LEFT,   187,30,  (lv_coord_t)(SW/2.6), " ", LV_LABEL_LONG_SCROLL_CIRCULAR);
    _lb_share      = mk(&ds_digib_font_24, 0xFFFFFF, LV_ALIGN_TOP_LEFT,   187,55,  SW, " ", LV_LABEL_LONG_DOT);
    _lb_temp       = mk(&ds_digib_font_24, 0xFFFFFF, LV_ALIGN_TOP_LEFT,   187,83,  SW, " ", LV_LABEL_LONG_DOT);
    _lb_fan        = mk(&ds_digib_font_24, 0xFFFFFF, LV_ALIGN_TOP_LEFT,   187,110, SW, " ", LV_LABEL_LONG_DOT);
    _lb_utc_time   = mk(&ds_digib_font_18, 0xFFFFFF, LV_ALIGN_TOP_LEFT,   1,  3,   88, " ", LV_LABEL_LONG_DOT);

    // Swarm bar
    _lb_swarm_bd   = mk(&ds_digib_font_24, 0xFFFFFF, LV_ALIGN_TOP_LEFT, 3,   210, SW, " ", LV_LABEL_LONG_DOT);
    _lb_swarm_wk   = mk(&ds_digib_font_24, 0xFFFFFF, LV_ALIGN_TOP_LEFT, 145, 210, SW, " ", LV_LABEL_LONG_DOT);
    _lb_swarm_hr   = mk(&ds_digib_font_24, 0xFFFFFF, LV_ALIGN_TOP_LEFT, 237, 210, SW, " ", LV_LABEL_LONG_DOT);

    // Static labels / symbols
    mk(&lv_font_montserrat_16, 0xFFA500, LV_ALIGN_TOP_LEFT, 113, 2, 24, "d", LV_LABEL_LONG_DOT);
    mk(&lv_font_montserrat_16, 0xFFA500, LV_ALIGN_TOP_LEFT, 73,  1, SW, LV_SYMBOL_BELL, LV_LABEL_LONG_DOT);
    _lb_wifi_symb = mk(&lv_font_montserrat_16, 0xFFA500, LV_ALIGN_TOP_LEFT, 187, 1, SW, LV_SYMBOL_WIFI, LV_LABEL_LONG_DOT);
    mk(&symbol_20, 0xA9A9A9, LV_ALIGN_TOP_LEFT, 153, 30,  SW, "\xEF\x82\x80", LV_LABEL_LONG_DOT);
    mk(&symbol_20, 0xA9A9A9, LV_ALIGN_TOP_LEFT, 151, 55,  SW, "\xEF\x8E\x82", LV_LABEL_LONG_DOT);
    mk(&symbol_20, 0xA9A9A9, LV_ALIGN_TOP_LEFT, 160, 83,  SW, "\xEF\x8B\x88", LV_LABEL_LONG_DOT);
    mk(&symbol_20, 0xA9A9A9, LV_ALIGN_TOP_LEFT, 155, 110, SW, "\xEF\xA1\xA3", LV_LABEL_LONG_DOT);
}
