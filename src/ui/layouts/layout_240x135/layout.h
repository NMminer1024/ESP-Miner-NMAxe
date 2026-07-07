// What: 240x135 layout registry for the new page framework.
// Why: Resolution-specific page classes need one place to expose their catalog
// and shared scaffold metrics to the rest of the UI system.
// Role: Publishes the 240x135 page set used by Gamma and similar boards.
// Benefit: Adding new boards with the same resolution can reuse this layout
// without changing top-level UI routing code.
#pragma once

#include "ui/page.h"
#include "ui/pages/page_base.h"

namespace nm::ui {

const PageCatalog& page_catalog_240x135();
const PageScaffoldMetrics& page_metrics_240x135();

}  // namespace nm::ui
