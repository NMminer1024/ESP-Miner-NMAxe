// What: Generic display abstraction used by the UI layer.
// Why: Each BSP may drive a different panel controller or bus implementation,
// but LVGL integration should only depend on basic display operations.
// Role: Defines display geometry types and the rectangle-write contract.
// Benefit: Allows board-private panel drivers to stay fully encapsulated while
// the UI stack talks to one clean rendering interface.
#pragma once

#include <stdint.h>

namespace nm::drivers {

struct DisplaySize {
    uint16_t width = 0;
    uint16_t height = 0;

    DisplaySize() = default;
    DisplaySize(uint16_t width_value, uint16_t height_value)
        : width(width_value), height(height_value) {}
};

struct DisplayRect {
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t width = 0;
    uint16_t height = 0;

    DisplayRect() = default;
    DisplayRect(uint16_t x_value, uint16_t y_value, uint16_t width_value, uint16_t height_value)
        : x(x_value), y(y_value), width(width_value), height(height_value) {}
};

class Display {
public:
    virtual ~Display() = default;
    virtual bool init() = 0;
    virtual const char* name() const = 0;
    virtual DisplaySize size() const = 0;
    virtual bool write_rect(const DisplayRect& rect, const uint16_t* pixels) = 0;
    virtual bool set_flip(bool flip) = 0;
    virtual bool flip() const = 0;
    virtual bool set_brightness_percent(uint8_t percent) = 0;
    virtual uint8_t brightness_percent() const = 0;
};

}  // namespace nm::drivers
