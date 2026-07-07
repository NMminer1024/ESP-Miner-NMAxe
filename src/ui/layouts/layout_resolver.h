// What: Layout-to-page-catalog resolver for the new page framework.
// Why: `ui_root` should choose a page set by layout id, not by hard-coded page
// construction logic.
// Role: Translates a resolved product layout into a concrete page catalog.
// Benefit: New resolutions can be added by extending one resolver instead of
// editing every UI entry point.
#pragma once

#include "product/ui_profile.h"
#include "ui/page.h"

namespace nm::ui {

const PageCatalog& resolve_page_catalog(product::UiLayoutId layout_id);

}  // namespace nm::ui
