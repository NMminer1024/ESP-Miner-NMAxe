// What: FreeRTOS task placement and priority plan for the new framework.
// Why: UI, mining, networking, and monitoring cannot keep sharing Arduino's
// low-priority loop task once the firmware grows back to the legacy feature set.
// Role: Defines stable task priorities, stacks, and core affinity for services.
// Benefit: Keeps scheduling policy explicit instead of scattering magic numbers
// across service startup code.
#pragma once

#include <stdint.h>

namespace nm::app {

// Keep the relative ordering close to the proven legacy firmware:
// LVGL must be above normal monitoring/service work, but below WiFi/Stratum and
// ASIC TX/RX once those latency-sensitive threads are migrated.
enum TaskPriority : uint8_t {
    kTaskPriorityBackground = 1,
    kTaskPriorityAppService = 10,
    kTaskPriorityPower = 13,
    kTaskPriorityButton = 14,
    kTaskPriorityMonitor = 16,
    kTaskPriorityUi = 17,
    kTaskPriorityLvgl = 18,
    kTaskPriorityWifi = 20,
    kTaskPriorityStratum = 21,
    kTaskPriorityMinerTx = 22,
    kTaskPriorityMinerRx = 23,
};

enum TaskCore : uint8_t {
    kTaskCoreNet = 0,
    kTaskCoreUi = 1,
};

constexpr uint32_t kAppServiceTaskStackBytes = 4096;
constexpr uint32_t kWifiTaskStackBytes = 6144;
constexpr uint32_t kStratumTaskStackBytes = 8192;
constexpr uint32_t kMinerTaskStackBytes = 8192;
constexpr uint32_t kUiTaskStackBytes = 6144;
constexpr uint32_t kUiTaskPeriodMs = 5;
constexpr uint32_t kAppServiceTaskPeriodMs = 5;
constexpr uint32_t kWifiTaskPeriodMs = 50;

}  // namespace nm::app
