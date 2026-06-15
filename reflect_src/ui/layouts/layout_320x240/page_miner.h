#pragma once

#include "ui/pages/page_miner.h"
#include "lvgl.h"

// ============================================================================
// PageMiner320x240 — 320x240 resolution miner dashboard
//
//   Inherits PageMinerBase: only handles layout at 320x240 coordinates.
//   Data updates driven by Observable (AppState::miner).
// ============================================================================
class PageMiner320x240 : public PageMinerBase {
public:
    void        create(lv_obj_t* parent) override;
    const char* name() const             override { return "Miner320x240"; }

protected:
    void        _create_dynamic(lv_obj_t* parent) override;
};
