// What: Page-specific base class for the clock screen.
// Why: Time-related presentation and settings should live on their own page,
// matching the old product page tree.
// Role: Maps current time configuration and idle-state data into a clock-page
// scaffold until RTC/SNTP state is migrated.
// Benefit: Time UI wiring can evolve independently from other product pages.
#pragma once

#include "ui/pages/page_base.h"

namespace nm::ui {

class PageClockBase : public PageScaffoldBase {
public:
    state::UiPageId id() const override { return state::UiPageId::Clock; }
    void render(const PageContext& context) override;

protected:
    void create_clock_page(lv_obj_t* parent, const PageScaffoldMetrics& metrics);
};

}  // namespace nm::ui
