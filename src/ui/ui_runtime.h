// What: UI runtime bootstrap surface for the page-based LVGL framework.
// Why: The application layer should start and render UI without knowing LVGL
// widget ownership, layout selection, or page construction details.
// Role: Exposes the top-level operations needed by the UI service.
// Benefit: Keeps BSP/app layers insulated while the page tree grows toward the
// full legacy UI structure.
#pragma once

#include "bsp/board.h"
#include "config/app_config.h"
#include "state/runtime_state.h"
#include "state/ui_state.h"

namespace nm::ui {

bool boot_runtime(
    const bsp::Board& board,
    const config::AppConfig& config,
    const state::RuntimeState& runtime,
    const state::UiState& ui_state);
void render_runtime(
    const bsp::Board& board,
    const config::AppConfig& config,
    const state::RuntimeState& runtime,
    const state::UiState& ui_state);
void poll_runtime();

}  // namespace nm::ui
