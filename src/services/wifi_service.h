// What: WiFi STA/AP startup service for the new boot pipeline.
// Why: Loading must gate ASIC bring-up on real network readiness just like the
// legacy firmware, without letting UI pages call Arduino WiFi APIs directly.
// Role: Owns WiFi initialization and publishes network telemetry into runtime.
#pragma once

#include "config/app_config.h"
#include "state/runtime_state.h"
#include "system/events.h"

namespace nm::services {

class WifiService {
public:
    void start(
        const config::AppConfig& config,
        state::RuntimeState& runtime,
        system::EventFlags& events);

    void poll();
    bool sta_connected() const;
    bool ap_ready() const;

private:
    enum class Stage : uint8_t {
        Idle = 0,
        StaDelay = 1,
        StaConnecting = 2,
        StaConnected = 3,
        ApStarting = 4,
        ApReady = 5,
        Fault = 6,
    };

    void _start_sta();
    void _start_ap();
    void _publish_sta_connected();
    void _publish_disconnected(uint8_t status);
    void _copy_network_identity();

    const config::AppConfig* _config = nullptr;
    state::RuntimeState* _runtime = nullptr;
    system::EventFlags* _events = nullptr;
    Stage _stage = Stage::Idle;
    uint32_t _stage_started_ms = 0;
    uint32_t _connect_after_ms = 0;
    uint32_t _ap_ready_after_ms = 0;
    uint32_t _last_retry_log_ms = 0;
};

}  // namespace nm::services
