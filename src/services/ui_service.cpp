// What: Concrete UI service driving the page-based LVGL tree.
// Why: The new architecture needs a service-level adapter between runtime state
// and the layout/page layer while UI migration proceeds incrementally.
// Role: Starts the UI, applies page-navigation events, and schedules renders.
// Benefit: Keeps rendering concerns away from `Application` and other services.
#include "services/ui_service.h"

#include <Arduino.h>

#include "ui/ui_root.h"

namespace nm::services {

bool UiService::start(
    const bsp::Board& board,
    const config::AppConfig& config,
    state::RuntimeState& runtime,
    state::UiState& ui_state,
    system::EventFlags& events) {
    _board = &board;
    _config = &config;
    _runtime = &runtime;
    _ui_state = &ui_state;
    _events = &events;
    _last_render_ms = 0;

    if (!ui::boot(board, config, runtime, ui_state)) {
        runtime.boot.phase = state::BootPhase::Fault;
        runtime.boot.message = "ui bind failed";
        return false;
    }

    runtime.boot.ui_ready = true;
    events.set(system::Event::UiReady);
    ui_state.dirty = true;
    return true;
}

void UiService::poll() {
    if (_board == nullptr || _runtime == nullptr || _ui_state == nullptr || _events == nullptr) {
        return;
    }

    // Phase-1 event consumption:
    // These `consume()` calls intentionally coalesce repeated requests because
    // the current framework only needs lightweight UI wake/page toggles. When
    // async producers arrive, upgrade the event transport instead of encoding
    // more semantics into this temporary bitflag pattern.
    if (_events->consume(system::Event::UiWakeRequested)) {
        _ui_state->last_activity_ms = millis();
        _ui_state->dirty = true;
    }
    if (_events->consume(system::Event::UiNextPageRequested)) {
        _ui_state->current_page = state::next_runtime_ui_page(_ui_state->current_page);
        _ui_state->dirty = true;
    }
    if (_events->consume(system::Event::UiPrevPageRequested)) {
        _ui_state->current_page = state::prev_runtime_ui_page(_ui_state->current_page);
        _ui_state->dirty = true;
    }

    const uint32_t now_ms = millis();
    const bool telemetry_dirty = _events->consume(system::Event::TelemetryUpdated);
    const bool mining_dirty = _events->consume(system::Event::MiningStateChanged);
    // Temporary render trigger:
    // The 1 s fallback refresh is useful while the runtime snapshot is still
    // small and mostly polled. If later pages depend on richer async data,
    // prefer explicit invalidation/messages over tightening this loop.
    if (_ui_state->dirty || telemetry_dirty || mining_dirty || (now_ms - _last_render_ms) >= 1000) {
        ui::render(*_board, *_config, *_runtime, *_ui_state);
        _ui_state->dirty = false;
        _last_render_ms = now_ms;
    }

    ui::poll();
}

}  // namespace nm::services
