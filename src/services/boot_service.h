// What: High-level boot orchestrator for the new BSP-first service layer.
// Why: Application startup should be expressed in service terms rather than as
// direct board-driver calls from `Application`.
// Role: Loads config, initializes the active BSP, applies default operating
// settings, and runs early self-tests through abstract interfaces.
// Benefit: Centralizes boot policy while keeping board specifics behind drivers.
#pragma once

#include "bsp/board.h"
#include "config/config_store.h"
#include "state/runtime_state.h"
#include "state/ui_state.h"
#include "system/events.h"

namespace nm::services {

class BootService {
public:
    bool start(
        bsp::Board& board,
        config::ConfigStore& config_store,
        config::AppConfig& config,
        state::RuntimeState& runtime,
        state::UiState& ui_state,
        system::EventFlags& events);
};

}  // namespace nm::services
