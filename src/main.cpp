#include "app/application.h"
#include "app/system_events.h"
#include "utils/reboot_log/reboot_log.h"

void setup() {
    // Persist the previous boot's reboot record before other tasks can touch reboot intent state.
    reboot_log_init();

    auto& app = MinerApp::instance();
    app.init();
    app.begin();
}

void loop() {
    static uint32_t s_loop_count = 0;
    s_loop_count++;

    EventGroupHandle_t evt = MinerApp::instance().sys_evt();

    // ── Test 1: Block Hit celebration (image + backlight flash) ──
    // Trigger after 40 loops (~40 s). Set #if 1 to enable.
#if 0
    if (evt && s_loop_count == 40) {
        xEventGroupSetBits(evt, SYS_EVENT_MINER_BLOCK_HIT);
    }
#endif

    // ── Test 2: High Difficulty celebration (image + backlight pulse) ──
    // Trigger after 40 loops (~40 s). Set #if 1 to enable.
#if 1
    if (evt && s_loop_count == 40) {
        xEventGroupSetBits(evt, SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED);
    }
#endif

    delay(1000);
}
