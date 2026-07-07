// What: Lightweight event-flag hub shared by the new services.
// Why: Even before FreeRTOS task migration, services need a neutral way to
// signal UI actions and boot milestones without tight coupling.
// Role: Provides a small bitflag API for setting, testing, and consuming events.
// Benefit: Gives the architecture a stable event seam now and can later be
// swapped for RTOS-backed primitives with minimal service churn.
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
};

class EventFlags {
public:
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
