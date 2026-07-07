// What: Concrete boot service for the current minimal startup path.
// Why: The new architecture needs an explicit boot-policy layer before mining
// and networking are migrated.
// Role: Drives config load, board init, power defaults, fan self-test, and
// initial UI-related runtime state.
// Benefit: Establishes the future startup skeleton without leaking hardware
// details into `Application`.
#include "services/boot_service.h"

#include <Arduino.h>
#include <stdio.h>

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
    _stage_started_ms = millis();

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
    _set_boot_state(state::BootPhase::InitPower, "Vbus check   ", 10);
    _stage = Stage::WaitAdc;
    _stage_started_ms = millis();
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

        case Stage::WaitAdc: {
            const uint32_t now_ms = millis();
            static const char* const kVbusCheck[] = {
                "Vbus check   ", "Vbus check.  ", "Vbus check.. ", "Vbus check..."
            };
            const uint8_t anim = static_cast<uint8_t>(((now_ms - _stage_started_ms) / 300u) % 4u);
            _set_boot_state(state::BootPhase::InitPower, kVbusCheck[anim], 10);

            auto* power = _board->drivers().power;
            if (power == nullptr || power->adc_ready()) {
                _advance(
                    Stage::WaitVbus,
                    state::BootPhase::InitPower,
                    power != nullptr && power->is_dc_plugged() ? "DC pluged." : "USB pluged.",
                    20,
                    0x00FF00);
            }
            return;
        }

        case Stage::WaitVbus: {
            auto* power = _board->drivers().power;
            const uint32_t now_ms = millis();
            const uint32_t elapsed_ms = now_ms - _stage_started_ms;
            if (power == nullptr) {
                _advance_silent(Stage::FadeBacklight, state::BootPhase::InitUi);
                return;
            }

            if (elapsed_ms < 500u) {
                _set_boot_state(
                    state::BootPhase::InitPower,
                    power->is_dc_plugged() ? "DC pluged." : "USB pluged.",
                    20,
                    0x00FF00);
                return;
            }

            const uint32_t vbus_mv = power->read_vbus_mv();
            _runtime->power.vbus_mv = vbus_mv;
            _runtime->power.dc_plugged = power->is_dc_plugged();
            _runtime->power.adc_ready = power->adc_ready();

            const bool vbus_ready =
                _board->policies().vbus_min_required_mv == 0 ||
                vbus_mv >= _board->policies().vbus_min_required_mv;
            if (vbus_ready) {
                snprintf(
                    _boot_message,
                    sizeof(_boot_message),
                    "Vbus %.1fv.",
                    static_cast<double>(vbus_mv) / 1000.0);
                _set_boot_state(state::BootPhase::InitPower, _boot_message, 20, 0x00FF00);
                if (elapsed_ms >= 1000u) {
                    _advance_silent(Stage::FadeBacklight, state::BootPhase::InitUi);
                }
                return;
            }

            snprintf(
                _boot_message,
                sizeof(_boot_message),
                "Vbus %.1fv(at least%.1fv)",
                static_cast<double>(vbus_mv) / 1000.0,
                static_cast<double>(_board->policies().vbus_min_required_mv) / 1000.0);
            const bool blink = (((elapsed_ms / 500u) & 1u) == 0u);
            _set_boot_state(state::BootPhase::InitPower, _boot_message, 20, blink ? 0xFF0000 : 0xFFFFFF);
            if (elapsed_ms >= 1500u) {
                _advance_silent(Stage::FadeBacklight, state::BootPhase::InitUi);
            }
            return;
        }

        case Stage::FadeBacklight: {
            if (!_runtime->boot.ui_ready) {
                return;
            }

            auto* display = _board->drivers().display;
            if (display == nullptr || _target_brightness_percent == 0) {
                _advance_silent(Stage::ApplyPowerDefaults, state::BootPhase::InitPower);
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
                const uint8_t fade_span = static_cast<uint8_t>(30u - 20u);
                _runtime->boot.progress_percent =
                    static_cast<uint8_t>(20u + ((_current_brightness_percent * fade_span) / _target_brightness_percent));
                _ui_state->dirty = true;
                return;
            }

            _advance_silent(Stage::ApplyPowerDefaults, state::BootPhase::InitPower);
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

            _advance_silent(Stage::InitCooling, state::BootPhase::InitCooling);
            return;

        case Stage::InitCooling:
            _runtime->fan_count = 0;
            for (size_t i = 0; i < _board->drivers().fans.size() && i < state::kMaxFans; ++i) {
                auto* fan = _board->drivers().fans[i];
                if (fan == nullptr) {
                    continue;
                }
                fan->set_speed_percent(100);
                _runtime->fans[i].present = true;
                _runtime->fans[i].self_test_passed = false;
                _runtime->fans[i].speed_percent = fan->speed_percent();
                _runtime->fans[i].rpm = fan->read_rpm();
                _runtime->fan_count++;
            }

            _advance_silent(Stage::RegisterInputs, state::BootPhase::InitUi);
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
            _advance(Stage::WaitWifi, state::BootPhase::Ready, "Wifi connect   ", 30);
            return;

        case Stage::WaitWifi: {
            const uint32_t now_ms = millis();
            static const char* const kWifiConnect[] = {
                "Wifi connect   ", "Wifi connect.  ", "Wifi connect.. ", "Wifi connect..."
            };
            const uint8_t anim = static_cast<uint8_t>(((now_ms - _stage_started_ms) / state::kBootMessageMinVisibleMs) % 4u);
            snprintf(
                _boot_message,
                sizeof(_boot_message),
                "%s[%s]",
                kWifiConnect[anim],
                _config->network.sta_ssid.c_str());
            _set_boot_state(state::BootPhase::Ready, _boot_message, 30);

            if (_runtime->network.ap_ready) {
                _set_boot_state(state::BootPhase::Fault, "AP config mode", 30, 0xFF3B30);
                if (_ui_state != nullptr) {
                    _ui_state->current_page = state::UiPageId::Config;
                    _ui_state->dirty = true;
                }
                _stage = Stage::Fault;
                return;
            }

            if (_runtime->network.sta_connected) {
                _advance(Stage::WaitWifiConfirm, state::BootPhase::Ready, "Wifi Connected!", 30, 0x00FF00);
            }
            return;
        }

        case Stage::WaitWifiConfirm:
            _set_boot_state(state::BootPhase::Ready, "Wifi Connected!", 30, 0x00FF00);
            if (state::boot_message_equals(_runtime->boot.message, "Wifi Connected!") &&
                !_runtime->boot.pending_message_valid &&
                (millis() - _runtime->boot.message_changed_ms) >= state::kBootMessageMinVisibleMs) {
                _advance_silent(Stage::WaitServicesStart, state::BootPhase::Ready);
            }
            return;

        case Stage::WaitServicesStart:
            return;
    }
}

