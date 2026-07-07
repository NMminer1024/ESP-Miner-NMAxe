// What: Board power-control abstraction exported by the BSP.
// Why: Voltage rails, enable pins, and power sequencing differ by board, but
// higher layers need one consistent entry point into that subsystem.
// Role: Defines the common power driver contract for initialization ownership.
// Benefit: Keeps board-specific power implementation details below the BSP
// boundary and leaves room for policy-driven power logic later.
#pragma once

#include <stdint.h>

namespace nm::drivers {

enum class PowerRail : uint8_t {
    Pll0v8 = 0,
    Vdd1v8 = 1,
    Vcore = 2,
};

class Power {
public:
    virtual ~Power() = default;
    virtual bool init() = 0;
    virtual const char* name() const = 0;
    virtual bool adc_ready() const = 0;
    virtual bool set_rail_enabled(PowerRail rail, bool enabled) = 0;
    virtual bool set_vcore_limits(uint16_t min_mv, uint16_t max_mv) = 0;
    virtual uint16_t min_vcore_mv() const = 0;
    virtual uint16_t max_vcore_mv() const = 0;
    virtual bool set_vcore_mv(uint16_t value_mv) = 0;
    virtual bool is_vcore_ready() const = 0;
    virtual bool is_dc_plugged() const = 0;
    virtual uint32_t read_vbus_mv() = 0;
    virtual uint32_t read_ibus_ma() = 0;
    virtual uint32_t read_vcore_mv() = 0;
};

// Temporary skeleton-only fallback.
// TODO(agent): remove this class after each active BSP is wired to a real power driver.
class NullPower final : public Power {
public:
    explicit NullPower(const char* power_name) : _name(power_name) {}

    bool init() override { return true; }
    const char* name() const override { return _name; }
    bool adc_ready() const override { return true; }
    bool set_rail_enabled(PowerRail, bool) override { return true; }
    bool set_vcore_limits(uint16_t min_mv, uint16_t max_mv) override {
        _min_vcore_mv = min_mv;
        _max_vcore_mv = max_mv;
        return true;
    }
    uint16_t min_vcore_mv() const override { return _min_vcore_mv; }
    uint16_t max_vcore_mv() const override { return _max_vcore_mv; }
    bool set_vcore_mv(uint16_t value_mv) override {
        _vcore_mv = value_mv;
        return true;
    }
    bool is_vcore_ready() const override { return true; }
    bool is_dc_plugged() const override { return true; }
    uint32_t read_vbus_mv() override { return 5000; }
    uint32_t read_ibus_ma() override { return 0; }
    uint32_t read_vcore_mv() override { return _vcore_mv; }

private:
    const char* _name = "null-power";
    uint16_t _min_vcore_mv = 0;
    uint16_t _max_vcore_mv = 0;
    uint16_t _vcore_mv = 0;
};

}  // namespace nm::drivers
