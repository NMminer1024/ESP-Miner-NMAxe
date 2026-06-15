#pragma once

// ============================================================================
//  FreeRTOS task priority assignments (low → high)
// ============================================================================
enum {
    TASK_PRIORITY_APHORISM   = 1,
    TASK_PRIORITY_FAN        = 2,
    TASK_PRIORITY_CONFIG     = 3,
    TASK_PRIORITY_APP_TICK   = 4,
    TASK_PRIORITY_DISPLAY    = 5,
    TASK_PRIORITY_SCAN       = 6,
    TASK_PRIORITY_SWARM      = 7,
    TASK_PRIORITY_MARKET     = 8,
    TASK_PRIORITY_WS         = 9,
    TASK_PRIORITY_ASIC_CNT   = 10,
    TASK_PRIORITY_ASIC_INIT  = 11,
    TASK_PRIORITY_DAEMON     = 12,
    TASK_PRIORITY_PWR        = 13,
    TASK_PRIORITY_BTN        = 14,
    TASK_PRIORITY_TOUCH      = 15,
    TASK_PRIORITY_MONITOR    = 16,
    TASK_PRIORITY_UI         = 17,
    TASK_PRIORITY_LVGL_DRV   = 18,
    TASK_PRIORITY_LED        = 19,
    TASK_PRIORITY_WIFI       = 20,
    TASK_PRIORITY_STRATUM    = 21,
    TASK_PRIORITY_MINER_TX   = 22,
    TASK_PRIORITY_MINER_RX   = 23,
};
