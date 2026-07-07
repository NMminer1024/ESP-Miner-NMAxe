#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>

#define MINER_WIFI_CONFIG_TIMEOUT  (60 * 5)   // seconds of AP config window

// ============================================================================
//  WifiState — live network state (replaces board.status.wifi)
//
//  Written by the wifi thread (status/ip/...) and monitor (rssi); read by
//  stratum/market/monitor/display/web.
// ============================================================================
struct WifiState {
    volatile int      status = WL_DISCONNECTED;   // wl_status_t
    IPAddress         ip, gateway, subnet, dns;
    volatile int      rssi = 0;
    volatile bool     client_connected = false;
    volatile bool     force_config_required = false;
    volatile uint16_t config_timeout = 0;
    SemaphoreHandle_t reconnect_xsem = nullptr;
};

// ============================================================================
//  WifiConnConfig — connection parameters loaded once from NVS at boot
// ============================================================================
struct WifiConnConfig {
    String    sta_ssid, sta_pwd;
    String    ap_ssid;
    IPAddress ap_ip{192, 168, 4, 1};
    String    hostname;
    String    board_name;   // spec.name — selects config-timeout ownership branch
};

// ============================================================================
//  WifiCtx — launch context for wifi_connect / config_monitor threads
// ============================================================================
struct WifiCtx {
    WifiState*         state    = nullptr;
    WifiConnConfig*    cfg      = nullptr;
    EventGroupHandle_t init_evt = nullptr;
};
