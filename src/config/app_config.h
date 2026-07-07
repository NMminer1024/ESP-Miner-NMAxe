// What: Runtime configuration model consumed by the service layer.
// Why: User preferences and operating targets should be loaded into a stable
// project-owned struct instead of being pulled ad hoc from hardware or NVS.
// Role: Carries the current screen and mining-related configuration values.
// Benefit: Services can depend on one normalized config object, which makes
// future persistence backends and UI editing workflows much easier to add.
#pragma once

#include <stdint.h>

namespace nm::config {

struct ScreenConfig {
    uint8_t brightness_percent = 100;
    bool flip = false;
    bool auto_cycle_pages = false;
    bool screensaver_enabled = false;
    uint32_t screensaver_timeout_s = 15 * 60;
};

struct MiningConfig {
    uint16_t target_freq_mhz = 0;
    uint16_t target_vcore_mv = 0;
};

struct UiConfig {
    uint8_t startup_page = 0;
};

struct AppConfig {
    ScreenConfig screen;
    MiningConfig mining;
    UiConfig ui;
};

}  // namespace nm::config
