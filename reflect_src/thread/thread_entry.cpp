#include "thread_entry.h"
#include "../app/application.h"
#include "../utils/logger/logger.h"

void stratum_thread_entry(void* args) {
    (void)args;
    LOG_D("(stratum) placeholder");
    while (true) { delay(10000); }
}

void miner_tx_thread_entry(void* args) {
    (void)args;
    LOG_D("(asic_tx) placeholder");
    while (true) { delay(10000); }
}

void miner_rx_thread_entry(void* args) {
    (void)args;
    LOG_D("(asic_rx) placeholder");
    while (true) { delay(10000); }
}

void wifi_connect_thread_entry(void* args) {
    (void)args;
    LOG_D("(wifi) placeholder");
    while (true) { delay(10000); }
}

void power_loop_thread_entry(void* args) {
    (void)args;
    LOG_D("(power) placeholder");
    while (true) { delay(10000); }
}

void fan_thread_entry(void* args) {
    (void)args;
    LOG_D("(fan) placeholder");
    while (true) { delay(10000); }
}

void led_thread_entry(void* args) {
    (void)args;
    LOG_D("(led) placeholder");
    while (true) { delay(10000); }
}

void webserver_thread_entry(void* args) {
    (void)args;
    LOG_D("(web) placeholder");
    while (true) { delay(10000); }
}

void scan_thread_entry(void* args) {
    (void)args;
    LOG_D("(scan) placeholder");
    while (true) { delay(10000); }
}

void swarm_thread_entry(void* args) {
    (void)args;
    LOG_D("(swarm) placeholder");
    while (true) { delay(10000); }
}

void market_thread_entry(void* args) {
    (void)args;
    LOG_D("(market) placeholder");
    while (true) { delay(10000); }
}

void lvgl_thread_entry(void* args) {
    (void)args;
    LOG_D("(lvgl) placeholder");
    while (true) { delay(5); }
}

void ui_thread_entry(void* args) {
    (void)args;
    LOG_D("(ui) placeholder");
    while (true) { delay(100); }
}

void benchmark_thread_entry(void* args) {
    (void)args;
    LOG_D("(benchmark) placeholder");
    while (true) { delay(10000); }
}
