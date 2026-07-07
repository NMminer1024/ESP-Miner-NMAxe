// What: Runtime configuration model consumed by the service layer.
// Why: User preferences and operating targets should be loaded into one stable
// project-owned struct instead of being pulled ad hoc from hardware or NVS.
// Role: Carries the normalized application configuration across screen,
// mining, network, pool, cooling, theme, and benchmark domains.
// Benefit: Services can depend on one coherent config object, which makes
// future persistence backends and UI editing workflows much easier to add.
#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace nm::config {

struct TimeConfig {
    String timezone = "8.0";
    uint8_t hour_format = 24;
    String date_format = "YYYY/MM/DD";
};

struct NetworkConfig {
    String sta_ssid = "NMTech-2.4G";
    String sta_password = "NMMiner2048";
    String hostname{};
    String ap_ssid{};
    bool force_config = false;
};

struct StratumEndpointConfig {
    String url{};
    String user{};
    String password = "x";
};

struct StratumConfig {
    StratumEndpointConfig primary;
    StratumEndpointConfig fallback;
};

struct ScreenConfig {
    uint8_t brightness_percent = 100;
    bool flip = false;
    bool auto_cycle_pages = false;
    bool screensaver_enabled = false;
    uint32_t screensaver_timeout_s = 15 * 60;
    uint8_t screensaver_mode = 0;
};

struct MiningConfig {
    uint16_t target_freq_mhz = 0;
    uint16_t target_vcore_mv = 0;
};

struct LedConfig {
    bool indicator_enabled = true;
};

struct FanControlConfig {
    bool auto_control = true;
    float target_temp_c = 0.0f;
    uint8_t manual_speed_percent = 100;
};

struct CoolingConfig {
    FanControlConfig asic;
    FanControlConfig vcore;
    bool invert_polarity = false;
    uint8_t tps53647_phase_count = 0;
};

struct MarketConfig {
    String display_coin = "BTC";
    String watchlist = "BTC,ETH,LTC,BNB,DOGE,XRP,TRX,SOL";
};

struct ThemeConfig {
    String color_scheme = "dark";
    String name = "dark";
    String accent_colors_json{};
};

struct BenchmarkConfig {
    uint8_t mode = 0;
    uint16_t freq_min_mhz = 400;
    uint16_t freq_max_mhz = 625;
    uint16_t freq_step_mhz = 50;
    uint16_t vcore_min_mv = 1000;
    uint16_t vcore_max_mv = 1300;
    uint16_t vcore_step_mv = 25;
    uint8_t sample_interval_s = 10;
    uint16_t benchmark_time_s = 180;
    uint16_t stabilize_time_s = 120;
    uint16_t current_freq_mhz = 400;
    uint16_t current_vcore_mv = 1000;
    String result_json = "[]";
    uint32_t start_timestamp = 0;
    uint32_t total_seconds = 0;
};

struct UiConfig {
    uint8_t startup_page = 0;
};

struct AppConfig {
    TimeConfig time;
    NetworkConfig network;
    StratumConfig stratum;
    ScreenConfig screen;
    MiningConfig mining;
    LedConfig led;
    CoolingConfig cooling;
    MarketConfig market;
    ThemeConfig theme;
    BenchmarkConfig benchmark;
    UiConfig ui;
};

}  // namespace nm::config
