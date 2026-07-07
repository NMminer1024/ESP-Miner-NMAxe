// What: Minimal ASIC control abstraction for mining-capable BSPs.
// Why: The framework needs a typed hook for the active ASIC path even before the
// real mining implementation is fully wired.
// Role: Defines the board-exported interface for ASIC subsystem ownership.
// Benefit: Lets the BSP reserve a clean seam for mining logic while keeping
// application and UI layers decoupled from ASIC-specific details.
#pragma once

namespace nm::drivers {

class Asic {
public:
    virtual ~Asic() = default;
    virtual bool init() = 0;
    virtual const char* name() const = 0;
};

// Temporary skeleton-only fallback.
// TODO(agent): remove this class after each active BSP is wired to a real ASIC driver.
class NullAsic final : public Asic {
public:
    explicit NullAsic(const char* asic_name) : _name(asic_name) {}

    bool init() override { return true; }
    const char* name() const override { return _name; }

private:
    const char* _name = "null-asic";
};

}  // namespace nm::drivers
