// What: 240x135 loading-page concrete implementation.
// Why: The old 240x135 products share one exact loading layout that belongs in
// the resolution tree rather than the generic UI root.
// Role: Creates the migrated legacy loading page for NMAxe and Gamma.
// Benefit: Boot visuals now match the old product flow while staying inside the
// new page framework.
#include "ui/layouts/layout_240x135/page_loading.h"

namespace nm::ui {

void PageLoading240x135::create(lv_obj_t* parent) {
    create_loading_page(parent, 240, 135);
}

}  // namespace nm::ui
