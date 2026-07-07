// What: Lightweight event-flag hub shared by the new services.
// Why: Even before FreeRTOS task migration, services need a neutral way to
// signal UI actions and boot milestones without tight coupling.
// Role: Provides a small bitflag API for setting, testing, and consuming events.
// Benefit: Gives the architecture a stable event seam now and can later be
// swapped for RTOS-backed primitives with minimal service churn.
// Temporary note: this is intentionally only a phase-1 event mechanism. Bit
// flags coalesce repeated events and assume cheap single-loop consumers, so do
// not stretch this type into long-term market/stratum/web/mining traffic.
// TODO(agent): replace this with a queue/mailbox/RTOS-backed event transport
// before introducing asynchronous or high-rate service producers.
#pragma once

#include <stdint.h>

namespace nm::system {

enum class Event : uint32_t {
    None = 0,
    BoardReady = 1u << 0,
    UiReady = 1u << 1,
    TelemetryUpdated = 1u << 2,
    UiWakeRequested = 1u << 3,
    UiNextPageRequested = 1u << 4,
    UiPrevPageRequested = 1u << 5,
    FactoryResetRequested = 1u << 6,
    SetupModeRequested = 1u << 7,
    MiningStateChanged = 1u << 8,
    NetworkStateChanged = 1u << 9,
};

class EventFlags {
public:
    // Temporary phase-1 API:
    // Safe enough for the current single-loop service skeleton, but not a
    // substitute for queued events once multiple concurrent producers exist.
    void set(Event event) {
        _bits |= static_cast<uint32_t>(event);
    }

    void clear(Event event) {
        _bits &= ~static_cast<uint32_t>(event);
    }

    bool test(Event event) const {
        return (_bits & static_cast<uint32_t>(event)) != 0;
    }

    bool consume(Event event) {
        if (!test(event)) {
            return false;
        }
        clear(event);
        return true;
    }

    uint32_t bits() const {
        return _bits;
    }

private:
    uint32_t _bits = 0;
};

}  // namespace nm::system
