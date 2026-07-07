// What: Page-specific base class for the hashrate/health screen.
// Why: The old product has a dedicated health-oriented page, and keeping that
// slot now avoids collapsing future mining telemetry back into one screen.
// Role: Provides a placeholder health-page scaffold for current runtime data.
// Benefit: The page tree matches the old product structure earlier, which makes
// later migration less invasive.
#pragma once

#include "ui/pages/page_base.h"

namespace nm::ui {

class PageHrHealthBase : public PageScaffoldBase {
public:
    state::UiPageId id() const override { return state::UiPageId::HrHealth; }
    void render(const PageContext& context) override;

protected:
    void create_hr_health_page(lv_obj_t* parent, const PageScaffoldMetrics& metrics);
};

}  // namespace nm::ui
