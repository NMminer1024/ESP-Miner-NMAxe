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
