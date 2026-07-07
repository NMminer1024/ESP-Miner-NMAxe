// What: 240x135 config-page concrete implementation.
// Why: The page hierarchy should already route config UI through a concrete
// 240x135 class instead of leaving the layout directory empty.
// Role: Creates the 240x135 config-page scaffold using shared layout metrics.
// Benefit: The config page now has a stable resolution-specific landing zone.
#include "ui/layouts/layout_240x135/page_config.h"

#include "ui/layouts/layout_240x135/layout.h"

namespace nm::ui {

void PageConfig240x135::create(lv_obj_t* parent) {
    create_config_page(parent, page_metrics_240x135());
}

}  // namespace nm::ui
