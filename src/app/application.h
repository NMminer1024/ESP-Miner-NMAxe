// What: Top-level application orchestrator for one firmware image.
// Why: Even in a BSP-first project, something still needs to define the order of
// serial boot, board bring-up, UI startup, and the main polling loop.
// Role: Owns the high-level runtime sequence above BSP and UI internals.
// Benefit: Startup policy is centralized, making the firmware easier to reason
// about and preventing framework code from leaking into Arduino globals.
#pragma once

#include "config/app_config.h"
#include "config/config_store.h"
#include "services/boot_service.h"
#include "services/mining_service.h"
#include "services/monitor_service.h"
#include "services/ui_service.h"
#include "state/runtime_state.h"
#include "state/ui_state.h"
#include "system/events.h"

namespace nm::bsp {
class Board;
}

namespace nm {

class Application {
public:
    static Application& instance();

    void setup();
    void loop();

    const bsp::Board& board() const;
    const config::AppConfig& config() const { return _config; }
    const state::RuntimeState& runtime() const { return _runtime; }

private:
    Application() = default;

    bsp::Board* _board = nullptr;
    bool _initialized = false;
    bool _services_started = false;
    bool _boot_slide_complete = false;
    config::NvsConfigStore _config_store{};
    config::AppConfig _config{};
    state::RuntimeState _runtime{};
    state::UiState _ui_state{};
    system::EventFlags _events{};
    services::BootService _boot_service{};
    services::MiningService _mining_service{};
    services::MonitorService _monitor_service{};
    services::UiService _ui_service{};
};

}  // namespace nm