bool BootService::ready_for_services() const {
    return _stage == Stage::WaitServicesStart || _stage == Stage::Complete;
}

bool BootService::waiting_for_wifi() const {
    return _stage == Stage::WaitWifi;
}

void BootService::mark_services_started() {
    if (_stage != Stage::WaitServicesStart || _runtime == nullptr || _ui_state == nullptr) {
        return;
    }

    _stage = Stage::Complete;
}

void BootService::_set_boot_state(
    state::BootPhase phase,
    const char* message,
    uint8_t progress_percent,
    uint32_t message_color) {
    if (_runtime == nullptr) {
        return;
    }

    _runtime->boot.phase = phase;
    state::publish_boot_state(
        _runtime->boot,
        phase,
        message,
        progress_percent,
        message_color,
        millis());
    if (_ui_state != nullptr) {
        _ui_state->dirty = true;
    }
}

void BootService::_advance(
    Stage next_stage,
    state::BootPhase phase,
    const char* message,
    uint8_t progress_percent,
    uint32_t message_color) {
    _stage = next_stage;
    _stage_started_ms = millis();
    _set_boot_state(phase, message, progress_percent, message_color);
}

void BootService::_advance_silent(Stage next_stage, state::BootPhase phase) {
    _stage = next_stage;
    _stage_started_ms = millis();
    if (_runtime != nullptr) {
        _runtime->boot.phase = phase;
    }
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
