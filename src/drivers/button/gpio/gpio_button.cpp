// What: GPIO-backed button implementation for the new input abstraction.
// Why: Services need normalized button events, not direct pin reads or an
// external helper library baked into the application layer.
// Role: Debounces the GPIO state and generates higher-level button events.
// Benefit: Input policy remains reusable and testable above the BSP layer.
#include "drivers/button/gpio/gpio_button.h"

#include <Arduino.h>

namespace nm::drivers {

bool GpioButton::init() {
    if (_initialized) {
        return true;
    }

    if (_pin < 0) {
        return false;
    }

    pinMode(_pin, INPUT_PULLUP);
    _last_raw_pressed = _active_low ? (digitalRead(_pin) == LOW) : (digitalRead(_pin) == HIGH);
    _stable_pressed = _last_raw_pressed;
    _raw_change_ms = millis();
    _initialized = true;
    return true;
}

void GpioButton::poll() {
    if (!_initialized) {
        return;
    }

    const uint32_t now = millis();
    const bool raw_pressed = _active_low ? (digitalRead(_pin) == LOW) : (digitalRead(_pin) == HIGH);

    if (raw_pressed != _last_raw_pressed) {
        _last_raw_pressed = raw_pressed;
        _raw_change_ms = now;
    }

    if (raw_pressed != _stable_pressed && (now - _raw_change_ms) >= _debounce_ms) {
        _stable_pressed = raw_pressed;
        if (_stable_pressed) {
            _stable_press_ms = now;
            _long_press_active = false;
            _last_repeat_ms = now;
            _push_event(ButtonEventType::Pressed, now);
        } else {
            _push_event(ButtonEventType::Released, now);
            if (_long_press_active) {
                _push_event(ButtonEventType::LongPressStop, now);
                _long_press_active = false;
                _pending_click = false;
            } else if (_pending_click && (now - _last_release_ms) <= _click_window_ms) {
                _push_event(ButtonEventType::DoubleClicked, now);
                _pending_click = false;
            } else {
                _pending_click = true;
                _last_release_ms = now;
            }
        }
    }

    if (_stable_pressed) {
        const uint32_t held_ms = now - _stable_press_ms;
        if (!_long_press_active && held_ms >= _long_press_ms) {
            _long_press_active = true;
            _pending_click = false;
            _last_repeat_ms = now;
            _push_event(ButtonEventType::LongPressStart, now);
        } else if (_long_press_active && (now - _last_repeat_ms) >= _long_repeat_ms) {
            _last_repeat_ms = now;
            _push_event(ButtonEventType::LongPressRepeat, now);
        }
    } else if (_pending_click && (now - _last_release_ms) > _click_window_ms) {
        _push_event(ButtonEventType::Clicked, _last_release_ms);
        _pending_click = false;
    }
}

bool GpioButton::read_event(ButtonEvent& event) {
    if (_queue_count == 0) {
        return false;
    }

    event = _queue[_queue_head];
    _queue_head = (_queue_head + 1) % kQueueSize;
    --_queue_count;
    return true;
}

void GpioButton::_push_event(ButtonEventType type, uint32_t timestamp_ms) {
    if (_queue_count >= kQueueSize) {
        return;
    }

    _queue[_queue_tail] = {type, timestamp_ms};
    _queue_tail = (_queue_tail + 1) % kQueueSize;
    ++_queue_count;
}

}  // namespace nm::drivers
