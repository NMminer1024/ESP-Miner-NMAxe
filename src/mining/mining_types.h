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
#include "drivers/temp/temp_ctx.h"    // TempState

typedef uint16_t asic_id_t;

// ── Mining timing constants (mirror legacy global.h) ────────────────────────
#define MINER_ASIC_ALIVE_TIMEOUT        (1000*60*3)  // ms, ASIC frozen watchdog
#define MINER_STRATUM_ALIVE_TIMEOUT     (1000*60*5)  // ms, pool silence watchdog
// UI/LVGL stall detection is log-only (no reboot action): the threshold is set
// far above any legitimate render pause so it never fires in normal operation.
#define MINER_UI_ALIVE_TIMEOUT          (1000*60*2)  // ms, UI render stall (log-only)
#define MINER_RESUME_GRACE_MS           (1000*60*3)  // ms, post-resume grace window
#define MINER_LOG_SUMMARY_INTERVAL      (1000*60*1)  // ms, summary log cadence

// ── History / persistence cadence (mirror legacy global.h) ──────────────────
#define BOARD_NVS_SAVE_INTERVAL         (60*60)         // seconds between NVS persists
#define MINER_HISTORY_SAMPLE_DEEPTH     (1000*3600*24)  // history retention window (ms)
#define MINER_HISTORY_SAMPLE_INTERVAL   (5)             // history sample interval (s)
#define MINER_HISTORY_MAX_SIZE          (20000)         // hard cap on deque element count

// ============================================================================
//  Miner runtime control state (mirrors legacy miner_runtime_state_t)
// ============================================================================
enum MinerRuntimeState {
    MINER_RUNTIME_RUNNING = 0,
    MINER_RUNTIME_PAUSING,
    MINER_RUNTIME_PAUSED,
    MINER_RUNTIME_RESUMING,
    MINER_RUNTIME_ERROR,
    // Firmware-initiated fast Vcore power-cycle + ASIC reinit (HCN imbalance
    // recovery). Distinct from the user pause/resume quartet so user_paused is
    // never set and the mining-pause overlay stays hidden (silent recovery).
    MINER_RUNTIME_AUTO_REINITING,
};

static inline const char* miner_runtime_state_to_string(MinerRuntimeState state) {
    switch (state) {
        case MINER_RUNTIME_RUNNING:  return "running";
        case MINER_RUNTIME_PAUSING:  return "pausing";
        case MINER_RUNTIME_PAUSED:   return "paused";
        case MINER_RUNTIME_RESUMING: return "resuming";
        case MINER_RUNTIME_ERROR:    return "error";
        case MINER_RUNTIME_AUTO_REINITING: return "auto_reiniting";
        default:                     return "unknown";
    }
}

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
// Lifetime Top-10 best share difficulties, kept sorted descending (index 0 =
// best). Zero entries mean "not filled yet". Written by the ASIC rx thread on
// every accepted share that cracks into the list; read by web/UI and persisted
// to NVS (NVS_CONFIG_TOP_DIFFS) on the nvs_save_xsem signal.
#define MINER_TOP_DIFF_COUNT 10
// Snapshot of the connection in use when a Top-10 entry was achieved:
// pool = "host:port" of the stratum connection, user = stratum user
// (wallet[.worker]). Empty string = unknown (pre-snapshot firmware upgrade).
// Sizing budget: pool covers any real stratum FQDN + port (IP form max 21);
// user covers the longest valid BTC address (bech32m "bc1p..." = 62 chars)
// plus a generous worker suffix. Both truncate safely via snprintf if a user
// configures something longer — the NVS copy of stratumuser stays authoritative.
// Combined NVS payload worst case: COUNT * (POOL + USER + 2 separators)
// ~= 1.5 KB, well under the 4000-byte per-key nvs_set_str limit.
#define MINER_TOP_SNAP_POOL_LEN 48
#define MINER_TOP_SNAP_USER_LEN 96

