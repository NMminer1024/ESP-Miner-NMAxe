#include "ui/pages/page_clock.h"

void PageClockBase::destroy() {
    auto& c = AppState::instance().clock;
    auto& m = AppState::instance().miner;
    c.time_str.unsubscribe(_on_text);
    c.date_str.unsubscribe(_on_text);
    c.price.unsubscribe(_on_text);
    m.hashrate.unsubscribe(_on_text);
    m.hashrate_unit.unsubscribe(_on_text);
    m.blk_hit.unsubscribe(_on_text);
    _lb_time = _lb_date = _lb_hr = _lb_hr_unit = _lb_hits = _lb_price = nullptr;
}

void PageClockBase::_finish_create() {
    auto& c = AppState::instance().clock;
    auto& m = AppState::instance().miner;
    c.time_str.subscribe(_on_text, &_lb_time);
    c.date_str.subscribe(_on_text, &_lb_date);
    c.price.subscribe(_on_text, &_lb_price);
    m.hashrate.subscribe(_on_text, &_lb_hr);
    m.hashrate_unit.subscribe(_on_text, &_lb_hr_unit);
    m.blk_hit.subscribe(_on_text, &_lb_hits);
}

void PageClockBase::_on_text(const String& v, void* ctx) {
    if (!ctx) return;
    lv_obj_t** lbl = static_cast<lv_obj_t**>(ctx);
    if (*lbl) lv_label_set_text(*lbl, v.c_str());
}
