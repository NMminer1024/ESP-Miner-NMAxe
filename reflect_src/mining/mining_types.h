#pragma once
#include <cstdint>
#include <cstring>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

// ============================================================================
//  Stratum → Miner : Job snapshot
// ============================================================================
struct MiningJob {
    uint8_t     block_header[80];
    uint8_t     net_diff_target[32];
    uint32_t    pool_diff_mask;
    double      pool_difficulty;
    bool        clean_job;

    uint32_t    sequence;           // monotonic, 0 = no job yet

    char        job_id[68];
    char        extranonce2[20];
    char        ntime[12];
};

// ============================================================================
//  Miner → Stratum : Submit request
// ============================================================================
struct SubmitRequest {
    uint32_t    nonce;
    double      diff;
    char        job_id[68];
    char        extranonce2[20];
    char        ntime[12];
};

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
//  Shared context between stratum thread and miner threads
// ============================================================================
struct MiningSharedCtx {
    // --- job channel (Stratum → Miner) ---
    MiningJob           latest_job;
    SemaphoreHandle_t   job_mutex;
    SemaphoreHandle_t   job_event;          // binary: new job available

    // --- submit channel (Miner → Stratum) ---
    QueueHandle_t       submit_queue;       // carries SubmitRequest

    // --- stop flags ---
    volatile bool       stop_mining;
    volatile bool       pause_requested;

    // --- statistics (single-writer per field) ---
    volatile uint64_t   hash_count;
    volatile double     last_diff;
    volatile double     best_session_diff;
    volatile double     best_ever_diff;
    volatile double     network_diff;
    volatile double     pool_diff;
    volatile uint32_t   shares_accepted;
    volatile uint32_t   shares_rejected;
    volatile uint16_t   block_hits;
    volatile uint32_t   last_nonce;
    volatile uint32_t   job_received;

    // --- hashrate (3m average, updated periodically) ---
    volatile double     hashrate_3m;

    // --- pool info (stratum writes, webserver reads) ---
    char                active_pool_host[64];
    char                active_pool_user[128];
    volatile int        active_pool_port;

    // --- uptime base: loaded once from NVS at boot ---
    volatile uint32_t   uptime_ever_base;

    // --- runtime control state (owned by miner thread; read by power/fan/web) ---
    volatile MinerRuntimeState runtime_state;
    volatile bool       user_paused;
    volatile uint32_t   pause_started_ms;
    volatile uint32_t   resume_grace_until_ms;

    // --- asic respond counter map (per-asic-id → counter) ---
    // Stored separately; indexed by asic_id.

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

    void pause_mining() {
        pause_requested = true;
        stop_mining     = true;
        if (job_event) xSemaphoreGive(job_event);
    }

    void resume_mining() {
        pause_requested = false;
        if (job_event) xSemaphoreGive(job_event);
    }

    void init() {
        memset(&latest_job, 0, sizeof(latest_job));
        job_mutex       = xSemaphoreCreateMutex();
        job_event       = xSemaphoreCreateBinary();
        submit_queue    = xQueueCreate(8, sizeof(SubmitRequest));
        stop_mining       = false;
        pause_requested   = false;
        hash_count        = 0;
        last_diff = best_session_diff = best_ever_diff = network_diff = pool_diff = 0.0;
        shares_accepted = shares_rejected = block_hits = 0;
        last_nonce = job_received = 0;
        hashrate_3m = 0.0;
        memset(active_pool_host, 0, sizeof(active_pool_host));
        memset(active_pool_user, 0, sizeof(active_pool_user));
        active_pool_port = 0;
        uptime_ever_base = 0;
        runtime_state         = MINER_RUNTIME_RUNNING;
        user_paused           = false;
        pause_started_ms      = 0;
        resume_grace_until_ms = 0;
    }
};

// ============================================================================
//  Stratum thread launch context
// ============================================================================
struct StratumCtx {
    MiningSharedCtx*    shared;
    // Pool connection info will be loaded from NVS at thread start
};

// ============================================================================
//  Miner thread launch context
// ============================================================================
struct MinerCtx {
    MiningSharedCtx*    shared;
    const char*         name;
    // ASIC driver reference will be set by application before thread start
};
