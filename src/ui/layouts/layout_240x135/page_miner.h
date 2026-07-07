// What: 240x135 miner-page concrete class.
// Why: The miner page will eventually use old assets and coordinates that are
// specific to this resolution.
// Role: Binds the miner-page base to the 240x135 layout metrics.
// Benefit: Resolution-specific miner work remains isolated in this file set.
#pragma once

#include "ui/pages/page_miner.h"

namespace nm::ui {

class PageMiner240x135 : public PageMinerBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "PageMiner240x135"; }
};

}  // namespace nm::ui
