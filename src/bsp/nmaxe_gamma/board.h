// What: Concrete Gamma BSP declaration.
// Why: Gamma still needs one board-owned type that publishes traits, policies,
// and driver instances to the rest of the firmware.
// Role: Declares the Gamma board object that binds board metadata to shared
// driver implementations.
// Benefit: The board stays easy to scan because controller-specific logic lives
// in reusable drivers while Gamma keeps only its hardware description.
#pragma once

#include "bsp/board.h"

namespace nm::bsp::nmaxe_gamma {

class NMAxeGammaBoard final : public bsp::Board {
public:
    NMAxeGammaBoard();

    void init() override;
    const char* key() const override;
    const BoardContext& context() const override;

private:
    BoardContext _context{};
};

}  // namespace nm::bsp::nmaxe_gamma
