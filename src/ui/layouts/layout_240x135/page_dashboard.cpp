// What: 240x135 dashboard-page concrete implementation.
// Why: Layout-specific dashboard construction belongs under the resolution tree.
// Role: Creates the 240x135 dashboard-page scaffold using shared metrics.
// Benefit: Future dashboard visual migration stays local to this layout.
#include "ui/layouts/layout_240x135/page_dashboard.h"

#include "ui/layouts/layout_240x135/layout.h"

namespace nm::ui {

void PageDashboard240x135::create(lv_obj_t* parent) {
    create_dashboard_page(parent, page_metrics_240x135());
}

}  // namespace nm::ui
