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
