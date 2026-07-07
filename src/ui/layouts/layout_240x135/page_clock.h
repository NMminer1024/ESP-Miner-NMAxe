// What: 240x135 clock-page concrete class.
// Why: Clock page composition is resolution-specific and should live in the
// 240x135 layout tree from the start.
// Role: Binds the clock-page base to the 240x135 layout metrics.
// Benefit: Real clock-page migration can replace this class in place later.
#pragma once

#include "ui/pages/page_clock.h"

namespace nm::ui {

class PageClock240x135 : public PageClockBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "PageClock240x135"; }
};

}  // namespace nm::ui
