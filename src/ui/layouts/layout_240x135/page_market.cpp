// What: 240x135 market-page concrete implementation.
// Why: The resolution tree should own market page construction from now on.
// Role: Creates the 240x135 market-page scaffold using shared metrics.
// Benefit: Later market-page migration lands cleanly in this layout directory.
#include "ui/layouts/layout_240x135/page_market.h"

#include "ui/layouts/layout_240x135/layout.h"

namespace nm::ui {

void PageMarket240x135::create(lv_obj_t* parent) {
    create_market_page(parent, page_metrics_240x135());
}

}  // namespace nm::ui
