// What: Legacy-style loading-page implementation for the new UI framework.
// Why: The old boot flow already defined the expected layout and motion, so the
// new tree should recreate that experience instead of keeping a placeholder.
// Role: Builds the loading widgets and binds boot/runtime/config state into the
// exact labels and progress bar used during startup.
// Benefit: NMAxe and Gamma now boot into a real migrated page without forcing
// old global UI managers back into the new architecture.
#include "ui/pages/page_loading.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app/firmware_identity.h"
#include "ui/assets/loading/loading_visuals.h"

namespace nm::ui {

namespace {
const char* loading_ip_text(const PageContext& context, const assets::LoadingVisualSpec& visuals) {
    if (context.runtime.network.sta_connected && context.runtime.network.ip[0] != '\0') {
        return context.runtime.network.ip;
    }

    return visuals.headline;
}

const char* loading_pool(const PageContext& context, const assets::LoadingVisualSpec& visuals) {
    if (context.runtime.power.vcore_ready && !context.config.stratum.primary.url.isEmpty()) {
        return context.config.stratum.primary.url.c_str();
    }

    return visuals.fallback_pool;
}

lv_color_t loading_ip_color(const PageContext& context) {
    return context.runtime.network.sta_connected ? lv_color_hex(0x00FF00) : lv_color_white();
}

lv_coord_t text_width(const char* text, const lv_font_t* font) {
    if (text == nullptr || font == nullptr) {
        return 0;
    }

    return lv_txt_get_width(text, static_cast<uint16_t>(strlen(text)), font, 0, LV_TEXT_FLAG_NONE);
}

void style_loading_text(lv_obj_t* label, const lv_font_t* font) {
    if (label == nullptr) {
        return;
    }

    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN);
}

}  // namespace

