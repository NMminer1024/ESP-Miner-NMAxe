#include "ui/pages/page_miner.h"

// ============================================================================
// destroy() ¡ª unsubscribe all Observables, null widget pointers
// ============================================================================
void PageMinerBase::destroy() {
    auto& m = AppState::instance().miner;

    m.time_str.unsubscribe(_on_text);
    m.uptime_day.unsubscribe(_on_text);
    m.uptime_hms.unsubscribe(_on_text);
    m.rssi.unsubscribe(_on_text);
    m.rssi_icon_color.unsubscribe(_on_color);
    m.price.unsubscribe(_on_text);
    m.price.unsubscribe(_on_color);
    m.job_count.unsubscribe(_on_text);
    m.job_icon_color.unsubscribe(_on_color);
    m.net_diff.unsubscribe(_on_text);
    m.local_diff.unsubscribe(_on_text);
    m.local_diff_icon_color.unsubscribe(_on_color);
    m.shares.unsubscribe(_on_text);
    m.shares_icon_color.unsubscribe(_on_color);
    m.blk_hit.unsubscribe(_on_text);
    m.hashrate.unsubscribe(_on_text);
    m.ver.unsubscribe(_on_text);
    m.ip.unsubscribe(_on_text);
    m.swarm_bd.unsubscribe(_on_text);
    m.swarm_hr.unsubscribe(_on_text);
    m.swarm_workers.unsubscribe(_on_text);

    // Widgets are destroyed with the parent tile; null pointers here is sufficient.
    _lb_time_str = _lb_uptime_day = _lb_uptime_hms = nullptr;
    _lb_hasrate = _lb_hasrate_unit = _lb_power = nullptr;
    _lb_vcore_temp = _lb_asic_temp = nullptr;
    _lb_net_diff = _lb_best_diff = nullptr;
    _lb_shares = _lb_blk_hit = _lb_job_count = nullptr;
    _lb_ver = _lb_ip = _lb_rssi = nullptr;
    _lb_swarm_bd = _lb_swarm_hr = _lb_swarm_workers = nullptr;
}

// ============================================================================
// _finish_create() ¡ª subscribe all Observables with widget pointers as ctx
// ============================================================================
void PageMinerBase::_finish_create() {
    auto& m = AppState::instance().miner;

    m.time_str.subscribe(_on_text, &_lb_time_str);
    m.uptime_day.subscribe(_on_text, &_lb_uptime_day);
    m.uptime_hms.subscribe(_on_text, &_lb_uptime_hms);
    m.rssi.subscribe(_on_text, &_lb_rssi);
    m.ip.subscribe(_on_text, &_lb_ip);
    m.ver.subscribe(_on_text, &_lb_ver);
    m.hashrate.subscribe(_on_text, &_lb_hasrate);
    m.blk_hit.subscribe(_on_text, &_lb_blk_hit);
    m.job_count.subscribe(_on_text, &_lb_job_count);
    m.net_diff.subscribe(_on_text, &_lb_net_diff);
    m.local_diff.subscribe(_on_text, &_lb_best_diff);
    m.shares.subscribe(_on_text, &_lb_shares);
    m.swarm_bd.subscribe(_on_text, &_lb_swarm_bd);
    m.swarm_hr.subscribe(_on_text, &_lb_swarm_hr);
    m.swarm_workers.subscribe(_on_text, &_lb_swarm_workers);

    // ©¤©¤ Colors ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    m.rssi_icon_color.subscribe(_on_color, &_lb_rssi);
    m.job_icon_color.subscribe(_on_color, &_lb_job_count);
    m.local_diff_icon_color.subscribe(_on_color, &_lb_best_diff);
    m.shares_icon_color.subscribe(_on_color, &_lb_shares);
}

// ============================================================================
// _on_text ¡ª static Observable callback: set label text
// ============================================================================
void PageMinerBase::_on_text(const String& v, void* ctx) {
    if (!ctx) return;
    lv_obj_t** lbl = static_cast<lv_obj_t**>(ctx);
    if (*lbl) lv_label_set_text(*lbl, v.c_str());
}

// ============================================================================
// _on_color ¡ª static Observable callback: set label color
// ============================================================================
void PageMinerBase::_on_color(const uint32_t& v, void* ctx) {
    if (!ctx) return;
    lv_obj_t** lbl = static_cast<lv_obj_t**>(ctx);
    if (*lbl) lv_obj_set_style_text_color(*lbl, lv_color_hex(v), 0);
}

