#pragma once

#include "ui/pages/page_hr_health.h"

class PageHr_health320x240 : public PageHr_healthBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "hr_health320x240"; }

protected:
    void _create_dynamic(lv_obj_t* parent) override;
};
