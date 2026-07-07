// What: Abstract board contract for BSP-first application startup.
// Why: Upper layers need one stable interface for board traits, policies, and
// driver handles without touching board-specific pins or compile-time macros.
// Role: Defines the minimal API every concrete BSP must implement.
// Benefit: Application and UI code can stay hardware-agnostic while each board
// keeps full control over its own low-level wiring and bring-up sequence.
#pragma once

#include "bsp/board_types.h"

namespace nm::bsp {

class Board {
public:
    virtual ~Board() = default;

    virtual void init() = 0;
    virtual const char* key() const = 0;
    virtual const BoardContext& context() const = 0;

    const BoardTraits& traits() const { return *context().traits; }
    const BoardPolicies& policies() const { return *context().policies; }
    const DisplayProfile& display_profile() const { return *context().display; }
    const ThermalProfile& thermal_profile() const { return *context().thermal; }
    const MiningProfile& mining_profile() const { return *context().mining; }
    const InputProfile& input_profile() const { return *context().input; }
    const BoardDrivers& drivers() const { return *context().drivers; }
};

Board& board();

}  // namespace nm::bsp
