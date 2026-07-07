// What: Touch input abstraction exported by BSPs to upper layers.
// Why: Different boards may use different touch controllers, buses, or even no
// touch at all, but the UI layer needs one stable read contract.
// Role: Defines the generic touch point shape and the driver interface.
// Benefit: Touch-capable boards can vary freely underneath while UI code stays
// focused on pointer semantics rather than controller-specific transactions.
#pragma once

#include <stdint.h>

namespace nm::drivers {

struct TouchPoint {
    uint16_t x = 0;
    uint16_t y = 0;
    bool pressed = false;

    TouchPoint() = default;
    TouchPoint(uint16_t x_value, uint16_t y_value, bool pressed_value)
        : x(x_value), y(y_value), pressed(pressed_value) {}
};

class Touch {
public:
    virtual ~Touch() = default;
    virtual bool init() = 0;
    virtual const char* name() const = 0;
    virtual bool read(TouchPoint& point) = 0;
};

// Temporary skeleton-only fallback.
// TODO(agent): remove this class after each active BSP is wired to a real touch driver.
class NullTouch final : public Touch {
public:
    explicit NullTouch(const char* touch_name) : _name(touch_name) {}

    bool init() override { return true; }
    const char* name() const override { return _name; }
    bool read(TouchPoint& point) override {
        point = TouchPoint();
        return false;
    }

private:
    const char* _name = "null-touch";
};

}  // namespace nm::drivers
