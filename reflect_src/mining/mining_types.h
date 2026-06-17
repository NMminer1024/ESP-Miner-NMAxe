#pragma once
#include <Arduino.h>
#include <map>
#include <deque>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>

#include "mining/miner.h"             // hashrate_t, history_node_t, proximity_node_t, AsicMinerClass, PsramAllocator
#include "stratum/stratum.h"          // StratumClass, stratum_info_t, pool_info_t (via pool.h)
#include "board/board.h"              // BoardSpecConfig
#include "drivers/power/power_hal.h"  // AxePowerHal

typedef uint16_t asic_id_t;

// ============================================================================
//  Miner runtime control state (mirrors legacy miner_runtime_state_t)
// ============================================================================
enum MinerRuntimeState {
    MINER_RUNTIME_RUNNING = 0,
    MINER_RUNTIME_PAUSING,
    MINER_RUNTIME_PAUSED,
    MINER_RUNTIME_RESUMING,
    MINER_RUNTIME_ERROR,
};

// ============================================================================
//  Difficulty snapshot (mirrors legacy diff_info_t)
// ============================================================================
struct DiffInfo {
    double best_session = 0.0;
    double best_ever    = 0.0;
    double pool         = 0.0;
    double last         = 0.0;
    double network      = 0.0;
};

// ============================================================================
//  MinerStatus — shared mining runtime state + statistics
//
//  Replaces the legacy board.status.miner god-struct. Writers/readers are
//  documented per field; the mining threads (stratum/tx/rx) and monitor are
//  the principal participants, with display/web as read-only consumers.
// ============================================================================
struct MinerStatus {
    DiffInfo   diff;
    hashrate_t hashrate{0.0, 0.0, 0.0};
    float      efficiency = 0.0f;       // J/TH
    uint16_t   hits = 0;                // block-proximity hit counter
    uint32_t   share_accepted = 0;
    uint32_t   share_rejected = 0;
    uint64_t   uptime_ever = 0;
    uint64_t   uptime_session = 0;
    uint32_t   latency = 0;             // ms, pool round-trip
    uint32_t   asic_update = 0;         // ms, last ASIC response timestamp
    uint32_t   stratum_update = 0;      // ms, last stratum data timestamp

    // --- runtime control (owned by miner tx; read by power/fan/rx/web) ---
    volatile MinerRuntimeState runtime_state = MINER_RUNTIME_RUNNING;
    volatile bool     user_paused = false;
    volatile uint32_t pause_started_ms = 0;
    uint32_t          resume_grace_until_ms = 0;
    SemaphoreHandle_t control_xsem = nullptr;   // pause/resume control signal
    SemaphoreHandle_t update_xsem  = nullptr;   // status-changed signal

    // --- per-asic response counters ---
    std::map<asic_id_t, uint64_t> asic_rsp_counter;

    // --- sample histories (PSRAM) ---
    struct {
        std::deque<history_node_t, PsramAllocator<history_node_t>> deque;
        SemaphoreHandle_t mutex = nullptr;
    } status_history;
    struct {
        std::deque<proximity_node_t, PsramAllocator<proximity_node_t>> deque;
        SemaphoreHandle_t mutex = nullptr;
    } proximity_history;

    // Controlled-idle: miner is intentionally not hashing (paused/resuming/error).
    // Power/fan loops use this to suppress vcore regulation and activity checks.
    bool is_controlled_idle() const {
        return user_paused ||
               runtime_state == MINER_RUNTIME_PAUSING ||
               runtime_state == MINER_RUNTIME_PAUSED  ||
               runtime_state == MINER_RUNTIME_RESUMING ||
               runtime_state == MINER_RUNTIME_ERROR;
    }
    bool in_resume_grace(uint32_t now_ms) const {
        if (resume_grace_until_ms == 0) return false;
        return (int32_t)(resume_grace_until_ms - now_ms) > 0;
    }
    bool suppress_activity_checks(uint32_t now_ms) const {
        return is_controlled_idle() || in_resume_grace(now_ms);
    }

    void init() {
        control_xsem            = xSemaphoreCreateCounting(1, 0);
        update_xsem             = xSemaphoreCreateCounting(1, 0);
        status_history.mutex    = xSemaphoreCreateMutex();
        proximity_history.mutex = xSemaphoreCreateMutex();
        runtime_state           = MINER_RUNTIME_RUNNING;
        user_paused             = false;
        pause_started_ms        = 0;
        resume_grace_until_ms   = 0;
    }
};

// ============================================================================
//  ConnInfo — pool & stratum connection set (mirrors board.info.connection)
// ============================================================================
struct ConnInfo {
    struct { pool_info_t    use, primary, fallback; } pool;
    struct { stratum_info_t use, primary, fallback; } stratum;
};

// ============================================================================
//  MinerCtx — launch context shared by stratum / miner-tx / miner-rx / monitor
//
//  Replaces board_sal_t*. Every cross-domain dependency is injected explicitly.
// ============================================================================
struct MinerCtx {
    AsicMinerClass*    miner   = nullptr;
    StratumClass*      stratum = nullptr;
    AxePowerHal*       power   = nullptr;
    BoardSpecConfig*   spec    = nullptr;
    MinerStatus*       status  = nullptr;
    ConnInfo*          conn    = nullptr;
    EventGroupHandle_t init_evt = nullptr;
    EventGroupHandle_t sys_evt  = nullptr;
    SemaphoreHandle_t  nvs_save_xsem = nullptr;
    volatile bool*     ota_running   = nullptr;   // nullptr -> treated as false
    volatile uint64_t* utc           = nullptr;   // shared UTC seconds (time domain)
    String             fw_version;                // for summary logs
};
