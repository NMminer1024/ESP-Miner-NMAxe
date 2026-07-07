// What: High-level mining bring-up service for the new BSP-first framework.
// Why: The framework now has boot, monitor, and UI layers, so the core ASIC
// business path needs its own service boundary instead of living in board code.
// Role: Waits for power readiness, applies the target ASIC profile, and
// publishes mining lifecycle state through the shared runtime model.
// Benefit: Lets future stratum/web layers command mining through one service
// without reaching into BSPs or chip drivers.
#pragma once

#include "bsp/board.h"
#include "config/app_config.h"
#include "state/runtime_state.h"
#include "system/events.h"

namespace nm::services {

class MiningService {
public:
    void start(
        const bsp::Board& board,
        const config::AppConfig& config,
        state::RuntimeState& runtime,
        system::EventFlags& events);

    void poll();

private:
    void _set_phase(state::MiningPhase phase, const char* message);
    void _publish_asic_status();
    uint32_t _read_vbus_mv();
    bool _power_ready() const;
    void _fail(const char* message);

    const bsp::Board* _board = nullptr;
    const config::AppConfig* _config = nullptr;
    state::RuntimeState* _runtime = nullptr;
    system::EventFlags* _events = nullptr;
    uint32_t _last_probe_attempt_ms = 0;
};

}  // namespace nm::services
