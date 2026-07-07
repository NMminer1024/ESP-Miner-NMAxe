// What: Page-specific base class for the market screen.
// Why: Market settings and later quote data should not leak into generic UI
// scaffolding or unrelated pages.
// Role: Maps current market-related config into a market-page scaffold.
// Benefit: The eventual market page can be migrated behind a stable interface.
#pragma once

#include "ui/pages/page_base.h"

namespace nm::ui {

class PageMarketBase : public PageScaffoldBase {
public:
    state::UiPageId id() const override { return state::UiPageId::Market; }
    void render(const PageContext& context) override;

protected:
    void create_market_page(lv_obj_t* parent, const PageScaffoldMetrics& metrics);
};

}  // namespace nm::ui