struct MinerStatus {
    DiffInfo   diff;
    hashrate_t hashrate{0.0, 0.0, 0.0};
    float      share_rate = 0.0f;       // ASIC nonce find rate (nonces/s)
    float      efficiency = 0.0f;       // J/TH
    uint16_t   hits = 0;                // block-proximity hit counter
    double     best_top_diffs[MINER_TOP_DIFF_COUNT] = {0};
    // UTC epoch (seconds) when each entry was achieved; 0 = unknown (pre-
    // timestamp firmware upgrade, or share found before the first NTP sync).
    // Shifted in lockstep with best_top_diffs. Persisted alongside as a second
    // comma-separated NVS string (NVS_CONFIG_TOP_DIFF_TS).
    uint32_t   best_top_diff_ts[MINER_TOP_DIFF_COUNT] = {0};
    // Pool / wallet snapshot per entry, shifted in lockstep too. Persisted as
    // a third NVS string (NVS_CONFIG_TOP_DIFF_SNAP): entries separated by ';',
    // fields by '|' ("pool1|user1;pool2|user2;...").
    char       best_top_diff_pool[MINER_TOP_DIFF_COUNT][MINER_TOP_SNAP_POOL_LEN] = {{0}};
    char       best_top_diff_user[MINER_TOP_DIFF_COUNT][MINER_TOP_SNAP_USER_LEN] = {{0}};
    // Network difficulty the pool reported at the moment the entry was
    // achieved — lets the UI show whether the share was block-proximity
    // (share diff >= network diff means a real block solve). 0 = unknown
    // (pre-support upgrade). Persisted as a fourth comma-separated NVS string
    // (NVS_CONFIG_TOP_DIFF_NET).
    double     best_top_diff_net[MINER_TOP_DIFF_COUNT] = {0};

    // Insert an accepted share difficulty into the lifetime Top-10 list.
    // Returns true when the share cracked into the list (caller should then
    // trigger an NVS save so the ranking survives reboots).
    bool record_top_diff(double d, uint32_t epoch, const char* pool, const char* user, double net) {
        if (d <= 0.0 || d <= best_top_diffs[MINER_TOP_DIFF_COUNT - 1]) {
            return false;
        }
        int pos = MINER_TOP_DIFF_COUNT - 1;
        while (pos > 0 && best_top_diffs[pos - 1] < d) {
            best_top_diffs[pos] = best_top_diffs[pos - 1];
            best_top_diff_ts[pos] = best_top_diff_ts[pos - 1];
            best_top_diff_net[pos] = best_top_diff_net[pos - 1];
            memcpy(best_top_diff_pool[pos], best_top_diff_pool[pos - 1], MINER_TOP_SNAP_POOL_LEN);
            memcpy(best_top_diff_user[pos], best_top_diff_user[pos - 1], MINER_TOP_SNAP_USER_LEN);
            pos--;
        }
        best_top_diffs[pos] = d;
        best_top_diff_ts[pos] = epoch;
        best_top_diff_net[pos] = net;
        snprintf(best_top_diff_pool[pos], MINER_TOP_SNAP_POOL_LEN, "%s", pool ? pool : "");
        snprintf(best_top_diff_user[pos], MINER_TOP_SNAP_USER_LEN, "%s", user ? user : "");
        return true;
    }
    uint32_t   share_accepted = 0;
    uint32_t   share_rejected = 0;
    uint64_t   uptime_ever = 0;
    uint64_t   uptime_session = 0;
    time_t     boot_epoch = 0;          // UTC epoch at boot (set after first NTP sync)
    uint32_t   latency = 0;             // ms, pool round-trip
    uint32_t   asic_update = 0;         // ms, last ASIC response timestamp
    uint32_t   stratum_update = 0;      // ms, last stratum data timestamp
    volatile uint32_t ui_update = 0;    // ms, last UI render heartbeat (log-only stall check)

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
    // AUTO_REINITING included so the fast recovery path is shielded from the
    // daemon/power-loop checks for its ~2s duration without touching them.
    bool is_controlled_idle() const {
        return user_paused ||
               runtime_state == MINER_RUNTIME_PAUSING ||
               runtime_state == MINER_RUNTIME_PAUSED  ||
               runtime_state == MINER_RUNTIME_RESUMING ||
               runtime_state == MINER_RUNTIME_ERROR   ||
               runtime_state == MINER_RUNTIME_AUTO_REINITING;
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
    const TempState*   temp    = nullptr;
    MinerStatus*       status  = nullptr;
    ConnInfo*          conn    = nullptr;
    EventGroupHandle_t init_evt = nullptr;
    EventGroupHandle_t sys_evt  = nullptr;
    SemaphoreHandle_t  nvs_save_xsem = nullptr;
    volatile bool*     ota_running   = nullptr;   // nullptr -> treated as false
    volatile uint64_t* utc           = nullptr;   // shared UTC seconds (time domain)
    volatile int*      wifi_status   = nullptr;   // wl_status_t snapshot (WL_CONNECTED check)
    SemaphoreHandle_t  wifi_reconnect_xsem = nullptr; // signal wifi thread to reconnect
    String             fw_version;                // for summary logs
};
