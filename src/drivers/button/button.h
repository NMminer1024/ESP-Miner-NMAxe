// What: Button input abstraction exported by BSPs to the service layer.
// Why: Boards differ in button count, GPIO polarity, and click/hold semantics,
// but top-level logic should only consume normalized button events.
// Role: Defines button events plus the polling/queue contract used by services.
// Benefit: Main flow can react to user input without touching GPIOs, debounce
// policy, or board-specific input wiring.
#pragma once

#include <stdint.h>

namespace nm::drivers {

enum class ButtonEventType : uint8_t {
    None = 0,
    Pressed = 1,
    Released = 2,
    Clicked = 3,
    DoubleClicked = 4,
    LongPressStart = 5,
    LongPressRepeat = 6,
    LongPressStop = 7,
};

struct ButtonEvent {
    ButtonEventType type = ButtonEventType::None;
    uint32_t timestamp_ms = 0;

    ButtonEvent() = default;
    ButtonEvent(ButtonEventType type_value, uint32_t timestamp_ms_value)
        : type(type_value), timestamp_ms(timestamp_ms_value) {}
};

class Button {
public:
    virtual ~Button() = default;
    virtual bool init() = 0;
    virtual const char* name() const = 0;
    virtual void poll() = 0;
    virtual bool is_pressed() const = 0;
    virtual bool read_event(ButtonEvent& event) = 0;
};

}  // namespace nm::drivers
