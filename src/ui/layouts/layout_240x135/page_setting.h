// What: 240x135 settings/swarm page concrete class.
// Why: Settings presentation is resolution-specific, even though the page logic
// is shared by the base class.
// Role: Binds the settings/swarm page base to the 240x135 layout metrics.
// Benefit: The exact old 240x135 settings page can be migrated here later.
#pragma once

#include "ui/pages/page_setting.h"

namespace nm::ui {

class PageSetting240x135 : public PageSettingBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "PageSetting240x135"; }
};

}  // namespace nm::ui
