#include "ui/pages/page_loading.h"

void PageLoadingBase::destroy() {
    auto& l = AppState::instance().loading;

    l.progress.unsubscribe(_on_progress);
    l.details.text.unsubscribe(_on_text);
    l.details.color.unsubscribe(_on_color);
    l.ip.text.unsubscribe(_on_ip_text);
    l.ip.color.unsubscribe(_on_color);
    l.pool.text.unsubscribe(_on_pool_text);

    _bar_progress = _lb_progress = _lb_details = _lb_ip = _lb_pool = nullptr;
    _display_progress = 0.0f;
    _target_progress = 0;
    _ip_font = nullptr;
    _pool_font = nullptr;
    _ip_max_width = 0;
    _pool_max_width = 0;
}

void PageLoadingBase::_finish_create() {
    auto& l = AppState::instance().loading;

    l.progress.subscribe(_on_progress, this);
    l.details.text.subscribe(_on_text, &_lb_details);
    l.details.color.subscribe(_on_color, &_lb_details);
    l.ip.text.subscribe(_on_ip_text, this);
    l.ip.color.subscribe(_on_color, &_lb_ip);
    l.pool.text.subscribe(_on_pool_text, this);

    _target_progress = l.progress.get();
    _display_progress = (float)_target_progress;
    _on_color(l.details.color.get(), &_lb_details);
    _on_text(l.details.text.get(), &_lb_details);
    _on_ip_text(l.ip.text.get(), this);
    _on_color(l.ip.color.get(), &_lb_ip);
    _on_pool_text(l.pool.text.get(), this);
}

void PageLoadingBase::_on_progress(const int32_t& v, void* ctx) {
    if (!ctx) return;
    PageLoadingBase* self = static_cast<PageLoadingBase*>(ctx);
    self->_target_progress = v;
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

void PageLoadingBase::_set_scrolling_label_text(lv_obj_t* lbl, const String& v,
                                                const lv_font_t* font, lv_coord_t max_width) {
    if (lbl == nullptr || font == nullptr) {
        return;
    }

    lv_coord_t width = lv_txt_get_width(v.c_str(), v.length(), font, 0, LV_TEXT_FLAG_NONE);
    if (max_width > 0 && width > max_width) {
        width = max_width;
    }
    if (width <= 0) {
        width = 1;
    }

    lv_obj_set_width(lbl, width);
    lv_label_set_text(lbl, v.c_str());
}

void PageLoadingBase::_on_ip_text(const String& v, void* ctx) {
    if (!ctx) return;
    PageLoadingBase* self = static_cast<PageLoadingBase*>(ctx);
    self->_set_scrolling_label_text(self->_lb_ip, v, self->_ip_font, self->_ip_max_width);
}

void PageLoadingBase::_on_pool_text(const String& v, void* ctx) {
    if (!ctx) return;
    PageLoadingBase* self = static_cast<PageLoadingBase*>(ctx);
    self->_set_scrolling_label_text(self->_lb_pool, v, self->_pool_font, self->_pool_max_width);
}

void PageLoadingBase::_on_update() {
    if (_bar_progress == nullptr || _lb_progress == nullptr) {
        return;
    }

    float delta = (float)_target_progress - _display_progress;
    if (fabsf(delta) > 0.001f) {
        _display_progress += delta * 0.4f;
    } else {
        _display_progress = (float)_target_progress;
    }

    if (_display_progress < 0.0f) _display_progress = 0.0f;
    if (_display_progress > 100.0f) _display_progress = 100.0f;

    String text = String((int)lroundf(_display_progress)) + "%";
    lv_label_set_text(_lb_progress, text.c_str());

    lv_coord_t bar_x = lv_obj_get_x(_bar_progress);
    lv_coord_t bar_y = lv_obj_get_y(_bar_progress);
    lv_coord_t bar_w = lv_obj_get_width(_bar_progress);
    lv_coord_t label_w = lv_obj_get_width(_lb_progress);
    lv_coord_t label_h = lv_obj_get_height(_lb_progress);
    float display_percent = _display_progress / 100.0f;
    lv_coord_t label_x = bar_x + (lv_coord_t)(bar_w * display_percent) - label_w / 2;
    lv_coord_t min_x = bar_x - label_w / 2;
    lv_coord_t max_x = bar_x + bar_w - label_w / 2;
    if (label_x < min_x) label_x = min_x;
    if (label_x > max_x) label_x = max_x;
    lv_obj_set_pos(_lb_progress, label_x, bar_y - label_h - 4);
    lv_bar_set_value(_bar_progress, (int32_t)lroundf(_display_progress), LV_ANIM_OFF);
}
