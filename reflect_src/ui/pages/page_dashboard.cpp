#include "ui/pages/page_dashboard.h"

void PageDashboardBase::destroy() {
    auto& d = AppState::instance().dashboard;
    d.power.unsubscribe(_on_text);
    d.vbus.unsubscribe(_on_text);
    d.ibus.unsubscribe(_on_text);
    d.asic_temp.unsubscribe(_on_text);
    d.vcore_temp.unsubscribe(_on_text);
    d.freq.unsubscribe(_on_text);
    d.vcore.unsubscribe(_on_text);
    _lb_power = _lb_vbus = _lb_ibus = nullptr;
    _lb_asic_temp = _lb_vcore_temp = _lb_freq = _lb_vcore = nullptr;
}

void PageDashboardBase::_finish_create() {
    auto& d = AppState::instance().dashboard;
    d.power.subscribe(_on_text, &_lb_power);
    d.vbus.subscribe(_on_text, &_lb_vbus);
    d.ibus.subscribe(_on_text, &_lb_ibus);
    d.asic_temp.subscribe(_on_text, &_lb_asic_temp);
    d.vcore_temp.subscribe(_on_text, &_lb_vcore_temp);
    d.freq.subscribe(_on_text, &_lb_freq);
    d.vcore.subscribe(_on_text, &_lb_vcore);
}

void PageDashboardBase::_on_text(const String& v, void* ctx) {
    if (!ctx) return;
    lv_obj_t** lbl = static_cast<lv_obj_t**>(ctx);
    if (*lbl) lv_label_set_text(*lbl, v.c_str());
}
