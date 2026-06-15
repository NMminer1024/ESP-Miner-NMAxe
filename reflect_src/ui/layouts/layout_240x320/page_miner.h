#pragma once

#include "ui/pages/page_miner.h"
#include "lvgl.h"

// ============================================================================
// PageMiner240x320 ¡ª 240x320 resolution miner dashboard
//
//   Inherits PageMinerBase: only handles layout at 240x320 coordinates.
//   Data updates driven by Observable (AppState::miner).
// ============================================================================
class PageMiner240x320 : public PageMinerBase {
public:
    void        create(lv_obj_t* parent) override;
    const char* name() const             override { return "Miner240x320"; }

protected:
    void        _create_dynamic(lv_obj_t* parent) override;
};
