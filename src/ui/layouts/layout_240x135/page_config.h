// What: 240x135 config-page concrete class.
// Why: Config-page visuals will differ by resolution, so the layout-specific
// class needs to exist before the real old page is migrated.
// Role: Binds the config-page base to the 240x135 layout metrics.
// Benefit: The exact old 240x135 config page can replace this file later.
#pragma once

#include "ui/pages/page_config.h"

namespace nm::ui {

class PageConfig240x135 : public PageConfigBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "PageConfig240x135"; }
};

}  // namespace nm::ui
