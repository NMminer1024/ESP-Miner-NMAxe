#pragma once

#include "bsp/board.h"
#include "product/ui_profiles/ui_profile.h"

namespace nm::product {

const UiProfile& active_ui_profile(const bsp::Board& board);

}  // namespace nm::product