void PageLoadingBase::create_loading_page(lv_obj_t* parent, lv_coord_t width, lv_coord_t height) {
    if (_root != nullptr) {
        return;
    }

    _width = width;
    _height = height;
    _display_progress = 0.0f;
    _target_progress = 0;
    _ip_font = &lv_font_montserrat_20;
    _pool_font = &lv_font_montserrat_16;
    _ip_max_width = width;
    _pool_max_width = width;

    _root = lv_obj_create(parent);
    if (_root == nullptr) {
        return;
    }

    lv_obj_remove_style_all(_root);
    lv_obj_set_size(_root, lv_pct(100), lv_pct(100));
    lv_obj_align(_root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(_root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(_root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, LV_PART_MAIN);

    const auto& visuals = assets::loading_visuals_240x135();

    _background = lv_img_create(_root);
    if (_background != nullptr && visuals.background != nullptr) {
        lv_img_set_src(_background, visuals.background);
        lv_obj_set_pos(_background, 0, 0);
    }

    _lb_version = lv_label_create(_root);
    style_loading_text(_lb_version, &lv_font_montserrat_16);
    lv_label_set_text(_lb_version, app::kFirmwareVersion);
    const lv_coord_t version_width = text_width(app::kFirmwareVersion, &lv_font_montserrat_16);
    lv_obj_set_width(_lb_version, version_width);
    lv_obj_align(_lb_version, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    _lb_details = lv_label_create(_root);
    style_loading_text(_lb_details, &lv_font_montserrat_14);
    lv_obj_set_width(_lb_details, static_cast<lv_coord_t>(width - version_width));
    lv_label_set_long_mode(_lb_details, LV_LABEL_LONG_DOT);
    lv_label_set_text(_lb_details, "Initializing...");
    lv_obj_align(_lb_details, LV_ALIGN_BOTTOM_LEFT, 3, 0);

    _bar_progress = lv_bar_create(_root);
    lv_bar_set_range(_bar_progress, 0, 100);
    lv_bar_set_value(_bar_progress, 0, LV_ANIM_OFF);
    lv_obj_set_size(_bar_progress, static_cast<lv_coord_t>((width * 9) / 10), 5);
    lv_obj_set_style_bg_color(_bar_progress, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_bar_progress, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_bar_progress, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(_bar_progress, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_align(_bar_progress, LV_ALIGN_CENTER, 0, -20);

    _lb_progress = lv_label_create(_root);
    style_loading_text(_lb_progress, &lv_font_montserrat_16);
    lv_label_set_text(_lb_progress, "0%");

    _lb_ip = lv_label_create(_root);
    style_loading_text(_lb_ip, _ip_font);
    lv_label_set_long_mode(_lb_ip, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(_lb_ip, LV_ALIGN_CENTER, 0, 13);
    set_scrolling_text(_lb_ip, visuals.headline, _ip_font, _ip_max_width);

    _lb_pool = lv_label_create(_root);
    style_loading_text(_lb_pool, _pool_font);
    lv_label_set_long_mode(_lb_pool, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(_lb_pool, LV_ALIGN_CENTER, 0, 35);
    set_scrolling_text(_lb_pool, visuals.fallback_pool, _pool_font, _pool_max_width);

    update_progress_widgets();
}

void PageLoadingBase::destroy() {
    if (_root != nullptr) {
        lv_obj_del(_root);
    }

    _root = nullptr;
    _background = nullptr;
    _bar_progress = nullptr;
    _lb_progress = nullptr;
    _lb_details = nullptr;
    _lb_ip = nullptr;
    _lb_pool = nullptr;
    _lb_version = nullptr;
    _width = 0;
    _height = 0;
    _ip_max_width = 0;
    _pool_max_width = 0;
    _display_progress = 0.0f;
    _target_progress = 0;
    _ip_font = nullptr;
    _pool_font = nullptr;
}

void PageLoadingBase::set_scrolling_text(
    lv_obj_t* label,
    const char* text,
    const lv_font_t* font,
    lv_coord_t max_width) const {
    if (label == nullptr || font == nullptr) {
        return;
    }

    const char* resolved = text != nullptr ? text : "";
    lv_coord_t width = text_width(resolved, font);
    if (max_width > 0 && width > max_width) {
        width = max_width;
    }
    if (width <= 0) {
        width = 1;
    }

    lv_obj_set_width(label, width);
    lv_label_set_text(label, resolved);
}

void PageLoadingBase::update_progress_widgets() {
    if (_bar_progress == nullptr || _lb_progress == nullptr) {
        return;
    }

    const float delta = static_cast<float>(_target_progress) - _display_progress;
    if (fabsf(delta) > 0.001f) {
        _display_progress += delta * 0.4f;
    } else {
        _display_progress = static_cast<float>(_target_progress);
    }

    if (_display_progress < 0.0f) {
        _display_progress = 0.0f;
    }
    if (_display_progress > 100.0f) {
        _display_progress = 100.0f;
    }

    char progress_text[8] = {};
    snprintf(progress_text, sizeof(progress_text), "%u%%", static_cast<unsigned>(lroundf(_display_progress)));
    lv_label_set_text(_lb_progress, progress_text);

    const lv_coord_t bar_x = lv_obj_get_x(_bar_progress);
    const lv_coord_t bar_y = lv_obj_get_y(_bar_progress);
    const lv_coord_t bar_w = lv_obj_get_width(_bar_progress);
    const lv_coord_t label_w = lv_obj_get_width(_lb_progress);
    const float ratio = _display_progress / 100.0f;

    lv_coord_t label_x = static_cast<lv_coord_t>(bar_x + static_cast<lv_coord_t>(bar_w * ratio) - (label_w / 2));
    const lv_coord_t min_x = static_cast<lv_coord_t>(bar_x - (label_w / 2));
    const lv_coord_t max_x = static_cast<lv_coord_t>(bar_x + bar_w - (label_w / 2));
    if (label_x < min_x) {
        label_x = min_x;
    }
    if (label_x > max_x) {
        label_x = max_x;
    }

    lv_obj_set_pos(_lb_progress, label_x, static_cast<lv_coord_t>(bar_y + 10));
    lv_bar_set_value(_bar_progress, static_cast<int32_t>(lroundf(_display_progress)), LV_ANIM_OFF);
}

void PageLoadingBase::render(const PageContext& context) {
    if (!is_created()) {
        return;
    }

    const auto& visuals = assets::loading_visuals_240x135();

    _target_progress = context.runtime.boot.progress_percent;
    update_progress_widgets();

    if (_lb_details != nullptr) {
        lv_label_set_text(
            _lb_details,
            context.runtime.boot.message != nullptr ? context.runtime.boot.message : "");
        lv_obj_set_style_text_color(
            _lb_details,
            lv_color_hex(context.runtime.boot.message_color),
            LV_PART_MAIN);
    }

    if (_lb_ip != nullptr) {
        set_scrolling_text(
            _lb_ip,
            loading_ip_text(context, visuals),
            _ip_font,
            _ip_max_width);
        lv_obj_set_style_text_color(_lb_ip, loading_ip_color(context), LV_PART_MAIN);
    }

    if (_lb_pool != nullptr) {
        set_scrolling_text(
            _lb_pool,
            loading_pool(context, visuals),
            _pool_font,
            _pool_max_width);
    }
}

}  // namespace nm::ui
