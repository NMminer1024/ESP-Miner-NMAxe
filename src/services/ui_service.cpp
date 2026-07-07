// What: Concrete UI service driving the current placeholder LVGL tree.
// Why: The new architecture needs a service-level adapter between runtime state
// and the view layer before richer pages are migrated.
// Role: Starts the UI, applies event-driven page changes, and schedules renders.
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

    runtime.boot.message = "bind ui";
    if (!ui::boot(board, runtime, ui_state)) {
        runtime.boot.phase = state::BootPhase::Fault;
        runtime.boot.message = "ui bind failed";
        return false;
    }

    runtime.boot.phase = state::BootPhase::Ready;
    runtime.boot.message = "ready";
    runtime.boot.ui_ready = true;
    runtime.boot.ready = true;
    events.set(system::Event::UiReady);
    return true;
}

void UiService::poll() {
    if (_board == nullptr || _runtime == nullptr || _ui_state == nullptr || _events == nullptr) {
        return;
    }

    if (_events->consume(system::Event::UiWakeRequested)) {
        _ui_state->last_activity_ms = millis();
        _ui_state->dirty = true;
    }
    if (_events->consume(system::Event::UiNextPageRequested)) {
        _ui_state->current_page =
            _ui_state->current_page == state::UiPageId::Summary ? state::UiPageId::Detail : state::UiPageId::Summary;
        _ui_state->dirty = true;
    }
    if (_events->consume(system::Event::UiPrevPageRequested)) {
        _ui_state->current_page =
            _ui_state->current_page == state::UiPageId::Summary ? state::UiPageId::Detail : state::UiPageId::Summary;
        _ui_state->dirty = true;
    }

    const uint32_t now_ms = millis();
    const bool telemetry_dirty = _events->consume(system::Event::TelemetryUpdated);
    if (_ui_state->dirty || telemetry_dirty || (now_ms - _last_render_ms) >= 1000) {
        ui::render(*_board, *_runtime, *_ui_state);
        _ui_state->dirty = false;
        _last_render_ms = now_ms;
    }

    ui::poll();
}

}  // namespace nm::services
