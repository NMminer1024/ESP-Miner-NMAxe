#ifndef AXE_NVS_CONFIG_H
#define AXE_NVS_CONFIG_H

#include <stdint.h>

#define NVS_CONFIG_NAMESPACE "main"

#define NVS_CONFIG_TIMEZONE                 "timezone"
#define NVS_CONFIG_TIME_FORMAT              "timeformat"
#define NVS_CONFIG_DATE_FORMAT              "dateformat"
#define NVS_CONFIG_WIFI_SSID                "wifissid"
#define NVS_CONFIG_WIFI_PASS                "wifipass"
#define NVS_CONFIG_HOSTNAME                 "hostname"
#define NVS_CONFIG_AP_SSID                  "apssid"
#define NVS_CONFIG_STRATUM_USER_PRIMARY     "stratumuser"
#define NVS_CONFIG_STRATUM_URL_PRIMARY      "stratumurl1"
#define NVS_CONFIG_STRATUM_PASS_PRIMARY     "stratumpass1"
#define NVS_CONFIG_STRATUM_USER_FALLBACK    "stratumuser2"
#define NVS_CONFIG_STRATUM_URL_FALLBACK     "stratumurl2"
#define NVS_CONFIG_STRATUM_PASS_FALLBACK    "stratumpass2"
#define NVS_CONFIG_ASIC_FREQ                "asicfrequency"
#define NVS_CONFIG_ASIC_VOLTAGE             "asicvoltage"
#define NVS_CONFIG_SCREEN_BRIGHTNESS        "brightness"
#define NVS_CONFIG_DEVICE_MODEL             "devicemodel"
#define NVS_CONFIG_FLIP_SCREEN              "flipscreen"
#define NVS_CONFIG_LED_INDICATOR            "ledindicator"
#define NVS_CONFIG_INVERT_FAN_POLARITY      "invertfanpol"
#define NVS_CONFIG_SCREEN_SAVER_ENABLE      "scrsaverenable"
#define NVS_CONFIG_SCREEN_SAVER_TIMEOUT     "scrsavertimeout"
#define NVS_CONFIG_SCREEN_SAVER_MODE        "scrsavermode"
#define NVS_CONFIG_AUTO_ASIC_FAN_SPEED      "autoasicfanspd"
#define NVS_CONFIG_AUTO_VCORE_FAN_SPEED     "autovcorefanspd"
#define NVS_CONFIG_ASIC_TARGET_TEMP         "asictargettemp"
#define NVS_CONFIG_VCORE_TARGET_TEMP        "vcoretargettemp"
#define NVS_CONFIG_ASIC_FAN_SPEED           "asicfanspeed"  // enabe manual fan speed control for ASIC cooling, value in percentage (0-100)
#define NVS_CONFIG_VCORE_FAN_SPEED          "vcorefanspeed" // enabe manual fan speed control for Vcore cooling, value in percentage (0-100) , if applicable
#define NVS_CONFIG_TPS53647_PHASE_NUM       "tps53647phn"   // number of phases for TPS53647 power management (2 or 3), for QAxe++

#define NVS_CONFIG_AUTO_SCREEN              "autoscreen"
#define NVS_CONFIG_BEST_EVER                "bestever"
#define NVS_CONFIG_SELF_TEST                "selftest"
#define NVS_CONFIG_BLOCK_HITS               "blockhits"
#define NVS_CONFIG_UPTIME                   "uptime"
#define NVS_CONFIG_FORCE_CONFIG             "forceconfig"
#define NVS_CONFIG_PRICE_DISPLAY_COIN       "maincoindisplay"
#define NVS_CONFIG_COIN_WATCHLIST           "coinwatchlist"
#define NVS_CONFIG_UI_LAST_PAGE             "lastPage"
// Theme configuration
#define NVS_CONFIG_THEME_SCHEME         "themescheme"
#define NVS_CONFIG_THEME_NAME           "themename"
#define NVS_CONFIG_THEME_COLORS         "themecolors"
// Set to 1 before writing SPIFFS OTA; cleared on successful completion.
// If set at boot it means the last SPIFFS upload was interrupted — force recovery mode.
#define NVS_CONFIG_SPIFFS_UPDATING      "spiffsupd"

