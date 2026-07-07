#pragma once

#include "ui/pages/page_clock.h"

class PageClock240x135 : public PageClockBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "clock240x135"; }

protected:
    void _create_dynamic(lv_obj_t* parent) override;
};
