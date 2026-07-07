// What: Minimal UI bootstrap surface for the current firmware.
// Why: The application layer should start and poll the UI without knowing LVGL
// details, page construction order, or display-port internals.
// Role: Exposes the two top-level operations needed by `Application`.
// Benefit: Keeps UI startup behind a narrow API so the page tree can evolve
// independently from app and BSP code.
#pragma once

#include "bsp/board.h"

namespace nm::ui {

void boot(const bsp::Board& board);
void poll();

}  // namespace nm::ui
