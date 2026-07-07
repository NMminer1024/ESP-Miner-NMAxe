// What: Board power-control abstraction exported by the BSP.
// Why: Voltage rails, enable pins, and power sequencing differ by board, but
// higher layers need one consistent entry point into that subsystem.
// Role: Defines the common power driver contract for initialization ownership.
// Benefit: Keeps board-specific power implementation details below the BSP
// boundary and leaves room for policy-driven power logic later.
#pragma once

namespace nm::drivers {

class Power {
public:
    virtual ~Power() = default;
    virtual bool init() = 0;
    virtual const char* name() const = 0;
};

// Temporary skeleton-only fallback.
// TODO(agent): remove this class after each active BSP is wired to a real power driver.
class NullPower final : public Power {
public:
    explicit NullPower(const char* power_name) : _name(power_name) {}

    bool init() override { return true; }
    const char* name() const override { return _name; }

private:
    const char* _name = "null-power";
};

}  // namespace nm::drivers
