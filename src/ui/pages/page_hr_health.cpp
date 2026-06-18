#include "ui/pages/page_hr_health.h"

void PageHr_healthBase::destroy() {
    auto& h = AppState::instance().hr_health;
    h.hashrate.unsubscribe(_on_text);
    h.efficiency.unsubscribe(_on_text);
    h.shares.unsubscribe(_on_text);
    h.best_diff.unsubscribe(_on_text);
    _lb_hashrate = _lb_efficiency = _lb_shares = _lb_best_diff = nullptr;
}

void PageHr_healthBase::_finish_create() {
    auto& h = AppState::instance().hr_health;
    h.hashrate.subscribe(_on_text, &_lb_hashrate);
    h.efficiency.subscribe(_on_text, &_lb_efficiency);
    h.shares.subscribe(_on_text, &_lb_shares);
    h.best_diff.subscribe(_on_text, &_lb_best_diff);
}

void PageHr_healthBase::_on_text(const String& v, void* ctx) {
    if (!ctx) return;
    lv_obj_t** lbl = static_cast<lv_obj_t**>(ctx);
    if (*lbl) lv_label_set_text(*lbl, v.c_str());
}
