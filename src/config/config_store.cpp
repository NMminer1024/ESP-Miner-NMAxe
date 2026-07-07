// What: Temporary config-store implementation backed only by board defaults.
// Why: The new service architecture needs a config layer now, even though NVS
// migration is intentionally deferred until the flow boundaries are stable.
// Role: Seeds the application config from BSP policies and traits.
// Benefit: Keeps the service APIs final while allowing persistence to be added
// later without reworking the startup path.
#include "config/config_store.h"

namespace nm::config {

bool BoardDefaultConfigStore::init() {
    return true;
}

bool BoardDefaultConfigStore::load(const bsp::Board& board, AppConfig& config) {
    config.screen.brightness_percent = board.policies().default_brightness_pct;
    config.screen.flip = board.policies().default_flip;
    config.mining.target_freq_mhz = board.policies().default_freq_mhz;
    config.mining.target_vcore_mv = board.policies().default_vcore_mv;
    config.ui.startup_page = 0;
    return true;
}

bool BoardDefaultConfigStore::save(const AppConfig&) {
    // Temporary bring-up path: persistence is deferred until the service layer stabilizes.
    return true;
}

}  // namespace nm::config
