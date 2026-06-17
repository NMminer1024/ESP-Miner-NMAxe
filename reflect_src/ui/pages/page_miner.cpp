#include "ui/pages/page_miner.h"

void PageMinerBase::destroy() {
    auto& m = AppState::instance().miner;
    m.hashrate.unsubscribe(_on_text);
    m.hashrate_unit.unsubscribe(_on_text);
    m.blk_hit.unsubscribe(_on_text);
    m.price.unsubscribe(_on_text);
    m.ver.unsubscribe(_on_text);
    m.power.unsubscribe(_on_text);
    m.ip.unsubscribe(_on_text);
    m.uptime_hms.unsubscribe(_on_text);
    m.uptime_day.unsubscribe(_on_text);
    m.diff.unsubscribe(_on_text);
    m.shares.unsubscribe(_on_text);
    m.temp.unsubscribe(_on_text);
    m.fan.unsubscribe(_on_text);
    m.utc_time.unsubscribe(_on_text);
    m.wifi_color.unsubscribe(_on_color);
    m.swarm_bd.unsubscribe(_on_text);
    m.swarm_hr.unsubscribe(_on_text);
    m.swarm_workers.unsubscribe(_on_text);

    _lb_hashrate = _lb_hr_unit = _lb_blk_hit = _lb_price = _lb_ver = nullptr;
    _lb_power = _lb_ip = _lb_uptime_hms = _lb_uptime_day = nullptr;
    _lb_diff = _lb_share = _lb_temp = _lb_fan = _lb_utc_time = _lb_wifi_symb = nullptr;
    _lb_swarm_bd = _lb_swarm_hr = _lb_swarm_wk = nullptr;
}

void PageMinerBase::_finish_create() {
    auto& m = AppState::instance().miner;
    m.hashrate.subscribe(_on_text, &_lb_hashrate);
    m.hashrate_unit.subscribe(_on_text, &_lb_hr_unit);
    m.blk_hit.subscribe(_on_text, &_lb_blk_hit);
    m.price.subscribe(_on_text, &_lb_price);
    m.ver.subscribe(_on_text, &_lb_ver);
    m.power.subscribe(_on_text, &_lb_power);
    m.ip.subscribe(_on_text, &_lb_ip);
    m.uptime_hms.subscribe(_on_text, &_lb_uptime_hms);
    m.uptime_day.subscribe(_on_text, &_lb_uptime_day);
    m.diff.subscribe(_on_text, &_lb_diff);
    m.shares.subscribe(_on_text, &_lb_share);
    m.temp.subscribe(_on_text, &_lb_temp);
    m.fan.subscribe(_on_text, &_lb_fan);
    m.utc_time.subscribe(_on_text, &_lb_utc_time);
    m.wifi_color.subscribe(_on_color, &_lb_wifi_symb);
    m.swarm_bd.subscribe(_on_text, &_lb_swarm_bd);
    m.swarm_hr.subscribe(_on_text, &_lb_swarm_hr);
    m.swarm_workers.subscribe(_on_text, &_lb_swarm_wk);
}

void PageMinerBase::_on_text(const String& v, void* ctx) {
    if (!ctx) return;
    lv_obj_t** lbl = static_cast<lv_obj_t**>(ctx);
    if (*lbl) lv_label_set_text(*lbl, v.c_str());
}

void PageMinerBase::_on_color(const uint32_t& v, void* ctx) {
    if (!ctx) return;
    lv_obj_t** lbl = static_cast<lv_obj_t**>(ctx);
    if (*lbl) lv_obj_set_style_text_color(*lbl, lv_color_hex(v), 0);
}
