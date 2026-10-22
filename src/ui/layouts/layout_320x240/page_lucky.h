#pragma once

#include "ui/pages/page_lucky.h"

class PageLucky320x240 : public PageLuckyBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "lucky320x240"; }

protected:
    void _create_dynamic(lv_obj_t* parent) override;
};
