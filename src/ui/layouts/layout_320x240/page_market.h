#pragma once

#include "ui/pages/page_market.h"

class PageMarket320x240 : public PageMarketBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "market320x240"; }

protected:
    void _create_dynamic(lv_obj_t* parent) override;
};
