// What: 240x135 dashboard-page concrete class.
// Why: Dashboard presentation is resolution-specific even when the runtime data
// it consumes is shared across boards.
// Role: Binds the dashboard-page base to the 240x135 layout metrics.
// Benefit: The old 240x135 dashboard visuals can be migrated in place later.
#pragma once

#include "ui/pages/page_dashboard.h"

namespace nm::ui {

class PageDashboard240x135 : public PageDashboardBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "PageDashboard240x135"; }
};

}  // namespace nm::ui
