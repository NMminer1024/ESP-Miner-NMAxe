#pragma once

#include "ui/pages/page_miner.h"
#include "lvgl.h"

class PageMiner240x135 : public PageMinerBase {
public:
    void        create(lv_obj_t* parent) override;
    const char* name() const             override { return "Miner240x135"; }
protected:
    void        _create_dynamic(lv_obj_t* parent) override;
};
