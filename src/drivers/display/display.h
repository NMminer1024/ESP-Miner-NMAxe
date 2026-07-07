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

class Display {
public:
    virtual ~Display() = default;
    virtual bool init() = 0;
    virtual const char* name() const = 0;
    virtual DisplaySize size() const = 0;
};

class NullDisplay final : public Display {
public:
    NullDisplay(const char* display_name, uint16_t width, uint16_t height)
        : _name(display_name), _size(width, height) {}

    bool init() override { return true; }
    const char* name() const override { return _name; }
    DisplaySize size() const override { return _size; }

private:
    const char* _name = "null-display";
    DisplaySize _size{};
};

}  // namespace nm::drivers
