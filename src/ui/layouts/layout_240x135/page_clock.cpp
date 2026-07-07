// What: 240x135 clock-page concrete implementation.
// Why: The layout layer should instantiate a concrete clock page for this
// resolution even before the real legacy page is migrated.
// Role: Creates the 240x135 clock-page scaffold using shared metrics.
// Benefit: Clock-page visual migration stays isolated under the layout tree.
#include "ui/layouts/layout_240x135/page_clock.h"

#include "ui/layouts/layout_240x135/layout.h"

namespace nm::ui {

void PageClock240x135::create(lv_obj_t* parent) {
    create_clock_page(parent, page_metrics_240x135());
}

}  // namespace nm::ui
