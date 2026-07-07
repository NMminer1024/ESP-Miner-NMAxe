#pragma once

#include "ui/pages/page_config.h"

class PageConfig320x240 : public PageConfigWifiListBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "config320x240"; }

protected:
    void _create_dynamic(lv_obj_t* parent) override;
};
