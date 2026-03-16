#include "app/application.h"
#include "esp_freertos_hooks.h"
#include "utils/logger/logger.h"

void setup() {
    auto& app = MinerApp::instance();
    app.init();
    app.begin();
}

void loop() {
#if 1
  // Testing only: simulate a screen blink
  static uint32_t cnt = 1;
  if ((cnt++ % 20) == 0) {
    // xEventGroupSetBits(g_board.status.sys_evt, SYS_EVENT_MINER_BLOCK_HIT);
    // LOG_W("Simulated SYS_EVENT_MINER_BLOCK_HIT triggered!");

    xEventGroupSetBits(g_board.status.sys_evt, SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED);
    LOG_W("Simulated SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED triggered!");
  }
#endif

#if 0
  // Dual-core average CPU usage monitor (FreeRTOS idle-hook based).
  // Principle: idle hooks increment counters; fewer increments = higher CPU load.
  //            A one-time calibration window (CPU_CALIB_MS) at startup captures
  //            the 100%-idle baseline rate. Every CPU_STAT_INTERVAL_MS the current
  //            idle rate is compared against that baseline to compute usage %.
  //
  // To disable: change `#if 1` to `#if 0`.
  #define CPU_CALIB_MS         1000UL   // baseline sampling duration (ms)
  #define CPU_STAT_INTERVAL_MS 5000UL   // reporting interval (ms)

  // Idle counters as static locals; non-capturing lambdas implicitly convert to
  // C function pointers, so no file-scope helpers are needed.
  static volatile uint32_t s_idle_cnt0 = 0;
  static volatile uint32_t s_idle_cnt1 = 0;
  static bool (* const s_hook0)() = []() -> bool { s_idle_cnt0++; return false; };
  static bool (* const s_hook1)() = []() -> bool { s_idle_cnt1++; return false; };

  static bool     s_cpu_init  = false;
  static uint32_t s_base0     = 1, s_base1     = 1; // 100%-idle hooks/s per core
  static uint32_t s_last_cnt0 = 0, s_last_cnt1 = 0;
  static uint32_t s_last_ms   = 0;

  // One-time init: register hooks, wait for calibration window, record baseline
  if (!s_cpu_init) {
    esp_register_freertos_idle_hook_for_cpu(s_hook0, 0);
    esp_register_freertos_idle_hook_for_cpu(s_hook1, 1);
    s_idle_cnt0 = 0;
    s_idle_cnt1 = 0;
    vTaskDelay(pdMS_TO_TICKS(CPU_CALIB_MS));  // sample pure-idle period
    s_base0     = s_idle_cnt0 ? s_idle_cnt0 : 1;
    s_base1     = s_idle_cnt1 ? s_idle_cnt1 : 1;
    s_idle_cnt0 = 0;
    s_idle_cnt1 = 0;
    LOG_W("CPU calib done: core0=%lu idle/s, core1=%lu idle/s (100%% idle baseline)",
          (unsigned long)s_base0, (unsigned long)s_base1);
    s_last_ms   = millis();
    s_cpu_init  = true;
  }

  // Periodic reporting
  uint32_t now_ms = millis();
  if (now_ms - s_last_ms >= CPU_STAT_INTERVAL_MS) {
    uint32_t elapsed_ms = now_ms - s_last_ms;

    // Snapshot volatile counters once to avoid inconsistency
    uint32_t cur0 = s_idle_cnt0;
    uint32_t cur1 = s_idle_cnt1;

    // Convert delta to idle hooks/s so the result is window-length independent
    uint32_t idle_ps0 = (uint32_t)((uint64_t)(cur0 - s_last_cnt0) * 1000 / elapsed_ms);
    uint32_t idle_ps1 = (uint32_t)((uint64_t)(cur1 - s_last_cnt1) * 1000 / elapsed_ms);

    // usage% = (1 - current_idle_rate / baseline_idle_rate) * 100, clamped to [0, 100]
    int usage0 = 100 - (int)((uint64_t)idle_ps0 * 100 / s_base0);
    int usage1 = 100 - (int)((uint64_t)idle_ps1 * 100 / s_base1);
    if (usage0 < 0) usage0 = 0;  if (usage0 > 100) usage0 = 100;
    if (usage1 < 0) usage1 = 0;  if (usage1 > 100) usage1 = 100;

    LOG_W("Avg CPU usage (%lus window): Core0=%d%%  Core1=%d%%",
          (unsigned long)(elapsed_ms / 1000), usage0, usage1);

    s_last_cnt0 = cur0;
    s_last_cnt1 = cur1;
    s_last_ms   = now_ms;
  }
#endif

    delay(1000);
}
