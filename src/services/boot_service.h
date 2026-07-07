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

    void poll();
    bool ready_for_services() const;
    void mark_services_started();

private:
    enum class Stage : uint8_t {
        Idle = 0,
        FadeBacklight = 1,
        ApplyPowerDefaults = 2,
        InitCooling = 3,
        RegisterInputs = 4,
        WaitServicesStart = 5,
        Complete = 6,
        Fault = 7,
    };

    void _set_boot_state(state::BootPhase phase, const char* message, uint8_t progress_percent);
    void _advance(Stage next_stage, state::BootPhase phase, const char* message, uint8_t progress_percent);
    bool _fail(const char* message);

    bsp::Board* _board = nullptr;
    config::AppConfig* _config = nullptr;
    state::RuntimeState* _runtime = nullptr;
    state::UiState* _ui_state = nullptr;
    system::EventFlags* _events = nullptr;
    Stage _stage = Stage::Idle;
    uint8_t _target_brightness_percent = 0;
    uint8_t _current_brightness_percent = 0;
    uint32_t _last_backlight_step_ms = 0;
};

}  // namespace nm::services
