// What: Page-specific base class for the boot loading screen.
// Why: The loading page is the first migrated old page and needs real widget
// ownership instead of the temporary text scaffold.
// Role: Owns the shared legacy-style loading widgets and boot-state mapping.
// Benefit: Layout variants can keep exact visuals while reusing one render
// pipeline for progress, details, version text, IP/slogan, and pool labels.
#pragma once

#include "ui/page.h"

namespace nm::ui {

class PageLoadingBase : public UIPage {
public:
    state::UiPageId id() const override { return state::UiPageId::Loading; }
    void destroy() override;
    void render(const PageContext& context) override;

protected:
    void create_loading_page(lv_obj_t* parent, lv_coord_t width, lv_coord_t height);

    lv_obj_t* _root = nullptr;
    lv_obj_t* _background = nullptr;
    lv_obj_t* _bar_progress = nullptr;
    lv_obj_t* _lb_progress = nullptr;
    lv_obj_t* _lb_details = nullptr;
    lv_obj_t* _lb_ip = nullptr;
    lv_obj_t* _lb_pool = nullptr;
    lv_obj_t* _lb_version = nullptr;
    lv_coord_t _width = 0;
    lv_coord_t _height = 0;
    lv_coord_t _ip_max_width = 0;
    lv_coord_t _pool_max_width = 0;
    float _display_progress = 0.0f;
    uint8_t _target_progress = 0;
    const lv_font_t* _ip_font = nullptr;
    const lv_font_t* _pool_font = nullptr;

private:
    void set_scrolling_text(
        lv_obj_t* label,
        const char* text,
        const lv_font_t* font,
        lv_coord_t max_width) const;
    void update_progress_widgets();
    bool is_created() const { return _root != nullptr; }
};

}  // namespace nm::ui
