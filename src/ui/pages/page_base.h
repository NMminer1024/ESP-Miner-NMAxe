// What: Shared LVGL page scaffold used by the page-specific base classes.
// Why: Every placeholder page currently needs the same root container, title,
// body lines, and footer while the real legacy assets are migrated gradually.
// Role: Centralizes create/destroy/show helpers and simple text-line handling.
// Benefit: Each page base stays focused on its own state mapping instead of
// re-implementing the same LVGL wiring for every resolution variant.
#pragma once

#include <array>
#include <stddef.h>

#include <lvgl.h>

#include "ui/page.h"

namespace nm::ui {

constexpr size_t kPageScaffoldLineCount = 5;

struct PageScaffoldMetrics {
    lv_coord_t width = 240;
    lv_coord_t height = 135;
    lv_coord_t title_y = 6;
    lv_coord_t subtitle_y = 26;
    std::array<lv_coord_t, kPageScaffoldLineCount> line_y = {46, 60, 74, 88, 102};
    lv_coord_t footer_y = -4;
    lv_coord_t horizontal_padding = 8;
};

class PageScaffoldBase : public UIPage {
public:
    void destroy() override;

protected:
    void create_scaffold(lv_obj_t* parent, const PageScaffoldMetrics& metrics);
    void set_title(const char* text) const;
    void set_subtitle(const char* text) const;
    void set_line(size_t index, const char* text) const;
    void clear_lines_from(size_t index) const;
    void set_footer(const char* text) const;
    bool is_created() const { return _root != nullptr; }

private:
    static lv_obj_t* create_label(
        lv_obj_t* parent,
        const lv_font_t* font,
        lv_align_t align,
        lv_coord_t x,
        lv_coord_t y,
        lv_coord_t width);

protected:
    lv_obj_t* _root = nullptr;
    lv_obj_t* _title = nullptr;
    lv_obj_t* _subtitle = nullptr;
    std::array<lv_obj_t*, kPageScaffoldLineCount> _lines{};
    lv_obj_t* _footer = nullptr;
    PageScaffoldMetrics _metrics{};
};

}  // namespace nm::ui
