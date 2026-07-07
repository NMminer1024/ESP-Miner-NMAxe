// What: Minimal Stratum client service for boot readiness gating.
// Why: The loading page must advance from pool connect/auth/job based on real
// pool protocol events, not synthetic mining-service placeholders.
// Role: Owns pool socket I/O on a dedicated task and exposes a small telemetry
// snapshot that Application can publish into RuntimeState.
#pragma once

#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "bsp/board.h"
#include "config/app_config.h"
#include "state/runtime_state.h"

namespace nm::services {

class StratumService {
public:
    void start(const bsp::Board& board, const config::AppConfig& config);
    void stop();
    bool poll(state::RuntimeState& runtime);
    bool started() const { return _started; }

private:
    struct Endpoint {
        String host;
        uint16_t port = 0;
        bool ssl = false;
        String user;
        String password;
    };

    bool _parse_endpoint(const config::StratumEndpointConfig& config, Endpoint& endpoint);
    void _run();
    void _connect_and_listen();
    bool _connect_pool(const Endpoint& endpoint);
    bool _send_line(const String& line);
    String _read_line(uint32_t timeout_ms);
    bool _subscribe();
    bool _authorize();
    bool _configure_version_rolling();
    bool _suggest_difficulty();
    void _handle_line(const String& line);
    void _set_error(const char* message);
    void _reset_session_status();
    uint32_t _next_id();
    static void _task_entry(void* args);

    mutable SemaphoreHandle_t _mutex = nullptr;
    TaskHandle_t _task = nullptr;
    bool _started = false;
    volatile bool _stop_requested = false;

    Endpoint _endpoint{};
    state::StratumTelemetry _telemetry{};
    WiFiClient _tcp_client{};
    WiFiClientSecure _ssl_client{};
    Client* _client = nullptr;
    uint32_t _request_id = 1;
    uint32_t _authorize_id = 0;
    String _client_id{};
};

}  // namespace nm::services
