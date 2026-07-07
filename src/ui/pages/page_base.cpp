// What: Implementation of the shared LVGL text-page scaffold.
// Why: Page bring-up should be mechanically consistent while the real old UI is
// still being migrated page by page.
// Role: Builds and manages a full-screen container with common labels.
// Benefit: Placeholder pages remain cheap to create now and easy to replace
// later with the exact legacy visuals inside the same class hierarchy.
#include "ui/pages/page_base.h"

namespace nm::ui {

lv_obj_t* PageScaffoldBase::create_label(
    lv_obj_t* parent,
    const lv_font_t* font,
    lv_align_t align,
    lv_coord_t x,
    lv_coord_t y,
    lv_coord_t width) {
    lv_obj_t* label = lv_label_create(parent);
    if (label == nullptr) {
        return nullptr;
    }

    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(label, width);
    lv_obj_align(label, align, x, y);
    return label;
}

void PageScaffoldBase::create_scaffold(lv_obj_t* parent, const PageScaffoldMetrics& metrics) {
    if (_root != nullptr) {
        return;
    }

    _metrics = metrics;
    _root = lv_obj_create(parent);
    if (_root == nullptr) {
        return;
    }

    lv_obj_remove_style_all(_root);
    lv_obj_set_size(_root, lv_pct(100), lv_pct(100));
    lv_obj_align(_root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(_root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(_root, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, LV_PART_MAIN);

    const lv_coord_t body_width = static_cast<lv_coord_t>(_metrics.width - (_metrics.horizontal_padding * 2));
    _title = create_label(_root, &lv_font_montserrat_16, LV_ALIGN_TOP_MID, 0, _metrics.title_y, body_width);
    _subtitle = create_label(_root, &lv_font_montserrat_12, LV_ALIGN_TOP_MID, 0, _metrics.subtitle_y, body_width);
    for (size_t i = 0; i < _lines.size(); ++i) {
        _lines[i] = create_label(
            _root,
            &lv_font_montserrat_12,
            LV_ALIGN_TOP_LEFT,
            _metrics.horizontal_padding,
            _metrics.line_y[i],
            body_width);
    }
    _footer = create_label(_root, &lv_font_montserrat_10, LV_ALIGN_BOTTOM_MID, 0, _metrics.footer_y, body_width);
}

void PageScaffoldBase::destroy() {
    if (_root != nullptr) {
        lv_obj_del(_root);
    }

    _root = nullptr;
    _title = nullptr;
    _subtitle = nullptr;
    _lines.fill(nullptr);
    _footer = nullptr;
}

void PageScaffoldBase::set_title(const char* text) const {
    if (_title != nullptr) {
        lv_label_set_text(_title, text != nullptr ? text : "");
    }
}

void PageScaffoldBase::set_subtitle(const char* text) const {
    if (_subtitle != nullptr) {
        lv_label_set_text(_subtitle, text != nullptr ? text : "");
    }
}

void PageScaffoldBase::set_line(size_t index, const char* text) const {
    if (index >= _lines.size() || _lines[index] == nullptr) {
        return;
    }

    lv_label_set_text(_lines[index], text != nullptr ? text : "");
}

void PageScaffoldBase::clear_lines_from(size_t index) const {
    for (size_t i = index; i < _lines.size(); ++i) {
        set_line(i, "");
    }
}

void PageScaffoldBase::set_footer(const char* text) const {
    if (_footer != nullptr) {
        lv_label_set_text(_footer, text != nullptr ? text : "");
    }
}

}  // namespace nm::ui
