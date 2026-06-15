#include "thread_entry.h"
#include "../app/application.h"

namespace {
void thread_log(const char* name) {
    Serial.print("[reflect-thread] ");
    Serial.println(name);
}
}

void stratum_thread_entry(void* args) {
    (void)args;
    thread_log("(stratum) placeholder");
    while (true) { delay(10000); }
}

void miner_tx_thread_entry(void* args) {
    (void)args;
    thread_log("(asic_tx) placeholder");
    while (true) { delay(10000); }
}

void miner_rx_thread_entry(void* args) {
    (void)args;
    thread_log("(asic_rx) placeholder");
    while (true) { delay(10000); }
}

void wifi_connect_thread_entry(void* args) {
    (void)args;
    thread_log("(wifi) placeholder");
    while (true) { delay(10000); }
}

void power_loop_thread_entry(void* args) {
    (void)args;
    thread_log("(power) placeholder");
    while (true) { delay(10000); }
}

void fan_thread_entry(void* args) {
    (void)args;
    thread_log("(fan) placeholder");
    while (true) { delay(10000); }
}

void led_thread_entry(void* args) {
    (void)args;
    thread_log("(led) placeholder");
    while (true) { delay(10000); }
}

void webserver_thread_entry(void* args) {
    (void)args;
    thread_log("(web) placeholder");
    while (true) { delay(10000); }
}

void scan_thread_entry(void* args) {
    (void)args;
    thread_log("(scan) placeholder");
    while (true) { delay(10000); }
}

void swarm_thread_entry(void* args) {
    (void)args;
    thread_log("(swarm) placeholder");
    while (true) { delay(10000); }
}

void market_thread_entry(void* args) {
    (void)args;
    thread_log("(market) placeholder");
    while (true) { delay(10000); }
}

void lvgl_thread_entry(void* args) {
    (void)args;
    thread_log("(lvgl) placeholder");
    while (true) { delay(5); }
}

void ui_thread_entry(void* args) {
    (void)args;
    thread_log("(ui) placeholder");
    while (true) { delay(100); }
}

void benchmark_thread_entry(void* args) {
    (void)args;
    thread_log("(benchmark) placeholder");
    while (true) { delay(10000); }
}
