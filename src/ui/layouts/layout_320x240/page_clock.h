#pragma once

#include "ui/pages/page_clock.h"

class PageClock320x240 : public PageClockBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "clock320x240"; }

protected:
    void _create_dynamic(lv_obj_t* parent) override;
};
