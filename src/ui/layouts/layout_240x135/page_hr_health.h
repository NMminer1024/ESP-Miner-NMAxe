// What: 240x135 hr-health page concrete class.
// Why: The old product has a separate hr-health page per resolution, so this
// layout tree should expose that class boundary now.
// Role: Binds the hr-health base to the 240x135 layout metrics.
// Benefit: Health-page migration work can remain fully layout-local later on.
#pragma once

#include "ui/pages/page_hr_health.h"

namespace nm::ui {

class PageHrHealth240x135 : public PageHrHealthBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "PageHrHealth240x135"; }
};

}  // namespace nm::ui
