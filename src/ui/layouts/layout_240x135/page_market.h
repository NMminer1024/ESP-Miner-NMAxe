// What: 240x135 market-page concrete class.
// Why: Market page visuals will later diverge by resolution and asset set.
// Role: Binds the market-page base to the 240x135 layout metrics.
// Benefit: The page hierarchy now mirrors the old product organization.
#pragma once

#include "ui/pages/page_market.h"

namespace nm::ui {

class PageMarket240x135 : public PageMarketBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "PageMarket240x135"; }
};

}  // namespace nm::ui
