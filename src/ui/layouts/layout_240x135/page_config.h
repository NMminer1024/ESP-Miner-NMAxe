#pragma once

#include "ui/pages/page_config.h"

class PageConfig240x135 : public PageConfigQrBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "config240x135"; }

protected:
    void _create_dynamic(lv_obj_t* parent) override;
};
