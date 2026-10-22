#pragma once

#include "ui/pages/page_lucky.h"

class PageLucky240x135 : public PageLuckyBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "lucky240x135"; }

protected:
    void _create_dynamic(lv_obj_t* parent) override;
};
