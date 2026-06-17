#include "ui/pages/page_setting.h"

void PageSettingBase::destroy() {
    auto& s = AppState::instance().setting_swarm;
    s.workers.unsubscribe(_on_text);
    s.total_hr.unsubscribe(_on_text);
    s.best_diff.unsubscribe(_on_text);
    s.neighbors.unsubscribe(_on_text);
    s.ip.unsubscribe(_on_text);
    _lb_workers = _lb_total_hr = _lb_best_diff = _lb_neighbors = _lb_ip = nullptr;
}

void PageSettingBase::_finish_create() {
    auto& s = AppState::instance().setting_swarm;
    s.workers.subscribe(_on_text, &_lb_workers);
    s.total_hr.subscribe(_on_text, &_lb_total_hr);
    s.best_diff.subscribe(_on_text, &_lb_best_diff);
    s.neighbors.subscribe(_on_text, &_lb_neighbors);
    s.ip.subscribe(_on_text, &_lb_ip);
}

void PageSettingBase::_on_text(const String& v, void* ctx) {
    if (!ctx) return;
    lv_obj_t** lbl = static_cast<lv_obj_t**>(ctx);
    if (*lbl) lv_label_set_text(*lbl, v.c_str());
}
