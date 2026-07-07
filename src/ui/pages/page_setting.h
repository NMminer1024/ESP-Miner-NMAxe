// What: Page-specific base class for the settings/swarm screen.
// Why: The old product exposes a dedicated settings/swarm page, so the new page
// tree should reserve that slot now rather than folding settings elsewhere.
// Role: Maps system-level settings and input traits into a settings scaffold.
// Benefit: Future settings/swarm migration can reuse the same routing contract.
#pragma once

#include "ui/pages/page_base.h"

namespace nm::ui {

class PageSettingBase : public PageScaffoldBase {
public:
    state::UiPageId id() const override { return state::UiPageId::SettingSwarm; }
    void render(const PageContext& context) override;

protected:
    void create_setting_page(lv_obj_t* parent, const PageScaffoldMetrics& metrics);
};

}  // namespace nm::ui
