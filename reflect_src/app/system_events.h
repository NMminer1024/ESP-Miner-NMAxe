#pragma once

// ============================================================================
//  System-wide FreeRTOS event-group bits
//
//  Two event groups exist per system:
//    - init_evt : one-shot "ready" milestones latched during boot/bring-up.
//    - sys_evt  : runtime notifications (block hit, faults, screensaver ...).
//
//  Values are kept identical to the legacy src/global.h scheme so behaviour
//  and any cross-thread handshake timing are byte-for-byte preserved.
// ============================================================================

// ── init_evt : boot/bring-up milestones ─────────────────────────────────────
enum {
    INIT_EVENT_FAN_POLARITY_DETECT   = (1 << 0),   // fan polarity detected, ready for fan thread to start
    INIT_EVENT_FAN_READY             = (1 << 1),   // fan initialized and self-test done
    INIT_EVENT_WIFI_STA_CONNECTED    = (1 << 2),   // wifi sta mode initialized and connected
    INIT_EVENT_WIFI_AP_READY         = (1 << 3),   // wifi ap mode initialized and ready for client connection
    INIT_EVENT_ASIC_COUNTED          = (1 << 4),   // asic counting done
    INIT_EVENT_VBUS_READY            = (1 << 5),   // vbus ready
    INIT_EVENT_VDD_VPLL_READY        = (1 << 6),   // vdd and pll ready
    INIT_EVENT_VCORE_READY           = (1 << 7),   // vcore ready
    INIT_EVENT_SCREEN_READY          = (1 << 8),   // screen initialized and ready
    INIT_EVENT_LVGL_READY            = (1 << 9),   // lvgl display driver ready
    INIT_EVENT_UI_READY              = (1 << 10),  // UI initialized and ready
    INIT_EVENT_MINER_READY           = (1 << 11),  // miner initialized and ready
    INIT_EVENT_TMP_READY             = (1 << 12),  // TMP102 self-test done
};

// ── sys_evt : runtime notifications ─────────────────────────────────────────
enum {
    SYS_EVENT_MINER_BLOCK_HIT            = (1 << 0),   // miner hit a block
    SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED   = (1 << 1),   // miner achieved a new high difficulty
    SYS_EVENT_MINER_VCORE_TEMP_UPDATE    = (1 << 2),   // vcore temperature update -> backlight effect
    SYS_EVENT_MINER_ASIC_TEMP_UPDATE     = (1 << 3),   // asic temperature update -> backlight effect
    SYS_EVENT_SCREEN_SAVER_TRIGGERED     = (1 << 4),   // screen saver triggered -> dim the screen
    SYS_EVENT_FIND_NEIGHBOR_TRIGGERED    = (1 << 5),   // blink screen to locate neighbor miner
    SYS_EVENT_POWER_OC_FAULT             = (1 << 6),   // power overcurrent fault -> prompt user
    SYS_EVENT_POWER_OT_FAULT             = (1 << 7),   // power over-temperature fault -> prompt user
};
