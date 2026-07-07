// What: 240x135 hr-health page concrete implementation.
// Why: The resolution tree should already own construction of the health page.
// Role: Creates the 240x135 hr-health page scaffold using shared metrics.
// Benefit: Later old-layout migration has a direct target under 240x135.
#include "ui/layouts/layout_240x135/page_hr_health.h"

#include "ui/layouts/layout_240x135/layout.h"

namespace nm::ui {

void PageHrHealth240x135::create(lv_obj_t* parent) {
    create_hr_health_page(parent, page_metrics_240x135());
}

}  // namespace nm::ui
