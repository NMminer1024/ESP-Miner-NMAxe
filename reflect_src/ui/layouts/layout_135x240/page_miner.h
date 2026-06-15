#pragma once

#include "ui/pages/page_miner.h"
#include "lvgl.h"

class PageMiner135x240 : public PageMinerBase {
public:
    void        create(lv_obj_t* parent) override;
    const char* name() const             override { return "Miner135x240"; }
protected:
    void        _create_dynamic(lv_obj_t* parent) override;
};
