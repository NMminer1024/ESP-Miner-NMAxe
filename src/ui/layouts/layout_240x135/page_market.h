#pragma once

#include "ui/pages/page_market.h"

class PageMarket240x135 : public PageMarketBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "market240x135"; }

protected:
    void _create_dynamic(lv_obj_t* parent) override;
};
