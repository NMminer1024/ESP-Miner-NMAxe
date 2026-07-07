// What: Top-level application orchestrator for one firmware image.
// Why: Even in a BSP-first project, something still needs to define the order of
// serial boot, board bring-up, UI startup, and the main polling loop.
// Role: Owns the high-level runtime sequence above BSP and UI internals.
// Benefit: Startup policy is centralized, making the firmware easier to reason
// about and preventing framework code from leaking into Arduino globals.
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "app/task_config.h"
#include "config/app_config.h"
#include "config/config_store.h"
#include "services/boot_service.h"
#include "services/mining_service.h"
#include "services/monitor_service.h"
#include "services/stratum_service.h"
#include "services/ui_service.h"
#include "services/wifi_service.h"
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

    void _start_tasks();
    void _run_app_once();
    void _run_ui_once();
    void _run_wifi_once();
    void _lock_state();
    void _unlock_state();

    static void _app_task_entry(void* args);
    static void _ui_task_entry(void* args);
    static void _wifi_task_entry(void* args);

    bsp::Board* _board = nullptr;
    bool _initialized = false;
    bool _tasks_started = false;
    bool _services_started = false;
    bool _wifi_started = false;
    bool _stratum_started = false;
    bool _boot_slide_complete = false;
    SemaphoreHandle_t _state_mutex = nullptr;
    TaskHandle_t _app_task = nullptr;
    TaskHandle_t _ui_task = nullptr;
    TaskHandle_t _wifi_task = nullptr;
    config::NvsConfigStore _config_store{};
    config::AppConfig _config{};
    state::RuntimeState _runtime{};
    state::UiState _ui_state{};
    system::EventFlags _events{};
    services::BootService _boot_service{};
    services::MiningService _mining_service{};
    services::MonitorService _monitor_service{};
    services::StratumService _stratum_service{};
    services::UiService _ui_service{};
    services::WifiService _wifi_service{};
};

}  // namespace nm
