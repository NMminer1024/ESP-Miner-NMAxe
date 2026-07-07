#pragma once

#include "ui/pages/page_setting.h"

class PageSetting320x240 : public PageSettingBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "setting320x240"; }

protected:
    void _create_dynamic(lv_obj_t* parent) override;
};
