// What: Concrete QAxe++ BSP declaration.
// Why: QAxe++ hardware differs from NMAxe/Gamma and needs its own board-owned
// driver bundle instead of being blocked at board selection.
// Role: Publishes QAxe++ traits, policies, display wiring, fans, buttons, and
// mining transport to the generic board/application layers.
// Benefit: QAxe++ can boot through the same BSP-first path as Axe and Gamma so
// product issues surface in parallel instead of late in the migration.
#pragma once

#include "bsp/board.h"

namespace nm::bsp::nmqaxe_pp {

class NMQAxePPBoard final : public bsp::Board {
public:
    NMQAxePPBoard();

    void init() override;
    const char* key() const override;
    const BoardContext& context() const override;

private:
    BoardContext _context{};
};

}  // namespace nm::bsp::nmqaxe_pp
