// What: Service wrapper around the page-based LVGL UI framework.
// Why: The application layer should talk to a service boundary, not directly to
// rendering functions or layout-specific page objects.
// Role: Initializes the page tree, consumes navigation events, and forwards the
// runtime snapshot into rendering.
// Benefit: Keeps UI flow replaceable and aligned with the new service-oriented
// application skeleton.
// Temporary note: this service currently assumes one LVGL owner plus cheap
// polling/event-flag wakeups. That is intentional for phase-1 bring-up, but
// higher-rate async producers should later notify UI through a stronger event
// or mailbox mechanism instead of expanding this poll contract indefinitely.
#pragma once

#include "bsp/board.h"
#include "config/app_config.h"
#include "state/runtime_state.h"
#include "state/ui_state.h"
#include "system/events.h"

namespace nm::services {

class UiService {
public:
    bool start(
        const bsp::Board& board,
        const config::AppConfig& config,
        state::RuntimeState& runtime,
        state::UiState& ui_state,
        system::EventFlags& events);

    void poll();

private:
    const bsp::Board* _board = nullptr;
    const config::AppConfig* _config = nullptr;
    state::RuntimeState* _runtime = nullptr;
    state::UiState* _ui_state = nullptr;
    system::EventFlags* _events = nullptr;
    uint32_t _last_render_ms = 0;
};

}  // namespace nm::services
