// What: Minimal UI bootstrap surface for the current firmware.
// Why: The application layer should start and poll the UI without knowing LVGL
// details, page construction order, or display-port internals.
// Role: Exposes the two top-level operations needed by `Application`.
// Benefit: Keeps UI startup behind a narrow API so the page tree can evolve
// independently from app and BSP code.
#pragma once

#include "bsp/board.h"
#include "state/runtime_state.h"
#include "state/ui_state.h"

namespace nm::ui {

bool boot(const bsp::Board& board, const state::RuntimeState& runtime, const state::UiState& ui_state);
void render(const bsp::Board& board, const state::RuntimeState& runtime, const state::UiState& ui_state);
void poll();

}  // namespace nm::ui
