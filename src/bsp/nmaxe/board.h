// What: Concrete NMAxe BSP declaration.
// Why: NMAxe and Gamma share the same 240x135 resolution but differ in ASIC
// identity, tuning, and product-facing UI content, so NMAxe needs its own BSP.
// Role: Publishes the NMAxe board context and driver assembly to the generic
// board/application layers.
// Benefit: `BOARD_NMAXE` no longer impersonates Gamma and can now diverge by
// product while still reusing shared drivers and layout infrastructure.
#pragma once

#include "bsp/board.h"

namespace nm::bsp::nmaxe {

class NMAxeBoard final : public bsp::Board {
public:
    NMAxeBoard();

    void init() override;
    const char* key() const override;
    const BoardContext& context() const override;

private:
    BoardContext _context{};
};

}  // namespace nm::bsp::nmaxe
