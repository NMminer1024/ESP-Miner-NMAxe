// What: Minimal Stratum client service for boot readiness gating.
// Why: The loading page must advance from pool connect/auth/job based on real
// pool protocol events, not synthetic mining-service placeholders.
// Role: Owns pool socket I/O on a dedicated task and exposes a small telemetry
// snapshot that Application can publish into RuntimeState.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <deque>
#include <map>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "bsp/board.h"
#include "config/app_config.h"
#include "state/runtime_state.h"
#include "utils/helper.h"

namespace nm::services {

struct PoolJobData {
    String id;
    String prevhash;
    String coinb1;
    String coinb2;
    String nbits;
    BasicJsonDocument<PsramJsonAllocator> merkle_branch;
    String version;
    String ntime;
    bool clean_jobs = false;

    PoolJobData() : merkle_branch(2048) {}
    PoolJobData(const PoolJobData& other) : merkle_branch(2048) {
        id = other.id;
        prevhash = other.prevhash;
        coinb1 = other.coinb1;
        coinb2 = other.coinb2;
        nbits = other.nbits;
        merkle_branch.set(other.merkle_branch.as<JsonVariantConst>());
        version = other.version;
        ntime = other.ntime;
        clean_jobs = other.clean_jobs;
    }
    PoolJobData& operator=(const PoolJobData& other) {
        if (this == &other) {
            return *this;
        }
        id = other.id;
        prevhash = other.prevhash;
        coinb1 = other.coinb1;
        coinb2 = other.coinb2;
        nbits = other.nbits;
        merkle_branch.clear();
        merkle_branch.set(other.merkle_branch.as<JsonVariantConst>());
        version = other.version;
        ntime = other.ntime;
        clean_jobs = other.clean_jobs;
        return *this;
    }
};

class StratumService {
public:
    void start(const bsp::Board& board, const config::AppConfig& config);
    void stop();
    bool poll(state::RuntimeState& runtime);
    bool started() const { return _started; }
    bool pop_job(PoolJobData& job);
    size_t job_cache_size() const;
    bool submit(const String& pool_job_id, const String& extranonce2, uint32_t ntime, uint32_t nonce, uint32_t version);
    String next_extranonce2();
    bool clear_extranonce2();
    SemaphoreHandle_t new_job_signal() const { return _new_job_sem; }
    SemaphoreHandle_t clear_job_signal() const { return _clear_job_sem; }

private:
    struct Endpoint {
        String host;
        uint16_t port = 0;
        bool ssl = false;
        String user;
        String password;
        bool valid = false;
    };

    struct PendingResponse {
        String method;
        bool status = false;
        uint32_t stamp_ms = 0;
    };

    bool _parse_endpoint(const config::StratumEndpointConfig& config, Endpoint& endpoint);
    void _select_endpoint(bool fallback);
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
    void _handle_notify(DynamicJsonDocument& doc);
    void _handle_method(const char* method, DynamicJsonDocument& doc);
    void _handle_response(DynamicJsonDocument& doc, const String& raw);
    void _record_pending(uint32_t id, const char* method);
    void _clear_old_pending();
    void _push_job(const PoolJobData& job);
    void _clear_job_cache_locked();
    void _set_pool_difficulty(double difficulty);
    void _set_version_mask(uint32_t mask, bool version_rolling);
    void _set_error(const char* message);
    void _reset_session_status();
    void _reset_protocol_state();
    bool _hello_pool(uint32_t now_ms);
    bool _primary_available();
    String _parse_stratum_error(const String& raw);
    uint32_t _next_id();
    static void _task_entry(void* args);

    mutable SemaphoreHandle_t _mutex = nullptr;
    SemaphoreHandle_t _write_mutex = nullptr;
    SemaphoreHandle_t _new_job_sem = nullptr;
    SemaphoreHandle_t _clear_job_sem = nullptr;
    TaskHandle_t _task = nullptr;
    bool _started = false;
    volatile bool _stop_requested = false;

    Endpoint _primary_endpoint{};
    Endpoint _fallback_endpoint{};
    Endpoint _endpoint{};
    bool _using_fallback = false;
    state::StratumTelemetry _telemetry{};
    WiFiClient _tcp_client{};
    WiFiClientSecure _ssl_client{};
    Client* _client = nullptr;
    uint32_t _request_id = 1;
    uint32_t _authorize_id = 0;
    uint32_t _last_write_ms = 0;
    uint32_t _last_read_ms = 0;
    uint32_t _last_primary_probe_ms = 0;
    uint16_t _pool_retry = 0;
    String _client_id{};
    String _extranonce2 = "0";
    uint8_t _job_cache_max = 4;
    double _pool_difficulty = 1024.0;
    std::deque<PoolJobData, PsramAllocator<PoolJobData>> _job_cache{};
    std::map<uint32_t, PendingResponse, std::less<uint32_t>, PsramAllocator<std::pair<const uint32_t, PendingResponse>>> _pending{};
};

}  // namespace nm::services
