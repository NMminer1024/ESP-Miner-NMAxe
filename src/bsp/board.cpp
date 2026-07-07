// What: Global access point for the single active BSP instance.
// Why: The build selects exactly one board type at compile time, but the rest
// of the codebase should not know which concrete class was chosen.
// Role: Instantiates `ActiveBoard` from `board_select.h` and returns it as `Board`.
// Benefit: Centralizes board construction and keeps compile-time BSP selection
// out of application and UI layers.
#include "bsp/board.h"

#include "bsp/board_select.h"

namespace nm::bsp {

Board& board() {
    static ActiveBoard instance;
    return instance;
}

}  // namespace nm::bsp
