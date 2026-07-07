#pragma once

#include "ui/pages/page_setting.h"

class PageSetting240x135 : public PageSwarmBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "setting240x135"; }

protected:
    void _create_dynamic(lv_obj_t* parent) override;
};
