#include "app/application.h"
#include "app/system_events.h"
#include "utils/logger/logger.h"

// Set to 1 only for bench testing (block-hit / high-diff celebration triggers).
// When 0, the Arduino loopTask deletes itself on first entry and its ~8 KB
// internal-RAM stack is returned to the heap. All real work runs in the
// MinerApp worker threads; nothing else depends on loop() being called.
#define LOOP_TEST_HOOKS 0

void setup() {
    auto& app = MinerApp::instance();
    app.init();
    app.begin();
}

void loop() {
#if LOOP_TEST_HOOKS
    static uint32_t s_loop_count = 0;
    s_loop_count++;

    EventGroupHandle_t evt = MinerApp::instance().sys_evt();

    // ── Test 1: Block Hit celebration (image + backlight flash) ──
    // Trigger every 56 loops (~56 s).
    if (evt && (s_loop_count % 56 == 0)) {
        xEventGroupSetBits(evt, SYS_EVENT_MINER_BLOCK_HIT);
        LOG_W("Test: Block Hit celebration triggered");
    }

    // ── Test 2: High Difficulty celebration (image + backlight pulse) ──
    // Trigger every 40 loops (~40 s).
    if (evt && (s_loop_count % 40 == 0)) {
        xEventGroupSetBits(evt, SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED);
        LOG_W("Test: High Difficulty celebration triggered");
    }

    delay(1000);
#else
    // Production path: nothing to do in loop(). Self-delete the Arduino
    // loopTask so its stack (CONFIG_ARDUINO_LOOP_STACK_SIZE, 8192 B on
    // PlatformIO's arduino-espressif32) is freed back to the internal heap.
    // This runs once, right after setup() returns; the task never wakes again.
    vTaskDelete(NULL);
#endif
}