// ── Benchmark mode ──────────────────────────────────────────────────────────
// bm_mode: 0=Normal, 1=Benchmark
#define NVS_CONFIG_BM_MODE              "bm_mode"
// Sweep parameters (mirrors python tool: -fr/-fs/-vr/-vs/-si/-bt/-st)
#define NVS_CONFIG_BM_FREQ_MIN          "bm_freq_min"
#define NVS_CONFIG_BM_FREQ_MAX          "bm_freq_max"
#define NVS_CONFIG_BM_FREQ_STEP         "bm_freq_step"
#define NVS_CONFIG_BM_VCORE_MIN         "bm_vcore_min"
#define NVS_CONFIG_BM_VCORE_MAX         "bm_vcore_max"
#define NVS_CONFIG_BM_VCORE_STEP        "bm_vcore_step"
#define NVS_CONFIG_BM_SAMPLE_INTV       "bm_smp_intv"   // seconds, sample interval
#define NVS_CONFIG_BM_TIME              "bm_time"        // seconds, per-round benchmark duration
#define NVS_CONFIG_BM_STAB_TIME         "bm_stab_time"  // seconds, stabilize wait before sampling
// State: current round's freq/vcore written before each reboot
#define NVS_CONFIG_BM_CUR_FREQ          "bm_cur_freq"
#define NVS_CONFIG_BM_CUR_VCORE         "bm_cur_vcore"
// Result: JSON array string, appended after each stable round
#define NVS_CONFIG_BM_RESULT            "bm_result"
// Binary search state (per-frequency; reset at each freq advance)
#define NVS_CONFIG_BM_BISECT_LO         "bm_bisect_lo"  // u16, highest known unstable vcore; 0=not found
#define NVS_CONFIG_BM_BISECT_HI         "bm_bisect_hi"  // u16, lowest known stable vcore; 0=not found
// Cross-frequency state
#define NVS_CONFIG_BM_LAST_SV           "bm_last_sv"    // u16, last freq's stable vcore (for next freq start); 0=none
// Best-result cache (updated incrementally; avoids JSON re-parse at finish)
#define NVS_CONFIG_BM_BEST_FREQ         "bm_best_freq"  // u16
#define NVS_CONFIG_BM_BEST_VCORE        "bm_best_vcore" // u16
#define NVS_CONFIG_BM_BEST_HR           "bm_best_hr"    // u16, avgHR * 10 (GH/s)
#define NVS_CONFIG_BM_BEST_EFF          "bm_best_eff"   // u16, effJTH * 10 (J/TH)
// Safety
#define NVS_CONFIG_BM_TEMP_LIMIT        "bm_temp_lim"   // u8, ASIC temp upper limit in °C (default 82)

char * nvs_config_get_string(const char * key, const char * default_value);
void nvs_config_set_string(const char * key, const char * value);

uint8_t  nvs_config_get_u8(const char * key,  const uint8_t default_value);
void nvs_config_set_u8(const char * key, const uint8_t value);

uint16_t nvs_config_get_u16(const char * key, const uint16_t default_value);
void nvs_config_set_u16(const char * key, const uint16_t value);

uint32_t nvs_config_get_u32(const char * key, const uint32_t default_value);
void nvs_config_set_u32(const char * key, const uint32_t value);

uint64_t nvs_config_get_u64(const char * key, const uint64_t default_value);
void nvs_config_set_u64(const char * key, const uint64_t value);

bool erase_all_nvs(void);
bool nvs_config_delete_key(const char * key);
#endif // MAIN_NVS_CONFIG_H
