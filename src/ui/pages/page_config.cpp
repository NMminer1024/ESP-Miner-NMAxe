#include "ui/pages/page_config.h"

void PageConfigBase::destroy() {
    auto& c = AppState::instance().config;
    c.ssid.unsubscribe(_on_text);
    c.ip.unsubscribe(_on_text);
    c.timeout.unsubscribe(_on_text);
    _lb_ssid = _lb_ip = _lb_timeout = nullptr;
}

void PageConfigBase::_finish_create() {
    auto& c = AppState::instance().config;
    c.ssid.subscribe(_on_text, &_lb_ssid);
    c.ip.subscribe(_on_text, &_lb_ip);
    c.timeout.subscribe(_on_text, &_lb_timeout);
}

void PageConfigBase::_on_text(const String& v, void* ctx) {
    if (!ctx) return;
    lv_obj_t** lbl = static_cast<lv_obj_t**>(ctx);
    if (*lbl) lv_label_set_text(*lbl, v.c_str());
}
