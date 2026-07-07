// What: Page-specific base class for the configuration screen.
// Why: Config presentation will eventually mirror the old QR/list flows, but
// the framework needs the page slot and abstraction now.
// Role: Maps configuration state into a neutral config-page scaffold.
// Benefit: Later config UI migration can keep the same navigation contract.
#pragma once

#include "ui/pages/page_base.h"

namespace nm::ui {

class PageConfigBase : public PageScaffoldBase {
public:
    state::UiPageId id() const override { return state::UiPageId::Config; }
    void render(const PageContext& context) override;

protected:
    void create_config_page(lv_obj_t* parent, const PageScaffoldMetrics& metrics);
};

}  // namespace nm::ui
