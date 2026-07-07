// What: Concrete application bootstrap and main-loop implementation.
// Why: This is where the selected BSP and UI framework are actually stitched
// together at runtime after the firmware image starts.
// Role: Initializes serial logging, boots the active board, and pumps UI polling.
// Benefit: Keeps the execution order explicit and makes framework bring-up easy
// to inspect or adjust without touching board-specific code.
#include "app/application.h"

#include <Arduino.h>

#include "bsp/board.h"

namespace nm {

Application& Application::instance() {
    static Application app;
    return app;
}

void Application::setup() {
    if (_initialized) {
        return;
    }

    Serial.begin(115200);
    delay(50);
    Serial.println();
    Serial.println("[app] BSP-first skeleton boot");

    _board = &bsp::board();
    if (!_boot_service.start(*_board, _config_store, _config, _runtime, _ui_state, _events)) {
        Serial.printf("[app] boot failed at phase=%u msg=%s\n",
                      static_cast<unsigned>(_runtime.boot.phase),
                      _runtime.boot.message);
        return;
    }
    if (!_ui_service.start(*_board, _config, _runtime, _ui_state, _events)) {
        Serial.printf("[app] ui start failed at phase=%u msg=%s\n",
                      static_cast<unsigned>(_runtime.boot.phase),
                      _runtime.boot.message);
        return;
    }

    _services_started = false;
    _boot_slide_complete = false;
    _initialized = true;
}

void Application::loop() {
    if (!_initialized || _board == nullptr) {
        return;
    }

    // Phase-1 temporary scheduler:
    // Keep the early framework in one simple, non-blocking loop until the BSP,
    // state, and service seams are stable. Do not add blocking market/stratum/
    // web/mining work here. Those subsystems should later move to dedicated
    // tasks/executors and feed state/events back into this layer.
    _boot_service.poll();

    if (_boot_service.ready_for_services() && !_services_started) {
        _monitor_service.start(*_board, _config, _runtime, _events);
        _mining_service.start(*_board, _config, _runtime, _events);
        _boot_service.mark_services_started();
        _services_started = true;
    }

    if (_services_started) {
        _mining_service.poll();
        _monitor_service.poll();

        if (!_boot_slide_complete) {
            switch (_runtime.mining.phase) {
                case state::MiningPhase::Standby:
                case state::MiningPhase::Running:
                    _runtime.boot.progress_percent = 100;
                    _runtime.boot.message = "miner ready";
                    _ui_state.current_page = state::UiPageId::Miner;
                    _ui_state.dirty = true;
                    _boot_slide_complete = true;
                    break;

                case state::MiningPhase::Disabled:
                    _runtime.boot.progress_percent = 100;
                    _runtime.boot.message = "miner disabled";
                    _ui_state.current_page = state::UiPageId::Miner;
                    _ui_state.dirty = true;
                    _boot_slide_complete = true;
                    break;

                case state::MiningPhase::Fault:
                    _runtime.boot.progress_percent = 100;
                    _runtime.boot.message = "miner fault";
                    _ui_state.current_page = state::UiPageId::Miner;
                    _ui_state.dirty = true;
                    _boot_slide_complete = true;
                    break;

                case state::MiningPhase::WaitPower:
                case state::MiningPhase::Probe:
                case state::MiningPhase::WaitVbus:
                case state::MiningPhase::WaitVcore:
                case state::MiningPhase::Bringup:
                    break;
            }
        }
    }

    _ui_service.poll();
}

const bsp::Board& Application::board() const {
    return *_board;
}

}  // namespace nm
