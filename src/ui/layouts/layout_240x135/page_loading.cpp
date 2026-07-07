// What: 240x135 loading-page concrete implementation.
// Why: The layout layer should decide how a loading page is instantiated for
// this resolution, even before the exact legacy background is migrated.
// Role: Creates the 240x135 loading-page scaffold using shared layout metrics.
// Benefit: Loading-page visuals can now evolve locally inside the 240x135 tree.
#include "ui/layouts/layout_240x135/page_loading.h"

#include "ui/layouts/layout_240x135/layout.h"

namespace nm::ui {

void PageLoading240x135::create(lv_obj_t* parent) {
    create_loading_page(parent, page_metrics_240x135());
}

}  // namespace nm::ui
