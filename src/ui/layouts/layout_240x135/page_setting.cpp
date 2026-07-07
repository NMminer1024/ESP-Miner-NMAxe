// What: 240x135 settings/swarm page concrete implementation.
// Why: The 240x135 layout should already expose a concrete settings page class.
// Role: Creates the 240x135 settings page scaffold using shared metrics.
// Benefit: Future legacy settings-page migration has a direct local target.
#include "ui/layouts/layout_240x135/page_setting.h"

#include "ui/layouts/layout_240x135/layout.h"

namespace nm::ui {

void PageSetting240x135::create(lv_obj_t* parent) {
    create_setting_page(parent, page_metrics_240x135());
}

}  // namespace nm::ui
