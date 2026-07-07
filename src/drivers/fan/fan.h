// What: Cooling fan abstraction exported from the BSP layer.
// Why: Different boards may have different fan counts or control hardware, but
// higher layers should not know those implementation details.
// Role: Defines the common identity and initialization surface for fan drivers.
// Benefit: Fan management can evolve per board without changing the framework
// contract that application, policy, or UI code depends on.
#pragma once

namespace nm::drivers {

class Fan {
public:
    virtual ~Fan() = default;
    virtual bool init() = 0;
    virtual const char* name() const = 0;
};

// Temporary skeleton-only fallback.
// TODO(agent): remove this class after each active BSP is wired to real fan control.
class NullFan final : public Fan {
public:
    explicit NullFan(const char* fan_name) : _name(fan_name) {}

    bool init() override { return true; }
    const char* name() const override { return _name; }

private:
    const char* _name = "null-fan";
};

}  // namespace nm::drivers
