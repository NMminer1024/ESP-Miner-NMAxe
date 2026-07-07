// What: Page-specific base class for the loading screen.
// Why: The loading page will be the first real old UI page to migrate, so it
// deserves its own stable class boundary now instead of living in `ui_root`.
// Role: Maps boot/runtime state into a loading-page scaffold independent of any
// particular resolution implementation.
// Benefit: 240x135 and future resolutions can share loading logic while still
// owning different visuals, coordinates, and background assets.
#pragma once

#include "ui/pages/page_base.h"

namespace nm::ui {

class PageLoadingBase : public PageScaffoldBase {
public:
    state::UiPageId id() const override { return state::UiPageId::Loading; }
    void render(const PageContext& context) override;

protected:
    void create_loading_page(lv_obj_t* parent, const PageScaffoldMetrics& metrics);
};

}  // namespace nm::ui
