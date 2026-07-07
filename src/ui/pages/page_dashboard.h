// What: Page-specific base class for the dashboard screen.
// Why: Power and thermal telemetry belong on their own page abstraction even
// before the final layout and art are migrated from old UI code.
// Role: Maps board/runtime telemetry into a dashboard-page scaffold.
// Benefit: The dashboard slot becomes stable now, so later visual migration is
// isolated to the layout classes instead of touching service code.
#pragma once

#include "ui/pages/page_base.h"

namespace nm::ui {

class PageDashboardBase : public PageScaffoldBase {
public:
    state::UiPageId id() const override { return state::UiPageId::Dashboard; }
    void render(const PageContext& context) override;

protected:
    void create_dashboard_page(lv_obj_t* parent, const PageScaffoldMetrics& metrics);
};

}  // namespace nm::ui
