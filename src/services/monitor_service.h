// What: Runtime polling service for board telemetry and user input.
// Why: The future main flow needs a single place to translate abstract driver
// reads into runtime state and service-level events.
// Role: Samples power, temperatures, fan RPM, and button events on a cadence.
// Benefit: UI and future network/mining layers can consume stable state instead
// of talking to devices directly.
#pragma once

#include "bsp/board.h"
#include "config/app_config.h"
#include "state/runtime_state.h"
#include "system/events.h"

namespace nm::services {

class MonitorService {
public:
    void start(
        const bsp::Board& board,
        const config::AppConfig& config,
        state::RuntimeState& runtime,
        system::EventFlags& events);

    void poll();

private:
    void _poll_buttons(uint32_t now_ms);
    void _poll_telemetry(uint32_t now_ms);

    const bsp::Board* _board = nullptr;
    const config::AppConfig* _config = nullptr;
    state::RuntimeState* _runtime = nullptr;
    system::EventFlags* _events = nullptr;
    uint32_t _last_button_poll_ms = 0;
    uint32_t _last_telemetry_poll_ms = 0;
};

}  // namespace nm::services
