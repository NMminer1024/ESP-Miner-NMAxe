// What: Page-specific base class for the miner screen.
// Why: Mining state will grow quickly, so it needs a dedicated UI surface
// rather than continuing to share a generic placeholder screen.
// Role: Maps runtime mining state into the miner-page scaffold.
// Benefit: The future exact old miner layout can slot in without changing the
// rest of the UI routing or service-facing contracts.
#pragma once

#include "ui/pages/page_base.h"

namespace nm::ui {

class PageMinerBase : public PageScaffoldBase {
public:
    state::UiPageId id() const override { return state::UiPageId::Miner; }
    void render(const PageContext& context) override;

protected:
    void create_miner_page(lv_obj_t* parent, const PageScaffoldMetrics& metrics);
};

}  // namespace nm::ui
