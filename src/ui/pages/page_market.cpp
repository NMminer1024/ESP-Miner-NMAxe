#include "ui/pages/page_market.h"

void PageMarketBase::destroy() {
    auto& m = AppState::instance().market;
    m.symbol.unsubscribe(_on_text);
    m.price.unsubscribe(_on_text);
    m.change.unsubscribe(_on_text);
    m.change.unsubscribe(_on_color);
    _lb_symbol = _lb_price = _lb_change = nullptr;
}

void PageMarketBase::_finish_create() {
    auto& m = AppState::instance().market;
    m.symbol.subscribe(_on_text, &_lb_symbol);
    m.price.subscribe(_on_text, &_lb_price);
    m.change.subscribe(_on_text, &_lb_change);
    m.change.subscribe(_on_color, &_lb_change);
}

void PageMarketBase::_on_text(const String& v, void* ctx) {
    if (!ctx) return;
    lv_obj_t** lbl = static_cast<lv_obj_t**>(ctx);
    if (*lbl) lv_label_set_text(*lbl, v.c_str());
}

void PageMarketBase::_on_color(const uint32_t& v, void* ctx) {
    if (!ctx) return;
    lv_obj_t** lbl = static_cast<lv_obj_t**>(ctx);
    if (*lbl) lv_obj_set_style_text_color(*lbl, lv_color_hex(v), 0);
}
