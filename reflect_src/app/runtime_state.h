#pragma once
#include <Arduino.h>
#include "../board/board.h"   // screen_preference_info_t, led_preference_info_t

// ============================================================================
//  PreferenceState — live user preferences (replaces board.status.preference)
//
//  Loaded from NVS at boot (defaulting to the board spec), then mutated at
//  runtime by the web/UI. Read by led/display/screensaver logic.
// ============================================================================
struct PreferenceState {
    screen_preference_info_t screen{};
    led_preference_info_t    led{};
};

// ============================================================================
//  OtaState — firmware update progress (replaces board.status.ota)
// ============================================================================
struct OtaState {
    String        filename;
    volatile bool running = false;
    volatile int  progress = 0;
    uint32_t      last_progress_ms = 0;
    String        error;
};

// ============================================================================
//  TimeState — time/date display format prefs (replaces board.status.time.format)
// ============================================================================
struct TimeState {
    struct {
        uint16_t time = 0;   // 0=24h, 1=12h (matches legacy time.format.time)
        String   date;       // date format string, e.g. "YYYY/MM/DD"
    } format;
};

// ============================================================================
//  BenchmarkState — live benchmark progress (replaces board.status.bm)
//  Single writer (benchmark thread), single reader (UI overlay); no mutex.
// ============================================================================
struct BenchmarkState {
    bool     active = false;       // overlay visible
    bool     in_stab = false;      // true=stabilization phase, false=sampling
    uint16_t cur_freq = 0;         // MHz
    uint16_t cur_vcore = 0;        // mV
    uint16_t freq_min = 0;
    uint16_t freq_max = 0;
    uint16_t freq_step = 0;
    uint16_t vcore_min = 0;
    uint16_t vcore_max = 0;
    uint16_t vcore_step = 0;
    uint32_t phase_elapsed = 0;    // seconds elapsed in current phase
    uint32_t phase_total = 0;      // total seconds for current phase
    float    avg_hr_ghs = 0.0f;    // running average hashrate (GH/s)
    float    exp_hr_ghs = 0.0f;    // expected hashrate (GH/s) from ASIC spec
    float    asic_temp = 0.0f;     // last ASIC temp sample
    float    vcore_temp = 0.0f;    // last VRM temp sample
    uint16_t stab_total = 0;       // total stabilization seconds
    uint16_t bm_total = 0;         // total sampling seconds
    uint32_t eta_sec = 0;          // estimated seconds remaining
};
