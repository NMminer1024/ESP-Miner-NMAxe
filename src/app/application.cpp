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

    _state_mutex = xSemaphoreCreateMutex();
    if (_state_mutex == nullptr) {
        Serial.println("[app] state mutex create failed");
        return;
    }

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
    _wifi_started = false;
    _services_started = false;
    _boot_slide_complete = false;
    _initialized = true;
    _start_tasks();
}

void Application::loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}

void Application::_start_tasks() {
    if (_tasks_started || !_initialized || _board == nullptr) {
        return;
    }

    const BaseType_t wifi_ok = xTaskCreatePinnedToCore(
        _wifi_task_entry,
        "(wifi)",
        app::kWifiTaskStackBytes,
        this,
        app::kTaskPriorityWifi,
        &_wifi_task,
        app::kTaskCoreNet);
    if (wifi_ok != pdPASS) {
        Serial.println("[app] failed to create wifi task");
        return;
    }

    const BaseType_t app_ok = xTaskCreatePinnedToCore(
        _app_task_entry,
        "(app)",
        app::kAppServiceTaskStackBytes,
        this,
        app::kTaskPriorityAppService,
        &_app_task,
        app::kTaskCoreUi);
    if (app_ok != pdPASS) {
        Serial.println("[app] failed to create app service task");
        if (_wifi_task != nullptr) {
            vTaskDelete(_wifi_task);
            _wifi_task = nullptr;
        }
        return;
    }

    const BaseType_t ui_ok = xTaskCreatePinnedToCore(
        _ui_task_entry,
        "(ui)",
        app::kUiTaskStackBytes,
        this,
        app::kTaskPriorityLvgl,
        &_ui_task,
        app::kTaskCoreUi);
    if (ui_ok != pdPASS) {
        Serial.println("[app] failed to create ui task");
        if (_app_task != nullptr) {
            vTaskDelete(_app_task);
            _app_task = nullptr;
        }
        if (_wifi_task != nullptr) {
            vTaskDelete(_wifi_task);
            _wifi_task = nullptr;
        }
        return;
    }

    _tasks_started = true;
    Serial.printf(
        "[app] tasks started wifi_prio=%u app_prio=%u ui_prio=%u ui_core=%u net_core=%u\n",
        static_cast<unsigned>(app::kTaskPriorityWifi),
        static_cast<unsigned>(app::kTaskPriorityAppService),
        static_cast<unsigned>(app::kTaskPriorityLvgl),
        static_cast<unsigned>(app::kTaskCoreUi),
        static_cast<unsigned>(app::kTaskCoreNet));
}

void Application::_run_app_once() {
    if (!_initialized || _board == nullptr) {
        return;
    }

    _boot_service.poll();

    if (_boot_service.waiting_for_wifi() && !_wifi_started) {
        _wifi_service.start(_config, _runtime, _events);
        _wifi_started = true;
    }

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
                    state::publish_boot_state(
                        _runtime.boot,
                        state::BootPhase::Ready,
                        "Miner ready!",
                        100,
                        0x00FF00,
                        millis());
                    _ui_state.current_page = state::UiPageId::Miner;
                    _ui_state.dirty = true;
                    _boot_slide_complete = true;
                    break;

                case state::MiningPhase::Disabled:
                    state::publish_boot_state(
                        _runtime.boot,
                        state::BootPhase::Ready,
                        "miner disabled",
                        100,
                        0xFFFFFF,
                        millis());
                    _ui_state.current_page = state::UiPageId::Miner;
                    _ui_state.dirty = true;
                    _boot_slide_complete = true;
                    break;

                case state::MiningPhase::Fault:
                    state::publish_boot_state(
                        _runtime.boot,
                        state::BootPhase::Fault,
                        "miner fault",
                        100,
                        0xFF0000,
                        millis());
                    _ui_state.current_page = state::UiPageId::Miner;
                    _ui_state.dirty = true;
                    _boot_slide_complete = true;
                    break;

                case state::MiningPhase::WaitPower:
                case state::MiningPhase::Probe:
                case state::MiningPhase::AsicConfirm:
                case state::MiningPhase::TempCheck:
                case state::MiningPhase::TempConfirm:
                case state::MiningPhase::FanPolarityCheck:
                case state::MiningPhase::FanPolarityConfirm:
                case state::MiningPhase::FanSelfTest:
                case state::MiningPhase::FanSelfTestConfirm:
                case state::MiningPhase::WaitVbus:
                case state::MiningPhase::WaitVcore:
                case state::MiningPhase::WaitVcoreConfirm:
                case state::MiningPhase::Bringup:
                    break;
            }
        }
    }
}

void Application::_run_ui_once() {
    if (!_initialized || _board == nullptr) {
        return;
    }
    _ui_service.poll();
}

void Application::_run_wifi_once() {
    if (!_initialized || _board == nullptr || !_wifi_started) {
        return;
    }
    _wifi_service.poll();
}

void Application::_lock_state() {
    if (_state_mutex != nullptr) {
        xSemaphoreTake(_state_mutex, portMAX_DELAY);
    }
}

void Application::_unlock_state() {
    if (_state_mutex != nullptr) {
        xSemaphoreGive(_state_mutex);
    }
}

void Application::_app_task_entry(void* args) {
    auto* app = static_cast<Application*>(args);
    if (app == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    while (true) {
        app->_lock_state();
        app->_run_app_once();
        app->_unlock_state();
        vTaskDelay(pdMS_TO_TICKS(app::kAppServiceTaskPeriodMs));
    }
}

void Application::_ui_task_entry(void* args) {
    auto* app = static_cast<Application*>(args);
    if (app == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    while (true) {
        app->_lock_state();
        app->_run_ui_once();
        app->_unlock_state();
        vTaskDelay(pdMS_TO_TICKS(app::kUiTaskPeriodMs));
    }
}

void Application::_wifi_task_entry(void* args) {
    auto* app = static_cast<Application*>(args);
    if (app == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    while (true) {
        app->_lock_state();
        app->_run_wifi_once();
        app->_unlock_state();
        vTaskDelay(pdMS_TO_TICKS(app::kWifiTaskPeriodMs));
    }
}

const bsp::Board& Application::board() const {
    return *_board;
}

}  // namespace nm
