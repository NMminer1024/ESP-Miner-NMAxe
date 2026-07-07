// What: Concrete boot service for the current minimal startup path.
// Why: The new architecture needs an explicit boot-policy layer before mining
// and networking are migrated.
// Role: Drives config load, board init, power defaults, fan self-test, and
// initial UI-related runtime state.
// Benefit: Establishes the future startup skeleton without leaking hardware
// details into `Application`.
#include "services/boot_service.h"

#include <Arduino.h>

namespace nm::services {

bool BootService::start(
    bsp::Board& board,
    config::ConfigStore& config_store,
    config::AppConfig& config,
    state::RuntimeState& runtime,
    state::UiState& ui_state,
    system::EventFlags& events) {
    _board = &board;
    _config = &config;
    _runtime = &runtime;
    _ui_state = &ui_state;
    _events = &events;
    _stage = Stage::Idle;
    _target_brightness_percent = 0;
    _current_brightness_percent = 0;
    _last_backlight_step_ms = millis();

    runtime.boot = {};
    _set_boot_state(state::BootPhase::LoadConfig, "load config", 5);

    if (!config_store.init()) {
        return _fail("config store init failed");
    }
    if (!config_store.load(board, config)) {
        return _fail("config load failed");
    }

    ui_state.current_page = state::UiPageId::Loading;
    ui_state.last_activity_ms = millis();
    ui_state.dirty = true;

    _set_boot_state(state::BootPhase::InitBoard, "init board", 10);
    board.init();
    runtime.boot.board_ready = true;
    events.set(system::Event::BoardReady);

    runtime.fan_count = 0;
    runtime.button_count = 0;

    if (board.drivers().display != nullptr) {
        board.drivers().display->set_flip(config.screen.flip);
        board.drivers().display->set_brightness_percent(0);
    }

    _target_brightness_percent =
        config.screen.brightness_percent != 0 ? config.screen.brightness_percent : board.policies().default_brightness_pct;
    _current_brightness_percent = 0;
    _set_boot_state(state::BootPhase::InitUi, "ui bind pending", 15);
    _stage = Stage::FadeBacklight;
    return true;
}

void BootService::poll() {
    if (_board == nullptr || _config == nullptr || _runtime == nullptr || _ui_state == nullptr || _events == nullptr) {
        return;
    }

    switch (_stage) {
        case Stage::Idle:
        case Stage::Complete:
        case Stage::Fault:
            return;

        case Stage::FadeBacklight: {
            if (!_runtime->boot.ui_ready) {
                return;
            }

            auto* display = _board->drivers().display;
            if (display == nullptr || _target_brightness_percent == 0) {
                _advance(Stage::ApplyPowerDefaults, state::BootPhase::InitPower, "apply power defaults", 30);
                return;
            }

            const uint32_t now_ms = millis();
            if ((now_ms - _last_backlight_step_ms) < 10u) {
                return;
            }
            _last_backlight_step_ms = now_ms;

            if (_current_brightness_percent < _target_brightness_percent) {
                ++_current_brightness_percent;
                display->set_brightness_percent(_current_brightness_percent);
                const uint8_t fade_span = static_cast<uint8_t>(30u - 15u);
                _runtime->boot.progress_percent =
                    static_cast<uint8_t>(15u + ((_current_brightness_percent * fade_span) / _target_brightness_percent));
                _ui_state->dirty = true;
                return;
            }

            _advance(Stage::ApplyPowerDefaults, state::BootPhase::InitPower, "apply power defaults", 30);
            return;
        }

        case Stage::ApplyPowerDefaults:
            if (_board->drivers().power != nullptr) {
                _board->drivers().power->set_vcore_limits(_board->policies().min_vcore_mv, _board->policies().max_vcore_mv);
                _board->drivers().power->set_vcore_mv(_config->mining.target_vcore_mv);

                // Preserve the legacy two-stage ASIC power-up order:
                // 1. Enable only the digital rails first so ASIC probe/count can happen
                //    before Vcore is raised.
                // 2. Vcore itself is enabled later by the mining service after probe
                //    succeeds and any higher-level gating policy is satisfied.
                _board->drivers().power->set_rail_enabled(drivers::PowerRail::Pll0v8, true);
                _board->drivers().power->set_rail_enabled(drivers::PowerRail::Vdd1v8, true);
                _board->drivers().power->set_rail_enabled(drivers::PowerRail::Vcore, false);
            }

            _advance(Stage::InitCooling, state::BootPhase::InitCooling, "fan self-test", 45);
            return;

        case Stage::InitCooling:
            _runtime->fan_count = 0;
            for (size_t i = 0; i < _board->drivers().fans.size() && i < state::kMaxFans; ++i) {
                auto* fan = _board->drivers().fans[i];
                if (fan == nullptr) {
                    continue;
                }
                fan->set_speed_percent(100);
                const auto self_test = fan->run_self_test();
                _runtime->fans[i].present = true;
                _runtime->fans[i].self_test_passed = self_test.passed;
                _runtime->fans[i].speed_percent = fan->speed_percent();
                _runtime->fans[i].rpm = self_test.rpm;
                _runtime->fan_count++;
            }

            _advance(Stage::RegisterInputs, state::BootPhase::InitUi, "bind inputs", 65);
            return;

        case Stage::RegisterInputs:
            _runtime->button_count = 0;
            for (size_t i = 0; i < _board->drivers().buttons.size() && i < state::kMaxButtons; ++i) {
                _runtime->buttons[i].present = false;
                if (_board->drivers().buttons[i] != nullptr) {
                    _runtime->buttons[i].present = true;
                    _runtime->button_count++;
                }
            }

            _runtime->boot.ready = true;
            _advance(Stage::WaitServicesStart, state::BootPhase::Ready, "core init ready", 90);
            return;

        case Stage::WaitServicesStart:
            return;
    }
}

bool BootService::ready_for_services() const {
    return _stage == Stage::WaitServicesStart || _stage == Stage::Complete;
}

void BootService::mark_services_started() {
    if (_stage != Stage::WaitServicesStart || _runtime == nullptr || _ui_state == nullptr) {
        return;
    }

    _stage = Stage::Complete;
    _set_boot_state(state::BootPhase::Ready, "wait miner ready", 95);
}

void BootService::_set_boot_state(state::BootPhase phase, const char* message, uint8_t progress_percent) {
    if (_runtime == nullptr) {
        return;
    }

    _runtime->boot.phase = phase;
    _runtime->boot.message = message;
    _runtime->boot.progress_percent = progress_percent;
    if (_ui_state != nullptr) {
        _ui_state->dirty = true;
    }
}

void BootService::_advance(Stage next_stage, state::BootPhase phase, const char* message, uint8_t progress_percent) {
    _stage = next_stage;
    _set_boot_state(phase, message, progress_percent);
}

bool BootService::_fail(const char* message) {
    _stage = Stage::Fault;
    _set_boot_state(state::BootPhase::Fault, message, 100);
    if (_runtime != nullptr) {
        _runtime->boot.ready = false;
    }
    return false;
}

}  // namespace nm::services
