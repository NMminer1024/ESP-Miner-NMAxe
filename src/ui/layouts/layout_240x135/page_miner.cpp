// What: 240x135 miner-page concrete implementation.
// Why: The 240x135 layout directory should own concrete miner-page creation
// from the start instead of deferring all layout concerns to the UI runtime.
// Role: Creates the 240x135 miner-page scaffold using shared layout metrics.
// Benefit: Future legacy miner migration has a dedicated local target.
#include "ui/layouts/layout_240x135/page_miner.h"

#include "ui/layouts/layout_240x135/layout.h"

namespace nm::ui {

void PageMiner240x135::create(lv_obj_t* parent) {
    create_miner_page(parent, page_metrics_240x135());
}

}  // namespace nm::ui
