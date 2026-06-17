#pragma once

// ============================================================================
//  Thread entry point stubs
//
//  Each thread receives its specific context struct as void* args.
//  No thread ever receives a global board_sal_t pointer.
// ============================================================================

// ── Stratum: pool communication ─────────────────────────────────────────────
void stratum_thread_entry(void* args);

// ── Miner TX/RX: ASIC job dispatch and response ─────────────────────────────
void miner_tx_thread_entry(void* args);
void miner_rx_thread_entry(void* args);

// ── WiFi: STA connect / AP config portal ────────────────────────────────────
void wifi_connect_thread_entry(void* args);

// ── Power init: rail bring-up, gate on fan/wifi/vbus, set vcore ──────────────
void power_init_thread_entry(void* args);

// ── Power loop: Vcore regulation, OC/OT monitoring ──────────────────────────
void power_loop_thread_entry(void* args);

// ── Fan: speed control ──────────────────────────────────────────────────────
void fan_thread_entry(void* args);

// ── LED: status indicator effects ───────────────────────────────────────────
void led_thread_entry(void* args);

// ── Webserver: HTTP + WebSocket ─────────────────────────────────────────────
void webserver_thread_entry(void* args);

// ── Scan: LAN alive IP discovery (ICMP ping) ────────────────────────────────
void scan_thread_entry(void* args);

// ── Swarm: probe / aggregate neighbor miner stats ───────────────────────────
void swarm_thread_entry(void* args);

// ── Market: cryptocurrency price & K-line data ──────────────────────────────
void market_thread_entry(void* args);

// ── LVGL: display refresh / lv_timer_handler ────────────────────────────────
void lvgl_thread_entry(void* args);

// ── UI: page management ─────────────────────────────────────────────────────
void ui_thread_entry(void* args);

// ── Benchmark: auto frequency/vcore tuning ───────────────────────────────────
void benchmark_thread_entry(void* args);
