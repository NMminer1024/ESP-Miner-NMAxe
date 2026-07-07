// What: Reusable GPIO-backed button driver with click/hold event synthesis.
// Why: Button semantics belong below the service layer, but the new framework
// still needs normalized input events without depending on OneButton directly.
// Role: Debounces one GPIO and emits click, double-click, and long-press events.
// Benefit: BSPs can expose real buttons now while keeping button policy reusable
// and independent from the UI implementation.
#pragma once

#include <array>

#include "drivers/button/button.h"

namespace nm::drivers {

class GpioButton final : public Button {
public:
    GpioButton(
        const char* button_name,
        int8_t pin,
        bool active_low = true,
        uint32_t debounce_ms = 30,
        uint32_t click_window_ms = 250,
        uint32_t long_press_ms = 800,
        uint32_t long_repeat_ms = 1000)
        : _name(button_name),
          _pin(pin),
          _active_low(active_low),
          _debounce_ms(debounce_ms),
          _click_window_ms(click_window_ms),
          _long_press_ms(long_press_ms),
          _long_repeat_ms(long_repeat_ms) {}

    bool init() override;
    const char* name() const override { return _name; }
    void poll() override;
    bool is_pressed() const override { return _stable_pressed; }
    bool read_event(ButtonEvent& event) override;

private:
    void _push_event(ButtonEventType type, uint32_t timestamp_ms);

    static constexpr size_t kQueueSize = 8;

    const char* _name = "button";
    int8_t _pin = -1;
    bool _active_low = true;
    uint32_t _debounce_ms = 30;
    uint32_t _click_window_ms = 250;
    uint32_t _long_press_ms = 800;
    uint32_t _long_repeat_ms = 1000;

    bool _initialized = false;
    bool _stable_pressed = false;
    bool _last_raw_pressed = false;
    bool _pending_click = false;
    bool _long_press_active = false;
    uint32_t _raw_change_ms = 0;
    uint32_t _stable_press_ms = 0;
    uint32_t _last_release_ms = 0;
    uint32_t _last_repeat_ms = 0;

    std::array<ButtonEvent, kQueueSize> _queue{};
    size_t _queue_head = 0;
    size_t _queue_tail = 0;
    size_t _queue_count = 0;
};

}  // namespace nm::drivers
