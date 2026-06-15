#include "pages/page_loading.h"

void PageLoadingBase::destroy() {
    auto& l = AppState::instance().loading;

    l.progress.unsubscribe(_on_progress);
    l.details.text.unsubscribe(_on_text);
    l.details.color.unsubscribe(_on_color);
    l.ip.text.unsubscribe(_on_text);

    _bar_progress = _lb_details = _lb_ip = nullptr;
}

void PageLoadingBase::_finish_create() {
    auto& l = AppState::instance().loading;

    l.progress.subscribe(_on_progress, &_bar_progress);
    l.details.text.subscribe(_on_text, &_lb_details);
    l.details.color.subscribe(_on_color, &_lb_details);
    l.ip.text.subscribe(_on_text, &_lb_ip);
}

void PageLoadingBase::_on_progress(const int32_t& v, void* ctx) {
    if (!ctx) return;
    lv_obj_t** bar = static_cast<lv_obj_t**>(ctx);
    if (*bar) lv_bar_set_value(*bar, v, LV_ANIM_ON);
}

void PageLoadingBase::_on_text(const String& v, void* ctx) {
    if (!ctx) return;
    lv_obj_t** lbl = static_cast<lv_obj_t**>(ctx);
    if (*lbl) lv_label_set_text(*lbl, v.c_str());
}

void PageLoadingBase::_on_color(const uint32_t& v, void* ctx) {
    if (!ctx) return;
    lv_obj_t** lbl = static_cast<lv_obj_t**>(ctx);
    if (*lbl) lv_obj_set_style_text_color(*lbl, lv_color_hex(v), 0);
}

