// What: High-level mining bring-up service for the new BSP-first framework.
// Why: The framework now has boot, monitor, and UI layers, so the core ASIC
// business path needs its own service boundary instead of living in board code.
// Role: Waits for power readiness, applies the target ASIC profile, and
// publishes mining lifecycle state through the shared runtime model.
// Benefit: Lets future stratum/web layers command mining through one service
// without reaching into BSPs or chip drivers.
#pragma once

#include <Arduino.h>

#include "bsp/board.h"
#include "config/app_config.h"
#include "state/runtime_state.h"
#include "system/events.h"

namespace nm::services {

class MiningService {
public:
    void start(
        const bsp::Board& board,
        const config::AppConfig& config,
        state::RuntimeState& runtime,
        system::EventFlags& events);

    void poll();

private:
    void _set_phase(state::MiningPhase phase, const char* message);
    void _set_boot_loading(const char* message, uint8_t progress_percent, uint32_t message_color = 0xFFFFFF);
    void _publish_asic_status();
    uint32_t _read_vbus_mv();
    bool _power_ready() const;
    void _fail(const char* message);
    uint16_t _fan_self_test_threshold_rpm() const;
    void _start_fan_polarity_task();
    void _start_fan_self_test_task();
    static void _fan_polarity_task_entry(void* args);
    static void _fan_self_test_task_entry(void* args);
    static void _fan_self_test_progress(uint16_t rpm, void* ctx);

    const bsp::Board* _board = nullptr;
    const config::AppConfig* _config = nullptr;
    state::RuntimeState* _runtime = nullptr;
    system::EventFlags* _events = nullptr;
    uint32_t _last_probe_attempt_ms = 0;
    bool _fan_polarity_ran = false;
    TaskHandle_t _fan_polarity_task = nullptr;
    volatile bool _fan_polarity_running = false;
    volatile bool _fan_polarity_complete = false;
    volatile bool _fan_polarity_inverted = false;
    volatile uint16_t _fan_polarity_rpm_50 = 0;
    volatile uint16_t _fan_polarity_rpm_100 = 0;
    bool _fan_self_test_ran = false;
    TaskHandle_t _fan_self_test_task = nullptr;
    volatile bool _fan_self_test_running = false;
    volatile bool _fan_self_test_complete = false;
    volatile bool _fan_self_test_passed = false;
    volatile uint16_t _fan_self_test_rpm = 0;
    uint16_t _fan_self_test_threshold = 0;
    char _boot_message[64] = {};
    char _asic_result_message[64] = {};
};

}  // namespace nm::services
