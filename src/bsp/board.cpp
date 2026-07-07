#include "bsp/board.h"

#include "bsp/board_select.h"

namespace nm::bsp {

Board& board() {
    static ActiveBoard instance;
    return instance;
}

}  // namespace nm::bsp
