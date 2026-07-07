#pragma once

// Phase-1 placeholder mapping:
// all current BOARD_* envs temporarily point to the same NMAxeGamma BSP
// so we can validate the new framework shape before splitting real boards.
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
