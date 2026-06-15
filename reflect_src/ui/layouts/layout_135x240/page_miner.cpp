#include "page_miner.h"
#include "utils/logger/logger.h"

void PageMiner135x240::create(lv_obj_t* parent) {
    _W = 135; _H = 240;
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
    _create_dynamic(parent);
    _finish_create();
    LOG_D("PageMiner135x240: created");
}

void PageMiner135x240::_create_dynamic(lv_obj_t* parent) {
    // TODO: layout for 135x240 (NMAXE / NMAXE_GAMMA)
    _lb_hasrate = lv_label_create(parent);
    lv_obj_set_width(_lb_hasrate, 120);
    lv_label_set_text(_lb_hasrate, "0.0");
    lv_obj_set_style_text_color(_lb_hasrate, lv_color_hex(0xEE7D30), 0);
    lv_obj_align(_lb_hasrate, LV_ALIGN_TOP_MID, 0, 5);

    _lb_hasrate_unit = lv_label_create(parent);
    lv_obj_set_width(_lb_hasrate_unit, 50);
    lv_label_set_text(_lb_hasrate_unit, "TH/s");
    lv_obj_set_style_text_color(_lb_hasrate_unit, lv_color_hex(0xEE7D30), 0);
    lv_obj_align(_lb_hasrate_unit, LV_ALIGN_TOP_MID, 45, 10);

    _lb_power = lv_label_create(parent);
    lv_obj_set_width(_lb_power, 100);
    lv_label_set_text(_lb_power, "0.0W");
    lv_obj_set_style_text_color(_lb_power, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(_lb_power, LV_ALIGN_TOP_MID, 0, 30);

    _lb_asic_temp = lv_label_create(parent);
    lv_obj_set_width(_lb_asic_temp, 120);
    lv_label_set_text(_lb_asic_temp, "ASIC 0C");
    lv_obj_set_style_text_color(_lb_asic_temp, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(_lb_asic_temp, LV_ALIGN_TOP_MID, 0, 50);

    _lb_vcore_temp = lv_label_create(parent);
    lv_obj_set_width(_lb_vcore_temp, 120);
    lv_label_set_text(_lb_vcore_temp, "VCORE 0C");
    lv_obj_set_style_text_color(_lb_vcore_temp, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(_lb_vcore_temp, LV_ALIGN_TOP_MID, 0, 70);

    _lb_net_diff = lv_label_create(parent);
    lv_obj_set_width(_lb_net_diff, 120);
    lv_label_set_text(_lb_net_diff, "Net: ---");
    lv_obj_set_style_text_color(_lb_net_diff, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(_lb_net_diff, LV_ALIGN_TOP_MID, 0, 95);

    _lb_best_diff = lv_label_create(parent);
    lv_obj_set_width(_lb_best_diff, 120);
    lv_label_set_text(_lb_best_diff, "Best: ---");
    lv_obj_set_style_text_color(_lb_best_diff, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(_lb_best_diff, LV_ALIGN_TOP_MID, 0, 115);

    _lb_shares = lv_label_create(parent);
    lv_obj_set_width(_lb_shares, 120);
    lv_label_set_text(_lb_shares, "Shares: 0/0");
    lv_obj_set_style_text_color(_lb_shares, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(_lb_shares, LV_ALIGN_TOP_MID, 0, 135);

    _lb_blk_hit = lv_label_create(parent);
    lv_obj_set_width(_lb_blk_hit, 120);
    lv_label_set_text(_lb_blk_hit, "Hits: 0");
    lv_obj_set_style_text_color(_lb_blk_hit, lv_color_hex(0xEE7D30), 0);
    lv_obj_align(_lb_blk_hit, LV_ALIGN_TOP_MID, 0, 155);

    _lb_ip = lv_label_create(parent);
    lv_obj_set_width(_lb_ip, 130);
    lv_label_set_text(_lb_ip, "---.---.---.---");
    lv_obj_set_style_text_color(_lb_ip, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(_lb_ip, LV_ALIGN_BOTTOM_MID, 0, -5);

    _lb_ver = lv_label_create(parent);
    lv_obj_set_width(_lb_ver, 100);
    lv_label_set_text(_lb_ver, "---");
    lv_obj_set_style_text_color(_lb_ver, lv_color_hex(0x808080), 0);
    lv_obj_align(_lb_ver, LV_ALIGN_BOTTOM_MID, 0, -25);
}
