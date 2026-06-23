#include "app/application.h"
#include "app/system_events.h"
#include "utils/logger/logger.h"

void setup() {
    auto& app = MinerApp::instance();
    app.init();
    app.begin();
}

void loop() {
    static uint32_t s_loop_count = 0;
    s_loop_count++;

    EventGroupHandle_t evt = MinerApp::instance().sys_evt();

    // ── Test 1: Block Hit celebration (image + backlight flash) ──
    // Trigger after 56 loops (~56 s). Set #if 1 to enable.
#if 0
    if (evt && (s_loop_count % 56 == 0)) {
        xEventGroupSetBits(evt, SYS_EVENT_MINER_BLOCK_HIT);
        LOG_W("Test: Block Hit celebration triggered");
    }
#endif

    // ── Test 2: High Difficulty celebration (image + backlight pulse) ──
    // Trigger after 40 loops (~40 s). Set #if 1 to enable.
#if 0
    if (evt && (s_loop_count % 40 == 0)) {
        xEventGroupSetBits(evt, SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED);
        LOG_W("Test: High Difficulty celebration triggered");
    }
#endif

    delay(1000);
}
