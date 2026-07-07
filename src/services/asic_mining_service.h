// What: ASIC TX/RX mining service for the BSP-first framework.
// Why: Stratum jobs must be converted into chip work and ASIC nonces must be
// verified/submitted without putting mining work in UI/app polling.
// Role: Owns dedicated miner TX/RX FreeRTOS tasks and small job-context cache.
#pragma once

#include <Arduino.h>
#include <deque>
#include <map>
#include <unordered_set>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "bsp/board.h"
#include "config/app_config.h"
#include "drivers/asic/asic.h"
#include "services/stratum_service.h"
#include "state/runtime_state.h"
#include "system/events.h"
#include "utils/helper.h"

namespace nm::services {

class AsicMiningService {
public:
    void start(
        const bsp::Board& board,
        const config::AppConfig& config,
        state::RuntimeState& runtime,
        system::EventFlags& events,
        StratumService& stratum);
    void stop();
    bool started() const { return _started; }

private:
    struct JobContext {
        drivers::AsicJob job{};
        String pool_job_id{};
        String extranonce2{};
        double network_diff = 0.0;
    };

    bool _build_and_send_job(const PoolJobData& pool_job);
    bool _build_asic_job(const PoolJobData& pool_job, const String& extranonce2, drivers::AsicJob& job);
    bool _find_job(uint8_t asic_job_id, JobContext& context);
    void _clear_job_cache();
    void _record_nonce(double diff);
    void _publish_diff(double last, double pool, double network);
    void _log_share_table(double last, double pool, double network, uint8_t asic_id);
    uint8_t _job_id_step() const;
    double _network_diff(const String& nbits) const;
    double _share_diff(
        uint32_t version,
        uint8_t* prev_block_hash,
        uint8_t* merkle_root,
        uint32_t ntime,
        uint32_t nbits,
        uint32_t nonce) const;
    state::StratumTelemetry _stratum_snapshot() const;
    static void _tx_task_entry(void* args);
    static void _rx_task_entry(void* args);
    void _tx_loop();
    void _rx_loop();

    const bsp::Board* _board = nullptr;
    const config::AppConfig* _config = nullptr;
    state::RuntimeState* _runtime = nullptr;
    system::EventFlags* _events = nullptr;
    StratumService* _stratum = nullptr;
    SemaphoreHandle_t _job_mutex = nullptr;
    TaskHandle_t _tx_task = nullptr;
    TaskHandle_t _rx_task = nullptr;
    volatile bool _stop_requested = false;
    bool _started = false;
    uint8_t _next_job_id = 0;
    double _last_target_diff = 0.0;
    double _hashrate_s3m = 0.0;
    double _hashrate_s30m = 0.0;
    double _hashrate_s60m = 0.0;
    size_t _off_3m = 0;
    size_t _off_30m = 0;
    String _dedup_job_key{};
    std::map<uint8_t, JobContext, std::less<uint8_t>, PsramAllocator<std::pair<const uint8_t, JobContext>>> _job_cache{};
    std::deque<std::pair<uint32_t, double>, PsramAllocator<std::pair<uint32_t, double>>> _hashrate_samples{};
    std::unordered_set<uint32_t, std::hash<uint32_t>, std::equal_to<uint32_t>, PsramAllocator<uint32_t>> _submitted_nonces{};
};

}  // namespace nm::services
