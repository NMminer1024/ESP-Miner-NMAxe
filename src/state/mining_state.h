// What: Shared runtime mining-status model for the new service stack.
// Why: The framework needs one hardware-agnostic place to express ASIC bring-up
// and future mining execution state without letting UI or web query drivers.
// Role: Captures the current mining phase, target settings, and ASIC-side
// readiness snapshot published by the mining service.
// Benefit: Keeps the main flow state-driven and gives later stratum/web layers
// one stable model to observe or control.
#pragma once

#include <stdint.h>

namespace nm::state {

enum class MiningPhase : uint8_t {
    Disabled = 0,
    WaitPower = 1,
    Probe = 2,
    WaitVbus = 3,
    WaitVcore = 4,
    Bringup = 5,
    Standby = 6,
    Running = 7,
    Fault = 8,
};

struct MiningState {
    MiningPhase phase = MiningPhase::Disabled;
    const char* message = "disabled";
    bool transport_ready = false;
    bool bringup_complete = false;
    uint8_t expected_asic_count = 0;
    uint8_t detected_asic_count = 0;
    uint16_t target_freq_mhz = 0;
    uint16_t applied_freq_mhz = 0;
    uint32_t last_transition_ms = 0;
};

}  // namespace nm::state
