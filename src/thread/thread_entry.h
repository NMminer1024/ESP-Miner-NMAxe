#ifndef _AXE_THREAD_ENTRY_H_
#define _AXE_THREAD_ENTRY_H_
#include <Arduino.h>

void power_init_thread_entry(void *args);
void power_loop_thread_entry(void *args);
void led_thread_entry(void *args);
void button_thread_entry(void *args);
void daemon_thread_entry(void *args);
void fan_thread_entry(void *args);
void market_thread_entry(void *args);
void miner_asic_count_thread_entry(void *args);
void miner_asic_init_thread_entry(void *args);
void miner_asic_tx_thread_entry(void *args);
void miner_asic_rx_thread_entry(void *args);
void swarm_thread_entry(void *args);
void monitor_thread_entry(void *args);
void wifi_connect_thread_entry(void *args);
void stratum_thread_entry(void *args);
void webserver_thread_entry(void *args);
void aphorism_thread_entry(void *args);
void alive_ip_scan_thread_entry(void* args);
#endif 