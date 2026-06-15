#include "thread_entry.h"
#include "../app/application.h"
#include "utils/logger/logger.h"

// ============================================================================
//  Thread entry stubs (to be filled in during migration)
// ============================================================================

void stratum_thread_entry(void* args) {
    // StratumCtx* ctx = static_cast<StratumCtx*>(args);
    LOG_I("(stratum) placeholder — TODO");
    while (true) { delay(10000); }
}

void miner_tx_thread_entry(void* args) {
    // MinerCtx* ctx = static_cast<MinerCtx*>(args);
    LOG_I("(asic_tx) placeholder — TODO");
    while (true) { delay(10000); }
}

void miner_rx_thread_entry(void* args) {
    // MinerCtx* ctx = static_cast<MinerCtx*>(args);
    LOG_I("(asic_rx) placeholder — TODO");
    while (true) { delay(10000); }
}

void wifi_connect_thread_entry(void* args) {
    // WifiCtx* ctx = static_cast<WifiCtx*>(args);
    LOG_I("(wifi) placeholder — TODO");
    while (true) { delay(10000); }
}

void power_loop_thread_entry(void* args) {
    // PowerStatus* ctx = static_cast<PowerStatus*>(args);
    LOG_I("(power) placeholder — TODO");
    while (true) { delay(10000); }
}

void fan_thread_entry(void* args) {
    // FanCtx* ctx = static_cast<FanCtx*>(args);
    LOG_I("(fan) placeholder — TODO");
    while (true) { delay(10000); }
}

void led_thread_entry(void* args) {
    // UIPrefs* ctx = static_cast<UIPrefs*>(args);
    LOG_I("(led) placeholder — TODO");
    while (true) { delay(10000); }
}

void webserver_thread_entry(void* args) {
    LOG_I("(web) placeholder — TODO");
    while (true) { delay(10000); }
}

void scan_thread_entry(void* args) {
    // ScanCtx* ctx = static_cast<ScanCtx*>(args);
    LOG_I("(scan) placeholder — TODO");
    while (true) { delay(10000); }
}

void swarm_thread_entry(void* args) {
    // SwarmCtx* ctx = static_cast<SwarmCtx*>(args);
    LOG_I("(swarm) placeholder — TODO");
    while (true) { delay(10000); }
}

void market_thread_entry(void* args) {
    // MarketCtx* ctx = static_cast<MarketCtx*>(args);
    LOG_I("(market) placeholder — TODO");
    while (true) { delay(10000); }
}

void lvgl_thread_entry(void* args) {
    LOG_I("(lvgl) placeholder — TODO");
    while (true) { delay(5); }
}

void ui_thread_entry(void* args) {
    LOG_I("(ui) placeholder — TODO");
    while (true) { delay(100); }
}

void benchmark_thread_entry(void* args) {
    LOG_I("(benchmark) placeholder — TODO");
    while (true) { delay(10000); }
}
