#include "page_miner.h"
#include "ui/assets/images.h"
#include "ui/assets/fonts.h"
#include "app/application.h"
#include "board/board.h"
#include "../../../version.h"
#include "../../../utils/logger/logger.h"

// Miner page (NMAxe / Gamma, 240x135) — exact legacy element layout.
void PageMiner240x135::create(lv_obj_t* parent) {
    _W = 240; _H = 135;
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
    _create_dynamic(parent);
    _finish_create();
    LOG_D("PageMiner240x135: created");
}

void PageMiner240x135::_create_dynamic(lv_obj_t* parent) {
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

    // Background + worker logo
    lv_obj_t* bg = lv_img_create(parent);
    lv_img_set_src(bg, &mining_page_img_135_240);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t* logo = lv_img_create(parent);
    lv_img_set_src(logo,
        MinerApp::instance().spec().name == BOARD_NMAXE_GAMMA_NAME
            ? &logo_worker_nmaxegamma
            : &logo_worker_nmaxe);
    lv_obj_align(logo, LV_ALIGN_TOP_LEFT, 45, 20);
    lv_obj_add_flag(logo, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Value labels
    _lb_blk_hit    = mk(&ds_digib_font_56, 0xEE7D30, LV_ALIGN_TOP_MID,    6,  36, SW, " ", LV_LABEL_LONG_SCROLL_CIRCULAR);
    _lb_hashrate   = mk(&ds_digib_font_38, 0xEE7D30, LV_ALIGN_BOTTOM_MID, 50, 0,  100," ", LV_LABEL_LONG_SCROLL_CIRCULAR);
    _lb_hr_unit    = mk(&ds_digib_font_28, 0xFFFFFF, LV_ALIGN_TOP_MID,    182,110,SW, " ", LV_LABEL_LONG_DOT);
    _lb_price      = mk(&ds_digib_font_20, 0xFFFFFF, LV_ALIGN_LEFT_MID,   33, 29, SW, "",  LV_LABEL_LONG_DOT);
    _lb_ver        = mk(&ds_digib_font_16, 0xFFFFFF, LV_ALIGN_TOP_LEFT,   2,  22, SW, &BOARD_CURRENT_FW_VERSION[1], LV_LABEL_LONG_DOT);
    _lb_power      = mk(&Inconsolata_18,   0xFFFFFF, LV_ALIGN_TOP_LEFT,   16, 114,(lv_coord_t)(SW/2.5), " ", LV_LABEL_LONG_DOT);
    _lb_ip         = mk(&ds_digib_font_16, 0xFFFFFF, LV_ALIGN_TOP_LEFT,   140,2,  (lv_coord_t)(SW/2.5), " ", LV_LABEL_LONG_SCROLL_CIRCULAR);
    _lb_uptime_hms = mk(&ds_digib_font_16, 0xFFFFFF, LV_ALIGN_TOP_LEFT,   65, 2,  88, " ", LV_LABEL_LONG_DOT);
    _lb_uptime_day = mk(&ds_digib_font_16, 0xFFFFFF, LV_ALIGN_TOP_LEFT,   32, 2,  88, " ", LV_LABEL_LONG_DOT);
    _lb_diff       = mk(&ds_digib_font_18, 0xFFFFFF, LV_ALIGN_TOP_LEFT,   132,25, (lv_coord_t)(SW/2.4), " ", LV_LABEL_LONG_SCROLL_CIRCULAR);
    _lb_share      = mk(&ds_digib_font_18, 0xFFFFFF, LV_ALIGN_TOP_LEFT,   132,41, SW, " ", LV_LABEL_LONG_DOT);
    _lb_temp       = mk(&ds_digib_font_18, 0xFFFFFF, LV_ALIGN_TOP_LEFT,   132,58, SW, " ", LV_LABEL_LONG_DOT);
    _lb_fan        = mk(&ds_digib_font_18, 0xFFFFFF, LV_ALIGN_TOP_LEFT,   132,75, SW, " ", LV_LABEL_LONG_DOT);

    // Static labels / symbols
    mk(&lv_font_montserrat_14, 0xFFA500, LV_ALIGN_TOP_LEFT, 56, 2, 20, "d", LV_LABEL_LONG_DOT);       // uptime day unit
    mk(&lv_font_montserrat_14, 0xFFA500, LV_ALIGN_TOP_MID,  18, 1, SW, LV_SYMBOL_BELL, LV_LABEL_LONG_DOT);  // uptime symbol
    _lb_wifi_symb  = mk(&lv_font_montserrat_14, 0xFFA500, LV_ALIGN_TOP_MID, 123, 1, SW, LV_SYMBOL_WIFI, LV_LABEL_LONG_DOT);
    _lb_diff_symb  = mk(&symbol_14, 0xA9A9A9, LV_ALIGN_TOP_MID, 108, 26, SW, "\xEF\x82\x80", LV_LABEL_LONG_DOT); // diff
    _lb_share_symb = mk(&symbol_14, 0xA9A9A9, LV_ALIGN_TOP_MID, 108, 42, SW, "\xEF\x8E\x82", LV_LABEL_LONG_DOT); // share
    _lb_temp_symb  = mk(&symbol_14, 0xA9A9A9, LV_ALIGN_TOP_MID, 113, 59, SW, "\xEF\x8B\x88", LV_LABEL_LONG_DOT); // temp
    _lb_fan_symb   = mk(&symbol_14, 0xA9A9A9, LV_ALIGN_TOP_MID, 110, 76, SW, "\xEF\xA1\xA3", LV_LABEL_LONG_DOT); // fan
}
