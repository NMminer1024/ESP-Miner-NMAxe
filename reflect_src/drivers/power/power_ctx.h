#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

// Forward declarations keep this context header free of heavy includes;
// the power threads (thread_entry.cpp) pull in the full definitions.
class  AxePowerHal;
struct BoardSpecConfig;
struct MiningSharedCtx;

// ============================================================================
//  PowerCtx — launch context for power_init / power_loop threads
//
//  Replaces the legacy board_sal_t* arg. Each field is an explicit dependency
//  injected by the application, so the power domain never reaches into a global
//  board god-object.
// ============================================================================
struct PowerCtx {
    AxePowerHal*           power       = nullptr;  // owned by application, lives for program lifetime
    const BoardSpecConfig* spec        = nullptr;  // vcore range / req, vbus_min_required
    EventGroupHandle_t     init_evt    = nullptr;  // INIT_EVENT_* milestones
    EventGroupHandle_t     sys_evt     = nullptr;  // SYS_EVENT_* runtime notifications
    volatile bool*         ota_running = nullptr;  // skip fault handling during OTA (nullptr -> false)
    MiningSharedCtx*       mining      = nullptr;  // controlled-idle check (nullptr -> not idle)
};
