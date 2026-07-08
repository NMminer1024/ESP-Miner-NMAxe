// What: 320x240 layout registry for the new page framework.
// Why: QAxe++ selects a 320x240 UI profile and should not fall back to the
// 240x135 catalog while BSP bring-up is being validated.
// Role: Publishes the 320x240 page set and shared scaffold metrics.
// Benefit: QAxe++ can boot through its own layout path while exact legacy pages
// are migrated incrementally.
#pragma once

#include "ui/page.h"
#include "ui/pages/page_base.h"

namespace nm::ui {

const PageCatalog& page_catalog_320x240();
const PageScaffoldMetrics& page_metrics_320x240();

}  // namespace nm::ui
