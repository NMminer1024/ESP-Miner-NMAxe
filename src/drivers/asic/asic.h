// What: Minimal ASIC control abstraction for mining-capable BSPs.
// Why: The framework needs a typed hook for the active ASIC path even before the
// real mining implementation is fully wired.
// Role: Defines the board-exported interface for ASIC subsystem ownership.
// Benefit: Lets the BSP reserve a clean seam for mining logic while keeping
// application and UI layers decoupled from ASIC-specific details.
#pragma once

#include <stdint.h>

namespace nm::drivers {

struct AsicStatus {
    bool transport_ready = false;
    bool bringup_complete = false;
    uint8_t detected_asic_count = 0;
    uint16_t target_freq_mhz = 0;
};

class Asic {
public:
    virtual ~Asic() = default;
    virtual bool init() = 0;
    virtual uint8_t probe_count() = 0;
    virtual bool bringup(uint16_t target_freq_mhz, uint8_t expected_asic_count) = 0;
    virtual const char* name() const = 0;
    virtual AsicStatus status() const = 0;
};

// Temporary skeleton-only fallback.
// TODO(agent): remove this class after each active BSP is wired to a real ASIC driver.
class NullAsic final : public Asic {
public:
    explicit NullAsic(const char* asic_name) : _name(asic_name) {}

    bool init() override { return true; }
    uint8_t probe_count() override { return 0; }
    bool bringup(uint16_t, uint8_t) override { return false; }
    const char* name() const override { return _name; }
    AsicStatus status() const override { return _status; }

private:
    const char* _name = "null-asic";
    AsicStatus _status{};
};

}  // namespace nm::drivers
