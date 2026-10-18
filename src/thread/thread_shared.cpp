// Definitions verbatim from thread_entry.cpp (only `static` dropped).
#include "thread_shared.h"
#include "../app/system_events.h"

namespace thread_shared {
volatile uint32_t g_hcn_poll_sent_total = 0;
ar_request_t g_ar_request;
hcn_hashrate_cache_t g_hcn_cache = {0};
bool hcn_cache_ensure_inited() {
    if (g_hcn_cache.inited) return true;
    g_hcn_cache.mutex = xSemaphoreCreateMutex();
    g_hcn_cache.inited = (g_hcn_cache.mutex != nullptr);
    if (!g_hcn_cache.inited) {
        LOG_E("HCN cache mutex create failed, HCN hashrate disabled.");
    }
    return g_hcn_cache.inited;
}
void hcn_cache_reset(const char* reason) {
    if (!hcn_cache_ensure_inited()) return;
    if (xSemaphoreTake(g_hcn_cache.mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;

    memset(g_hcn_cache.prev_cnt, 0, sizeof(g_hcn_cache.prev_cnt));
    memset(g_hcn_cache.prev_ms, 0, sizeof(g_hcn_cache.prev_ms));
    memset(g_hcn_cache.seen, 0, sizeof(g_hcn_cache.seen));
    memset(g_hcn_cache.gh_s, 0, sizeof(g_hcn_cache.gh_s));
    memset(g_hcn_cache.gh_ms, 0, sizeof(g_hcn_cache.gh_ms));
    memset(g_hcn_cache.sample_count, 0, sizeof(g_hcn_cache.sample_count));
    memset(g_hcn_cache.stall_cnt, 0, sizeof(g_hcn_cache.stall_cnt));
    g_hcn_cache.last_log_ms = 0;
    g_hcn_cache.reject_streak = 0;
    g_hcn_cache.last_progress_ms = 0;

    xSemaphoreGive(g_hcn_cache.mutex);
    LOG_W("HCN cache reset: %s", reason ? reason : "unknown");
}
void wait_for_wifi_sta_connected(EventGroupHandle_t init_evt, const char* thread_name) {
    if (init_evt == nullptr) return;
    LOG_I("%s waiting for WiFi STA connected gate...", thread_name);
    xEventGroupWaitBits(init_evt, INIT_EVENT_WIFI_STA_CONNECTED, pdFALSE, pdTRUE, portMAX_DELAY);
    LOG_I("%s WiFi STA connected gate opened.", thread_name);
}
} // namespace thread_shared
