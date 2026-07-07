// What: Concrete polling monitor for the current minimal service stack.
// Why: Boot alone is not enough; the framework needs one service that keeps the
// runtime snapshot fresh using only abstract driver calls.
// Role: Updates telemetry state and maps button events onto generic UI/system events.
// Benefit: Establishes the service boundary that later mining/network code will
// plug into instead of reading hardware ad hoc.
#include "services/monitor_service.h"

#include <Arduino.h>

namespace nm::services {

void MonitorService::start(
    const bsp::Board& board,
    const config::AppConfig& config,
    state::RuntimeState& runtime,
    system::EventFlags& events) {
    _board = &board;
    _config = &config;
    _runtime = &runtime;
    _events = &events;
    _last_button_poll_ms = 0;
    _last_telemetry_poll_ms = 0;
}

void MonitorService::poll() {
    if (_board == nullptr || _runtime == nullptr || _events == nullptr) {
        return;
    }

    const uint32_t now_ms = millis();
    _poll_buttons(now_ms);
    _poll_telemetry(now_ms);
}

void MonitorService::_poll_buttons(uint32_t now_ms) {
    if (now_ms - _last_button_poll_ms < 20) {
        return;
    }
    _last_button_poll_ms = now_ms;

    for (size_t i = 0; i < _board->drivers().buttons.size() && i < state::kMaxButtons; ++i) {
        auto* button = _board->drivers().buttons[i];
        if (button == nullptr) {
            continue;
        }

        button->poll();
        _runtime->buttons[i].pressed = button->is_pressed();

        drivers::ButtonEvent event;
        while (button->read_event(event)) {
            _events->set(system::Event::UiWakeRequested);
            _runtime->last_sample_ms = now_ms;

            switch (event.type) {
                case drivers::ButtonEventType::Clicked:
                    _runtime->buttons[i].click_count++;
                    if (i == 0) {
                        _events->set(system::Event::UiNextPageRequested);
                    }
                    break;
                case drivers::ButtonEventType::DoubleClicked:
                    _runtime->buttons[i].double_click_count++;
                    if (i == 0) {
                        _events->set(system::Event::UiPrevPageRequested);
                    }
                    break;
                case drivers::ButtonEventType::LongPressStart:
                    _runtime->buttons[i].long_press_count++;
                    if (i == 0) {
                        _events->set(system::Event::SetupModeRequested);
                    } else {
                        _events->set(system::Event::FactoryResetRequested);
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

void MonitorService::_poll_telemetry(uint32_t now_ms) {
    if (now_ms - _last_telemetry_poll_ms < 500) {
        return;
    }
    _last_telemetry_poll_ms = now_ms;

    if (_board->drivers().power != nullptr) {
        auto& power = _runtime->power;
        power.adc_ready = _board->drivers().power->adc_ready();
        power.dc_plugged = _board->drivers().power->is_dc_plugged();
        power.vcore_ready = _board->drivers().power->is_vcore_ready();
        power.vbus_mv = _board->drivers().power->read_vbus_mv();
        power.ibus_ma = _board->drivers().power->read_ibus_ma();
        power.vcore_mv = _board->drivers().power->read_vcore_mv();
        power.power_mw = (power.vbus_mv * power.ibus_ma) / 1000u;
    }

    if (_board->drivers().temp != nullptr) {
        auto& thermal = _runtime->thermal;
        thermal.vcore_c = _board->drivers().temp->read_vcore_c();
        thermal.asic_c = _board->drivers().temp->read_asic_c();
        thermal.ready = true;
    }

    for (size_t i = 0; i < _board->drivers().fans.size() && i < state::kMaxFans; ++i) {
        auto* fan = _board->drivers().fans[i];
        if (fan == nullptr) {
            continue;
        }
        _runtime->fans[i].present = true;
        _runtime->fans[i].speed_percent = fan->speed_percent();
        _runtime->fans[i].rpm = fan->read_rpm();
    }

    _runtime->last_sample_ms = now_ms;
    _events->set(system::Event::TelemetryUpdated);
}

}  // namespace nm::services
