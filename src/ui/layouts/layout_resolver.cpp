// What: Layout-to-page-catalog resolver implementation for the new UI system.
// Why: The active product profile already identifies a layout, so page catalog
// selection should live in one place.
// Role: Returns the concrete page catalog for the current layout id.
// Benefit: The rest of the UI stack can stay layout-agnostic.
#include "ui/layouts/layout_resolver.h"

#include "ui/layouts/layout_240x135/layout.h"

namespace nm::ui {

const PageCatalog& resolve_page_catalog(product::UiLayoutId layout_id) {
    switch (layout_id) {
        case product::UiLayoutId::Layout240x135:
            return page_catalog_240x135();
        case product::UiLayoutId::Layout320x240:
        case product::UiLayoutId::Layout480x320:
        case product::UiLayoutId::Unknown:
        default:
            // Temporary fallback:
            // TODO(agent): return the matching layout catalog once 320x240 and
            // 480x320 pages are introduced in the new framework.
            return page_catalog_240x135();
    }
}

}  // namespace nm::ui
