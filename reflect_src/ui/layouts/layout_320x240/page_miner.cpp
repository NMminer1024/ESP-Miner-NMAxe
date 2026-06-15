#include "page_miner.h"

#include "utils/logger/logger.h"

// ============================================================================
// create() ¡ª background + dynamic widgets + Observable subscriptions
// ============================================================================
void PageMiner320x240::create(lv_obj_t* parent) {
    _W = 320; _H = 240;

    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);

    _create_dynamic(parent);
    _finish_create();

    LOG_D("PageMiner320x240: created");
}

// ============================================================================
// _create_dynamic() ¡ª layout all labels at 320x240 coordinates
// ============================================================================
void PageMiner320x240::_create_dynamic(lv_obj_t* parent) {
    // TODO: Replace placeholder font references with actual font assets
    // when migrating from original display.cpp.

    // ©¤©¤ Top bar: hashrate ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    _lb_hasrate = lv_label_create(parent);
    lv_obj_set_width(_lb_hasrate, 200);
    lv_label_set_text(_lb_hasrate, "0.0");
    lv_obj_set_style_text_color(_lb_hasrate, lv_color_hex(0xEE7D30), 0);
    lv_obj_align(_lb_hasrate, LV_ALIGN_TOP_LEFT, 10, 10);

    _lb_hasrate_unit = lv_label_create(parent);
    lv_obj_set_width(_lb_hasrate_unit, 60);
    lv_label_set_text(_lb_hasrate_unit, "TH/s");
    lv_obj_set_style_text_color(_lb_hasrate_unit, lv_color_hex(0xEE7D30), 0);
    lv_obj_align(_lb_hasrate_unit, LV_ALIGN_TOP_LEFT, 215, 15);

    // ©¤©¤ Top bar: power ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    _lb_power = lv_label_create(parent);
    lv_obj_set_width(_lb_power, 100);
    lv_label_set_text(_lb_power, "0.0W");
    lv_obj_set_style_text_color(_lb_power, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(_lb_power, LV_ALIGN_TOP_RIGHT, -10, 10);

    // ©¤©¤ Top bar: temperatures ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    _lb_vcore_temp = lv_label_create(parent);
    lv_obj_set_width(_lb_vcore_temp, 80);
    lv_label_set_text(_lb_vcore_temp, "VC 0C");
    lv_obj_set_style_text_color(_lb_vcore_temp, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(_lb_vcore_temp, LV_ALIGN_TOP_LEFT, 10, 40);

    _lb_asic_temp = lv_label_create(parent);
    lv_obj_set_width(_lb_asic_temp, 80);
    lv_label_set_text(_lb_asic_temp, "AS 0C");
    lv_obj_set_style_text_color(_lb_asic_temp, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(_lb_asic_temp, LV_ALIGN_TOP_LEFT, 100, 40);

    // ©¤©¤ Time ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    _lb_time_str = lv_label_create(parent);
    lv_obj_set_width(_lb_time_str, 100);
    lv_label_set_text(_lb_time_str, "--:--");
    lv_obj_set_style_text_color(_lb_time_str, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(_lb_time_str, LV_ALIGN_TOP_RIGHT, -10, 40);

    // ©¤©¤ Uptime ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    _lb_uptime_day = lv_label_create(parent);
    lv_obj_set_width(_lb_uptime_day, 50);
    lv_label_set_text(_lb_uptime_day, "0d");
    lv_obj_set_style_text_color(_lb_uptime_day, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(_lb_uptime_day, LV_ALIGN_TOP_LEFT, 10, 70);

    _lb_uptime_hms = lv_label_create(parent);
    lv_obj_set_width(_lb_uptime_hms, 100);
    lv_label_set_text(_lb_uptime_hms, "00:00:00");
    lv_obj_set_style_text_color(_lb_uptime_hms, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(_lb_uptime_hms, LV_ALIGN_TOP_LEFT, 65, 70);

    // ©¤©¤ RSSI ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    _lb_rssi = lv_label_create(parent);
    lv_obj_set_width(_lb_rssi, 60);
    lv_label_set_text(_lb_rssi, "-60");
    lv_obj_set_style_text_color(_lb_rssi, lv_color_hex(0x00FF00), 0);
    lv_obj_align(_lb_rssi, LV_ALIGN_TOP_RIGHT, -10, 70);

    // ©¤©¤ Middle: difficulties ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    _lb_net_diff = lv_label_create(parent);
    lv_obj_set_width(_lb_net_diff, 150);
    lv_label_set_text(_lb_net_diff, "Net: ---");
    lv_obj_set_style_text_color(_lb_net_diff, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(_lb_net_diff, LV_ALIGN_TOP_LEFT, 10, 100);

    _lb_best_diff = lv_label_create(parent);
    lv_obj_set_width(_lb_best_diff, 150);
    lv_label_set_text(_lb_best_diff, "Best: ---");
    lv_obj_set_style_text_color(_lb_best_diff, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(_lb_best_diff, LV_ALIGN_TOP_RIGHT, -10, 100);

    // ©¤©¤ Middle: shares ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    _lb_shares = lv_label_create(parent);
    lv_obj_set_width(_lb_shares, 150);
    lv_label_set_text(_lb_shares, "Shares: 0/0");
    lv_obj_set_style_text_color(_lb_shares, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(_lb_shares, LV_ALIGN_TOP_LEFT, 10, 130);

    // ©¤©¤ Middle: block hits ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    _lb_blk_hit = lv_label_create(parent);
    lv_obj_set_width(_lb_blk_hit, 100);
    lv_label_set_text(_lb_blk_hit, "Hits: 0");
    lv_obj_set_style_text_color(_lb_blk_hit, lv_color_hex(0xEE7D30), 0);
    lv_obj_align(_lb_blk_hit, LV_ALIGN_TOP_RIGHT, -10, 130);

    // ©¤©¤ Middle: job count ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    _lb_job_count = lv_label_create(parent);
    lv_obj_set_width(_lb_job_count, 150);
    lv_label_set_text(_lb_job_count, "Jobs: 0");
    lv_obj_set_style_text_color(_lb_job_count, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(_lb_job_count, LV_ALIGN_TOP_LEFT, 10, 160);

    // ©¤©¤ Bottom: version ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    _lb_ip = lv_label_create(parent);
    lv_obj_set_width(_lb_ip, 200);
    lv_label_set_text(_lb_ip, "---.---.---.---");
    lv_obj_set_style_text_color(_lb_ip, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(_lb_ip, LV_ALIGN_BOTTOM_LEFT, 5, -5);

    _lb_ver = lv_label_create(parent);
    lv_obj_set_width(_lb_ver, 100);
    lv_label_set_text(_lb_ver, "---");
    lv_obj_set_style_text_color(_lb_ver, lv_color_hex(0x808080), 0);
    lv_obj_align(_lb_ver, LV_ALIGN_BOTTOM_RIGHT, -5, -5);

    // ©¤©¤ Swarm bar (bottom area) ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    _lb_swarm_workers = lv_label_create(parent);
    lv_obj_set_width(_lb_swarm_workers, 80);
    lv_label_set_text(_lb_swarm_workers, "W: 0");
    lv_obj_set_style_text_color(_lb_swarm_workers, lv_color_hex(0xEE7D30), 0);
    lv_obj_align(_lb_swarm_workers, LV_ALIGN_BOTTOM_LEFT, 5, -30);

    _lb_swarm_hr = lv_label_create(parent);
    lv_obj_set_width(_lb_swarm_hr, 120);
    lv_label_set_text(_lb_swarm_hr, "HR: 0");
    lv_obj_set_style_text_color(_lb_swarm_hr, lv_color_hex(0xEE7D30), 0);
    lv_obj_align(_lb_swarm_hr, LV_ALIGN_BOTTOM_MID, 0, -30);

    _lb_swarm_bd = lv_label_create(parent);
    lv_obj_set_width(_lb_swarm_bd, 100);
    lv_label_set_text(_lb_swarm_bd, "BD: 0");
    lv_obj_set_style_text_color(_lb_swarm_bd, lv_color_hex(0xEE7D30), 0);
    lv_obj_align(_lb_swarm_bd, LV_ALIGN_BOTTOM_RIGHT, -5, -30);
}

