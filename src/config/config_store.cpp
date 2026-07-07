// What: NVS-backed application config store seeded by board defaults.
// Why: The new framework now has enough stable config structure to persist
// multi-domain settings without leaking raw NVS reads into services.
// Role: Reads defaults from BSP policy plus legacy firmware defaults, overlays
// any saved NVS values, and writes the normalized `AppConfig` back through the
// shared storage driver.
// Benefit: Services consume one stable config object while persistence details
// remain isolated in this layer.
#include "config/config_store.h"

#include <math.h>
#include <mbedtls/sha256.h>
#include <stdlib.h>

#include "config/nvs_keys.h"
#include "drivers/storage/storage.h"

namespace nm::config {

namespace {

constexpr char kDefaultTimezone[] = "8.0";
constexpr uint8_t kDefaultHourFormat = 24;
constexpr char kDefaultDateFormat[] = "YYYY/MM/DD";
constexpr char kDefaultWifiSsid[] = "NMTech-2.4G";
constexpr char kDefaultWifiPassword[] = "NMMiner2048";
constexpr char kDefaultPrimaryPoolUrl[] = "stratum+tcp://solo.ckpool.org:3333";
constexpr char kDefaultFallbackPoolUrl[] = "stratum+tcp://xec.nmminer.com:3333";
constexpr char kDefaultPrimaryPoolUser[] = "18dK8EfyepKuS74fs27iuDJWoGUT4rPto1";
constexpr char kDefaultFallbackPoolUser[] = "ecash:qpf6dlpplgltcxuq4rve99jfk67z4tlcjc3sscrrsf";
constexpr char kDefaultPoolPassword[] = "x";
constexpr char kDefaultMarketCoin[] = "BTC";
constexpr char kDefaultCoinWatchlist[] = "BTC,ETH,LTC,BNB,DOGE,XRP,TRX,SOL";
constexpr char kDefaultThemeScheme[] = "dark";
constexpr char kDefaultThemeName[] = "dark";
constexpr char kDefaultThemeColors[] =
    "{"
    "\"--primary-color\":\"#F80421\","
    "\"--primary-color-text\":\"#ffffff\","
    "\"--highlight-bg\":\"#F80421\","
    "\"--highlight-text-color\":\"#ffffff\","
    "\"--focus-ring\":\"0 0 0 0.2rem rgba(248,4,33,0.2)\","
    "\"--slider-bg\":\"#dee2e6\","
    "\"--slider-range-bg\":\"#F80421\","
    "\"--slider-handle-bg\":\"#F80421\","
    "\"--progressbar-bg\":\"#dee2e6\","
    "\"--progressbar-value-bg\":\"#F80421\","
    "\"--checkbox-border\":\"#F80421\","
    "\"--checkbox-bg\":\"#F80421\","
    "\"--checkbox-hover-bg\":\"#df031d\","
    "\"--button-bg\":\"#F80421\","
    "\"--button-hover-bg\":\"#df031d\","
    "\"--button-focus-shadow\":\"0 0 0 2px #ffffff, 0 0 0 4px #F80421\","
    "\"--togglebutton-bg\":\"#F80421\","
    "\"--togglebutton-border\":\"1px solid #F80421\","
    "\"--togglebutton-hover-bg\":\"#df031d\","
    "\"--togglebutton-hover-border\":\"1px solid #df031d\","
    "\"--togglebutton-text-color\":\"#ffffff\""
    "}";

String board_name(const bsp::Board& board) {
    return String(board.traits().board_name);
}

String device_code() {
    char chip_id[13] = {};
    unsigned char digest[32] = {};

    snprintf(chip_id, sizeof(chip_id), "%012llx", ESP.getEfuseMac());
    mbedtls_sha256_ret(
        reinterpret_cast<const unsigned char*>(chip_id),
        12,
        digest,
        0);

    String code;
    code.reserve(64);
    for (size_t index = 0; index < sizeof(digest); ++index) {
        char hex[3] = {};
        snprintf(hex, sizeof(hex), "%02x", digest[index]);
        code += hex;
    }
    return code;
}

String device_suffix() {
    const String code = device_code();
    return code.length() >= 5 ? code.substring(0, 5) : code;
}

String default_ap_ssid(const bsp::Board& board) {
    return board_name(board) + "_" + device_suffix();
}

String default_worker_name(const char* wallet, const bsp::Board& board) {
    return String(wallet) + "." + board_name(board) + "_" + device_suffix();
}

float parse_float_setting(const String& value, float default_value) {
    if (value.isEmpty()) {
        return default_value;
    }

    char* end = nullptr;
    const float parsed = strtof(value.c_str(), &end);
    if (end == value.c_str()) {
        return default_value;
    }

    return parsed;
}

String format_float_setting(float value) {
    const float rounded = roundf(value);
    if (fabsf(value - rounded) < 0.01f) {
        return String(static_cast<int32_t>(rounded));
    }

    return String(value, 1);
}

uint8_t clamp_percent_u8(uint16_t value) {
    return value > 100u ? 100u : static_cast<uint8_t>(value);
}

void apply_board_defaults(const bsp::Board& board, AppConfig& config) {
    const auto& board_defaults = board.config_defaults();

    config.time.timezone = kDefaultTimezone;
    config.time.hour_format = kDefaultHourFormat;
    config.time.date_format = kDefaultDateFormat;

    config.network.sta_ssid = kDefaultWifiSsid;
    config.network.sta_password = kDefaultWifiPassword;
    config.network.ap_ssid = default_ap_ssid(board);
    config.network.hostname = config.network.ap_ssid;
    config.network.force_config = false;

    config.stratum.primary.url = kDefaultPrimaryPoolUrl;
    config.stratum.primary.user = default_worker_name(kDefaultPrimaryPoolUser, board);
    config.stratum.primary.password = kDefaultPoolPassword;
    config.stratum.fallback.url = kDefaultFallbackPoolUrl;
    config.stratum.fallback.user = default_worker_name(kDefaultFallbackPoolUser, board);
    config.stratum.fallback.password = kDefaultPoolPassword;

    config.screen.brightness_percent = board.policies().default_brightness_pct;
    config.screen.flip = board.policies().default_flip;
    config.screen.auto_cycle_pages = board_defaults.auto_cycle_pages;
    config.screen.screensaver_enabled = board_defaults.screensaver_enabled;
    config.screen.screensaver_timeout_s = board_defaults.screensaver_timeout_s;
    config.screen.screensaver_mode = 0;

    config.mining.target_freq_mhz = board.policies().default_freq_mhz;
    config.mining.target_vcore_mv = board.policies().default_vcore_mv;

    config.led.indicator_enabled = board_defaults.led_indicator_enabled;

    config.cooling.asic.auto_control = board_defaults.asic_fan.auto_control;
    config.cooling.asic.target_temp_c = board_defaults.asic_fan.target_temp_c;
    config.cooling.asic.manual_speed_percent = 100;
    config.cooling.vcore.auto_control = board_defaults.vcore_fan.auto_control;
    config.cooling.vcore.target_temp_c = board_defaults.vcore_fan.target_temp_c;
    config.cooling.vcore.manual_speed_percent = 100;
    config.cooling.invert_polarity = false;
    config.cooling.tps53647_phase_count = 0;

    config.market.display_coin = kDefaultMarketCoin;
    config.market.watchlist = kDefaultCoinWatchlist;

    config.theme.color_scheme = kDefaultThemeScheme;
    config.theme.name = kDefaultThemeName;
    config.theme.accent_colors_json = kDefaultThemeColors;

    config.benchmark.mode = 0;
    config.benchmark.freq_min_mhz = board_defaults.benchmark.freq_min_mhz;
    config.benchmark.freq_max_mhz = board_defaults.benchmark.freq_max_mhz;
    config.benchmark.freq_step_mhz = board_defaults.benchmark.freq_step_mhz;
    config.benchmark.vcore_min_mv = board_defaults.benchmark.vcore_min_mv;
    config.benchmark.vcore_max_mv = board_defaults.benchmark.vcore_max_mv;
    config.benchmark.vcore_step_mv = board_defaults.benchmark.vcore_step_mv;
    config.benchmark.sample_interval_s = board_defaults.benchmark.sample_interval_s;
    config.benchmark.benchmark_time_s = board_defaults.benchmark.benchmark_time_s;
    config.benchmark.stabilize_time_s = board_defaults.benchmark.stabilize_time_s;
    config.benchmark.current_freq_mhz = config.benchmark.freq_min_mhz;
    config.benchmark.current_vcore_mv = config.benchmark.vcore_min_mv;
    config.benchmark.result_json = "[]";
    config.benchmark.start_timestamp = 0;
    config.benchmark.total_seconds = 0;

    config.ui.startup_page = 0;
}

}  // namespace

bool NvsConfigStore::init() {
    return drivers::storage::init_flash();
}

bool NvsConfigStore::load(const bsp::Board& board, AppConfig& config) {
    apply_board_defaults(board, config);

    drivers::storage::Storage storage(NVS_CONFIG_NAMESPACE, false);
    if (!storage.valid()) {
        return true;
    }

    config.time.timezone = storage.get_string(NVS_CONFIG_TIMEZONE, config.time.timezone);
    config.time.hour_format = storage.get_u8(NVS_CONFIG_TIME_FORMAT, config.time.hour_format) == 12 ? 12 : 24;
    config.time.date_format = storage.get_string(NVS_CONFIG_DATE_FORMAT, config.time.date_format);

    config.network.ap_ssid = storage.get_string(NVS_CONFIG_AP_SSID, config.network.ap_ssid);
    config.network.sta_ssid = storage.get_string(NVS_CONFIG_WIFI_SSID, config.network.sta_ssid);
    config.network.sta_password = storage.get_string(NVS_CONFIG_WIFI_PASS, config.network.sta_password);
    config.network.hostname = storage.get_string(NVS_CONFIG_HOSTNAME, config.network.ap_ssid);
    config.network.force_config = storage.get_bool(NVS_CONFIG_FORCE_CONFIG, config.network.force_config);

    config.stratum.primary.url = storage.get_string(NVS_CONFIG_STRATUM_URL_PRIMARY, config.stratum.primary.url);
    config.stratum.primary.user = storage.get_string(NVS_CONFIG_STRATUM_USER_PRIMARY, config.stratum.primary.user);
    config.stratum.primary.password = storage.get_string(NVS_CONFIG_STRATUM_PASS_PRIMARY, config.stratum.primary.password);
    config.stratum.fallback.url = storage.get_string(NVS_CONFIG_STRATUM_URL_FALLBACK, config.stratum.fallback.url);
    config.stratum.fallback.user = storage.get_string(NVS_CONFIG_STRATUM_USER_FALLBACK, config.stratum.fallback.user);
    config.stratum.fallback.password = storage.get_string(NVS_CONFIG_STRATUM_PASS_FALLBACK, config.stratum.fallback.password);

    config.screen.brightness_percent = storage.get_u8(NVS_CONFIG_SCREEN_BRIGHTNESS, config.screen.brightness_percent);
    config.screen.flip = storage.get_bool(NVS_CONFIG_FLIP_SCREEN, config.screen.flip);
    config.screen.auto_cycle_pages = storage.get_bool(NVS_CONFIG_AUTO_SCREEN, config.screen.auto_cycle_pages);
    config.screen.screensaver_enabled = storage.get_bool(NVS_CONFIG_SCREEN_SAVER_ENABLE, config.screen.screensaver_enabled);
    config.screen.screensaver_timeout_s = storage.get_u32(NVS_CONFIG_SCREEN_SAVER_TIMEOUT, config.screen.screensaver_timeout_s);
    config.screen.screensaver_mode = storage.get_u8(NVS_CONFIG_SCREEN_SAVER_MODE, config.screen.screensaver_mode);

    config.mining.target_freq_mhz = storage.get_u16(NVS_CONFIG_ASIC_FREQ, config.mining.target_freq_mhz);
    config.mining.target_vcore_mv = storage.get_u16(NVS_CONFIG_ASIC_VOLTAGE, config.mining.target_vcore_mv);

    config.led.indicator_enabled = storage.get_bool(NVS_CONFIG_LED_INDICATOR, config.led.indicator_enabled);

    config.cooling.asic.auto_control = storage.get_u16(NVS_CONFIG_AUTO_ASIC_FAN_SPEED, config.cooling.asic.auto_control ? 1u : 0u) != 0;
    config.cooling.asic.target_temp_c = parse_float_setting(storage.get_string(NVS_CONFIG_ASIC_TARGET_TEMP, format_float_setting(config.cooling.asic.target_temp_c)), config.cooling.asic.target_temp_c);
    config.cooling.asic.manual_speed_percent = clamp_percent_u8(storage.get_u16(NVS_CONFIG_ASIC_FAN_SPEED, config.cooling.asic.manual_speed_percent));
    config.cooling.vcore.auto_control = storage.get_u16(NVS_CONFIG_AUTO_VCORE_FAN_SPEED, config.cooling.vcore.auto_control ? 1u : 0u) != 0;
    config.cooling.vcore.target_temp_c = parse_float_setting(storage.get_string(NVS_CONFIG_VCORE_TARGET_TEMP, format_float_setting(config.cooling.vcore.target_temp_c)), config.cooling.vcore.target_temp_c);
    config.cooling.vcore.manual_speed_percent = clamp_percent_u8(storage.get_u16(NVS_CONFIG_VCORE_FAN_SPEED, config.cooling.vcore.manual_speed_percent));
    config.cooling.invert_polarity = storage.get_bool(NVS_CONFIG_INVERT_FAN_POLARITY, config.cooling.invert_polarity);
    config.cooling.tps53647_phase_count = static_cast<uint8_t>(storage.get_u16(NVS_CONFIG_TPS53647_PHASE_NUM, config.cooling.tps53647_phase_count));

    config.market.display_coin = storage.get_string(NVS_CONFIG_PRICE_DISPLAY_COIN, config.market.display_coin);
    config.market.watchlist = storage.get_string(NVS_CONFIG_COIN_WATCHLIST, config.market.watchlist);

    config.theme.color_scheme = storage.get_string(NVS_CONFIG_THEME_SCHEME, config.theme.color_scheme);
    config.theme.name = storage.get_string(NVS_CONFIG_THEME_NAME, config.theme.name);
    config.theme.accent_colors_json = storage.get_string(NVS_CONFIG_THEME_COLORS, config.theme.accent_colors_json);

    config.benchmark.mode = storage.get_u8(NVS_CONFIG_BM_MODE, config.benchmark.mode);
    config.benchmark.freq_min_mhz = storage.get_u16(NVS_CONFIG_BM_FREQ_MIN, config.benchmark.freq_min_mhz);
    config.benchmark.freq_max_mhz = storage.get_u16(NVS_CONFIG_BM_FREQ_MAX, config.benchmark.freq_max_mhz);
    config.benchmark.freq_step_mhz = storage.get_u16(NVS_CONFIG_BM_FREQ_STEP, config.benchmark.freq_step_mhz);
    config.benchmark.vcore_min_mv = storage.get_u16(NVS_CONFIG_BM_VCORE_MIN, config.benchmark.vcore_min_mv);
    config.benchmark.vcore_max_mv = storage.get_u16(NVS_CONFIG_BM_VCORE_MAX, config.benchmark.vcore_max_mv);
    config.benchmark.vcore_step_mv = storage.get_u16(NVS_CONFIG_BM_VCORE_STEP, config.benchmark.vcore_step_mv);
    config.benchmark.sample_interval_s = storage.get_u8(NVS_CONFIG_BM_SAMPLE_INTV, config.benchmark.sample_interval_s);
    config.benchmark.benchmark_time_s = storage.get_u16(NVS_CONFIG_BM_TIME, config.benchmark.benchmark_time_s);
    config.benchmark.stabilize_time_s = storage.get_u16(NVS_CONFIG_BM_STAB_TIME, config.benchmark.stabilize_time_s);
    config.benchmark.current_freq_mhz = storage.get_u16(NVS_CONFIG_BM_CUR_FREQ, config.benchmark.freq_min_mhz);
    config.benchmark.current_vcore_mv = storage.get_u16(NVS_CONFIG_BM_CUR_VCORE, config.benchmark.vcore_min_mv);
    config.benchmark.result_json = storage.get_string(NVS_CONFIG_BM_RESULT, config.benchmark.result_json);
    config.benchmark.start_timestamp = storage.get_u32(NVS_CONFIG_BM_START_TS, config.benchmark.start_timestamp);
    config.benchmark.total_seconds = storage.get_u32(NVS_CONFIG_BM_TOTAL_SEC, config.benchmark.total_seconds);

    if (config.benchmark.mode == 1) {
        // Preserve the old boot rule: benchmark mode resumes from the persisted
        // current sweep point instead of the normal ASIC target, while Vcore is
        // still clamped to the board's safe operating range before bring-up.
        config.mining.target_freq_mhz = config.benchmark.current_freq_mhz;

        uint16_t target_vcore_mv = config.benchmark.current_vcore_mv;
        if (board.policies().min_vcore_mv > 0 && target_vcore_mv < board.policies().min_vcore_mv) {
            target_vcore_mv = board.policies().min_vcore_mv;
        }
        if (board.policies().max_vcore_mv > 0 && target_vcore_mv > board.policies().max_vcore_mv) {
            target_vcore_mv = board.policies().max_vcore_mv;
        }
        config.mining.target_vcore_mv = target_vcore_mv;
    }

    // Keep this on board-default fallback until the new UI page model is aligned
    // with the persisted `lastPage` semantics. Loading that value here now
    // would reinterpret old page ids as new placeholder page ids.
    config.ui.startup_page = 0;

    return true;
}

bool NvsConfigStore::save(const AppConfig& config) {
    drivers::storage::Storage storage(NVS_CONFIG_NAMESPACE, true);
    if (!storage.valid()) {
        return false;
    }

    bool ok = true;
    ok = storage.set_string(NVS_CONFIG_TIMEZONE, config.time.timezone) && ok;
    ok = storage.set_u8(NVS_CONFIG_TIME_FORMAT, config.time.hour_format) && ok;
    ok = storage.set_string(NVS_CONFIG_DATE_FORMAT, config.time.date_format) && ok;

    ok = storage.set_string(NVS_CONFIG_WIFI_SSID, config.network.sta_ssid) && ok;
    ok = storage.set_string(NVS_CONFIG_WIFI_PASS, config.network.sta_password) && ok;
    ok = storage.set_string(NVS_CONFIG_HOSTNAME, config.network.hostname) && ok;
    ok = storage.set_string(NVS_CONFIG_AP_SSID, config.network.ap_ssid) && ok;
    ok = storage.set_bool(NVS_CONFIG_FORCE_CONFIG, config.network.force_config) && ok;

    ok = storage.set_string(NVS_CONFIG_STRATUM_URL_PRIMARY, config.stratum.primary.url) && ok;
    ok = storage.set_string(NVS_CONFIG_STRATUM_USER_PRIMARY, config.stratum.primary.user) && ok;
    ok = storage.set_string(NVS_CONFIG_STRATUM_PASS_PRIMARY, config.stratum.primary.password) && ok;
    ok = storage.set_string(NVS_CONFIG_STRATUM_URL_FALLBACK, config.stratum.fallback.url) && ok;
    ok = storage.set_string(NVS_CONFIG_STRATUM_USER_FALLBACK, config.stratum.fallback.user) && ok;
    ok = storage.set_string(NVS_CONFIG_STRATUM_PASS_FALLBACK, config.stratum.fallback.password) && ok;

    ok = storage.set_u8(NVS_CONFIG_SCREEN_BRIGHTNESS, config.screen.brightness_percent) && ok;
    ok = storage.set_bool(NVS_CONFIG_FLIP_SCREEN, config.screen.flip) && ok;
    ok = storage.set_bool(NVS_CONFIG_AUTO_SCREEN, config.screen.auto_cycle_pages) && ok;
    ok = storage.set_bool(NVS_CONFIG_SCREEN_SAVER_ENABLE, config.screen.screensaver_enabled) && ok;
    ok = storage.set_u32(NVS_CONFIG_SCREEN_SAVER_TIMEOUT, config.screen.screensaver_timeout_s) && ok;
    ok = storage.set_u8(NVS_CONFIG_SCREEN_SAVER_MODE, config.screen.screensaver_mode) && ok;

    ok = storage.set_u16(NVS_CONFIG_ASIC_FREQ, config.mining.target_freq_mhz) && ok;
    ok = storage.set_u16(NVS_CONFIG_ASIC_VOLTAGE, config.mining.target_vcore_mv) && ok;

    ok = storage.set_bool(NVS_CONFIG_LED_INDICATOR, config.led.indicator_enabled) && ok;

    ok = storage.set_u16(NVS_CONFIG_AUTO_ASIC_FAN_SPEED, config.cooling.asic.auto_control ? 1u : 0u) && ok;
    ok = storage.set_string(
        NVS_CONFIG_ASIC_TARGET_TEMP,
        format_float_setting(config.cooling.asic.target_temp_c)) && ok;
    ok = storage.set_u16(NVS_CONFIG_ASIC_FAN_SPEED, config.cooling.asic.manual_speed_percent) && ok;
    ok = storage.set_u16(NVS_CONFIG_AUTO_VCORE_FAN_SPEED, config.cooling.vcore.auto_control ? 1u : 0u) && ok;
    ok = storage.set_string(
        NVS_CONFIG_VCORE_TARGET_TEMP,
        format_float_setting(config.cooling.vcore.target_temp_c)) && ok;
    ok = storage.set_u16(NVS_CONFIG_VCORE_FAN_SPEED, config.cooling.vcore.manual_speed_percent) && ok;
    ok = storage.set_bool(NVS_CONFIG_INVERT_FAN_POLARITY, config.cooling.invert_polarity) && ok;
    ok = storage.set_u16(NVS_CONFIG_TPS53647_PHASE_NUM, config.cooling.tps53647_phase_count) && ok;

    ok = storage.set_string(NVS_CONFIG_PRICE_DISPLAY_COIN, config.market.display_coin) && ok;
    ok = storage.set_string(NVS_CONFIG_COIN_WATCHLIST, config.market.watchlist) && ok;

    ok = storage.set_string(NVS_CONFIG_THEME_SCHEME, config.theme.color_scheme) && ok;
    ok = storage.set_string(NVS_CONFIG_THEME_NAME, config.theme.name) && ok;
    ok = storage.set_string(NVS_CONFIG_THEME_COLORS, config.theme.accent_colors_json) && ok;

    ok = storage.set_u8(NVS_CONFIG_BM_MODE, config.benchmark.mode) && ok;
    ok = storage.set_u16(NVS_CONFIG_BM_FREQ_MIN, config.benchmark.freq_min_mhz) && ok;
    ok = storage.set_u16(NVS_CONFIG_BM_FREQ_MAX, config.benchmark.freq_max_mhz) && ok;
    ok = storage.set_u16(NVS_CONFIG_BM_FREQ_STEP, config.benchmark.freq_step_mhz) && ok;
    ok = storage.set_u16(NVS_CONFIG_BM_VCORE_MIN, config.benchmark.vcore_min_mv) && ok;
    ok = storage.set_u16(NVS_CONFIG_BM_VCORE_MAX, config.benchmark.vcore_max_mv) && ok;
    ok = storage.set_u16(NVS_CONFIG_BM_VCORE_STEP, config.benchmark.vcore_step_mv) && ok;
    ok = storage.set_u8(NVS_CONFIG_BM_SAMPLE_INTV, config.benchmark.sample_interval_s) && ok;
    ok = storage.set_u16(NVS_CONFIG_BM_TIME, config.benchmark.benchmark_time_s) && ok;
    ok = storage.set_u16(NVS_CONFIG_BM_STAB_TIME, config.benchmark.stabilize_time_s) && ok;
    ok = storage.set_u16(NVS_CONFIG_BM_CUR_FREQ, config.benchmark.current_freq_mhz) && ok;
    ok = storage.set_u16(NVS_CONFIG_BM_CUR_VCORE, config.benchmark.current_vcore_mv) && ok;
    ok = storage.set_string(NVS_CONFIG_BM_RESULT, config.benchmark.result_json) && ok;
    ok = storage.set_u32(NVS_CONFIG_BM_START_TS, config.benchmark.start_timestamp) && ok;
    ok = storage.set_u32(NVS_CONFIG_BM_TOTAL_SEC, config.benchmark.total_seconds) && ok;

    return storage.commit() && ok;
}

}  // namespace nm::config
