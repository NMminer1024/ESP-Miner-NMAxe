#pragma once
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include "fan.h"

// Forward declarations keep this header light.
struct BoardSpecConfig;
struct TempState;

// ============================================================================
//  FanCtx — launch context for the fan thread
//
//  Replaces board_sal_t*. The fan thread:
//    - probes polarity into spec->fans[].polarity (needs a mutable spec)
//    - drives spec->fans[] PWM, tracks runtime status in *status_list
//    - reads temps from *temp for closed-loop PID control
// ============================================================================
struct FanCtx {
    BoardSpecConfig*           spec        = nullptr;  // mutable: polarity written during detect
    EventGroupHandle_t         init_evt    = nullptr;  // INIT_EVENT_* milestones
    EventGroupHandle_t         sys_evt     = nullptr;  // SYS_EVENT_* temp-update triggers
    std::vector<fan_status_t>* status_list = nullptr;  // runtime per-fan status (app-owned)
    const TempState*           temp        = nullptr;  // latest temperature samples
};
