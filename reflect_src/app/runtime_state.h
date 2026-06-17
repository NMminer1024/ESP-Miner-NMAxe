#pragma once
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
