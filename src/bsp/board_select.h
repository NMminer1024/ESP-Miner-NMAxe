// What: Compile-time board-to-BSP mapping for the current firmware build.
// Why: Exactly one concrete BSP must be selected before the generic board
// accessor can instantiate it.
// Role: Maps `BOARD_*` build flags onto the concrete `ActiveBoard` type.
// Benefit: Keeps board selection explicit and local to the BSP layer instead of
// spreading preprocessor conditionals through the rest of the project.
#pragma once

// Phase-1 board mapping:
// only BSPs that already have concrete implementations are wired here.
// Unimplemented BOARD_* targets fail at compile time so missing support stays
// explicit instead of silently impersonating another product.
#if defined(BOARD_NMAXE)
    #include "bsp/nmaxe/board.h"
    namespace nm::bsp {
    using ActiveBoard = nmaxe::NMAxeBoard;
    }
#elif defined(BOARD_NMAXE_GAMMA)
    #include "bsp/nmaxe_gamma/board.h"
    namespace nm::bsp {
    using ActiveBoard = nmaxe_gamma::NMAxeGammaBoard;
    }
#elif defined(BOARD_NMQAXE_PP) || defined(BOARD_NMQAXE_PP_REV61) || defined(BOARD_NMQAXE_PP_REV81)
    #include "bsp/nmqaxe_pp/board.h"
    namespace nm::bsp {
    using ActiveBoard = nmqaxe_pp::NMQAxePPBoard;
    }
#else
#error "No BSP selected. Define one BOARD_* macro in platformio.ini."
#endif
