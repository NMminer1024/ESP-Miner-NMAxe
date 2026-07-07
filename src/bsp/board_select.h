// What: Compile-time board-to-BSP mapping for the current firmware build.
// Why: Exactly one concrete BSP must be selected before the generic board
// accessor can instantiate it.
// Role: Maps `BOARD_*` build flags onto the concrete `ActiveBoard` type.
// Benefit: Keeps board selection explicit and local to the BSP layer instead of
// spreading preprocessor conditionals through the rest of the project.
#pragma once

// Phase-1 placeholder mapping:
// all current BOARD_* envs temporarily point to the same NMAxeGamma BSP
// so we can validate the new framework shape before splitting real boards.
// TODO(agent): replace this temporary fan-in mapping with one-to-one BOARD_* -> BSP wiring.
#if defined(BOARD_NMAXE) || defined(BOARD_NMAXE_GAMMA) || \
    defined(BOARD_NMQAXE_PP) || defined(BOARD_NMQAXE_PP_REV61) || \
    defined(BOARD_NMQAXE_PP_REV81)
#include "bsp/nmaxe_gamma/board.h"
namespace nm::bsp {
using ActiveBoard = nmaxe_gamma::NMAxeGammaBoard;
}
#else
#error "No BSP selected. Define one BOARD_* macro in platformio.ini."
#endif
