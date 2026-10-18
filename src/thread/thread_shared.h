// Shared state for the split thread-entry translation units.
// Constants/structs verbatim from thread_entry.cpp; globals and helpers
// keep single-instance linkage (extern decl here, def in thread_shared.cpp).
#pragma once

#include <Arduino.h>
#include "../utils/logger/logger.h"

namespace thread_shared {
constexpr uint8_t  HCN_MAX_ASIC_CHANNELS = 16;
constexpr uint32_t HCN_LOG_INTERVAL_MS   = 5000u;
constexpr uint32_t HCN_STALE_MS          = 30000u;
constexpr uint32_t HCN_MIN_DELTA_MS      = 1000u;
constexpr float    HCN_EMA_ALPHA         = 0.45f;
constexpr float    HCN_EMA_FASTSTART     = 0.85f;
constexpr uint8_t  HCN_FASTSTART_SAMPLES = 12;
// A 0x90 counter that does not increment across this many consecutive polls is
// treated as dead: the channel is dropped from the active set so the hashrate
// source can fall back to nonces. Escalation to a full reinit is handled by the
// monitor's no-progress watchdog (trigger 3).
constexpr uint8_t  HCN_STALL_DROP        = 3;

// Log-only diagnostics for tracking down transient hashrate collapses (no recovery action).
constexpr uint32_t HR_DIAG_HCN_DEAD_SEC  = 10;    // consecutive seconds with 0 active HCN channels before logging
constexpr float    HR_DIAG_DROP_RATIO    = 0.60f; // log once when hashrate falls below this fraction of recent peak
constexpr float    HR_DIAG_PEAK_DECAY    = 0.999f;// per-second decay so the peak re-baselines after ~10min on intentional changes

// Cross-thread counter: incremented by the tx loop each time a 0x90 poll command is sent,
// read by the rx loop's periodic HCN log line to tell "never sent" apart from "sent but no
// reply decodes". Diagnostic only, a stale read by a few counts is harmless.
extern volatile uint32_t g_hcn_poll_sent_total;

// ── Auto-reinit on HCN per-channel imbalance (silent Vcore power-cycle) ──────
// Motivation: on Vcore-series-stacked boards (e.g. NMQAxe++Nexus, 2xBM1373) the
// midpoint voltage is unregulated and can drift as the dies warm unevenly; the
// under-volted die then hashes far below its sibling while TOTAL rail power is
// unchanged. The only reliable firmware recovery is a Vcore off->on edge (the
// midpoint re-divides at the rising edge while both dies are cold) followed by
// a full ASIC reinit. Detection runs in the monitor thread against the HCN
// (0x90) per-channel EMA; execution is a dedicated branch in the miner-tx
// control lambda via MINER_RUNTIME_AUTO_REINITING (never sets user_paused, so
// the pause overlay stays hidden and the web pause/resume API is untouched).
constexpr float    AR_IMBALANCE_RATIO    = 0.60f;  // trigger when min < ratio * max
constexpr uint32_t AR_SUSTAIN_SEC        = 30;     // condition must hold this long (1s ticks)
constexpr float    AR_MIN_MAX_GHS        = 500.0f; // ignore when even the best channel is weak
constexpr uint32_t AR_COOLDOWN_MS        = 15 * 60 * 1000u;  // min gap between auto re-inits
constexpr uint8_t  AR_MAX_ATTEMPTS       = 5;      // give up for the session after this many
constexpr uint32_t AR_DISCHARGE_MS       = 800;    // Vcore off time before re-powering
constexpr uint32_t AR_READY_TIMEOUT_MS   = 3000;   // is_vcore_ready() poll ceiling
constexpr uint32_t AR_READY_POLL_MS      = 50;     // is_vcore_ready() poll step
constexpr uint32_t AR_POST_CHECK_MS      = 30 * 1000u;  // post-reinit recovery verdict delay
constexpr uint8_t  AR_REJECT_STREAK      = 3;      // consecutive rejected 0x90 samples that trigger a reinit
constexpr uint8_t  AR_HCN_DEAD_SEC       = 6;      // 1s ticks with polls flowing but no advancing 0x90 sample -> reinit
constexpr uint8_t  AR_PL_LOSS_TICKS      = 5;      // 1s ticks with fewer active HCN channels than chips -> reinit
constexpr uint32_t AR_PL_WARMUP_MS       = 15000;  // grace after polls (re)start before channel count is judged

// Cross-thread request: written once by the monitor-thread detector, consumed by
// the miner-tx control lambda. volatile fields suffice (single writer, single
// reader; a torn snapshot only costs one cycle of delay).
typedef struct {
    volatile bool     pending = false;
    volatile float    min_ghs = 0.0f;
    volatile float    max_ghs = 0.0f;
    volatile uint8_t  min_ch  = 0;
    volatile uint8_t  max_ch  = 0;
    volatile uint32_t sustain_sec = 0;
    volatile uint8_t  attempt = 0;
    volatile uint8_t  reason  = 0;    // 0 = channel imbalance, 1 = reject streak, 2 = no progress, 3 = channels missing
    volatile float    last_raw_ghs = 0.0f;  // reason 1: the rejected raw rate that broke the streak
} ar_request_t;
extern ar_request_t g_ar_request;;

typedef struct {
    SemaphoreHandle_t mutex;
    bool inited;

    uint32_t prev_cnt[HCN_MAX_ASIC_CHANNELS];
    uint32_t prev_ms[HCN_MAX_ASIC_CHANNELS];
    bool seen[HCN_MAX_ASIC_CHANNELS];

    float gh_s[HCN_MAX_ASIC_CHANNELS];
    uint32_t gh_ms[HCN_MAX_ASIC_CHANNELS];
    uint8_t sample_count[HCN_MAX_ASIC_CHANNELS];
    // Consecutive polls with d_cnt == 0 (counter not incrementing). Reset on any
    // advancing sample; at HCN_STALL_DROP the channel is dropped from the active set.
    uint8_t stall_cnt[HCN_MAX_ASIC_CHANNELS];
    uint32_t last_log_ms;

    // Diagnostic: why did the rate calc not produce a fresh sample this cycle?
    // Reset each time the periodic log prints, so counts reflect the last ~5s window.
    uint32_t diag_reject_cnt;      // raw_ghs failed the sanity check (implausible/reset)
    uint32_t diag_skip_cnt;        // d_ms < HCN_MIN_DELTA_MS, calc skipped entirely
    uint32_t diag_stall_cnt;       // d_cnt == 0, counter not incrementing
    uint32_t diag_ok_cnt;          // raw_ghs accepted, EMA updated
    float    diag_last_reject_raw_ghs;
    uint32_t diag_last_reject_d_cnt;
    uint32_t diag_last_reject_d_ms;

    // Consecutive-reject streak: incremented on every rejected raw_ghs sample, reset on
    // any accepted one. Used by the monitor-thread auto-reinit detector so it can react
    // in ~6s (3 polls) to a persistently-garbage 0x90 counter instead of waiting 30s
    // for the staleness filter to drop all channels first.
    uint8_t  reject_streak;
    float    last_reject_raw_ghs;
    uint8_t  last_reject_ch;

    // Last time any channel produced an accepted sample with d_cnt > 0 (real hashing
    // evidence from the 0x90 counter). Read by the monitor's no-progress watchdog
    // (trigger 3): polls flowing but this never advancing means the chain is dead.
    uint32_t last_progress_ms;
} hcn_hashrate_cache_t;

extern hcn_hashrate_cache_t g_hcn_cache;

bool hcn_cache_ensure_inited();

void hcn_cache_reset(const char* reason);


void wait_for_wifi_sta_connected(EventGroupHandle_t init_evt, const char* thread_name);

// ── Stratum: pool connect / subscribe / job notify / share results ──────────
} // namespace thread_shared
