// What: Project-wide NVS namespace and key table.
// Why: The new framework must keep using the same persisted key strings that
// have already shipped in previous firmware, so these identifiers are now the
// canonical storage contract for this project.
// Role: Centralizes the full namespace and key-string table under `config/`,
// including keys not wired into the new framework yet.
// Benefit: All future services can read and write one authoritative key set,
// and the exact persisted strings stay frozen across framework refactors.
// Rule: Do not change any string literal in this file unless you intentionally
// break on-device NVS compatibility with existing deployed firmware.
#pragma once

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
#define NVS_CONFIG_ASIC_FAN_SPEED           "asicfanspeed"
#define NVS_CONFIG_VCORE_FAN_SPEED          "vcorefanspeed"
#define NVS_CONFIG_TPS53647_PHASE_NUM       "tps53647phn"

#define NVS_CONFIG_AUTO_SCREEN              "autoscreen"
#define NVS_CONFIG_BEST_EVER                "bestever"
#define NVS_CONFIG_SELF_TEST                "selftest"
#define NVS_CONFIG_BLOCK_HITS               "blockhits"
#define NVS_CONFIG_UPTIME                   "uptime"
#define NVS_CONFIG_FORCE_CONFIG             "forceconfig"
#define NVS_CONFIG_PRICE_DISPLAY_COIN       "maincoindisplay"
#define NVS_CONFIG_COIN_WATCHLIST           "coinwatchlist"
#define NVS_CONFIG_UI_LAST_PAGE             "lastPage"

#define NVS_CONFIG_THEME_SCHEME             "themescheme"
#define NVS_CONFIG_THEME_NAME               "themename"
#define NVS_CONFIG_THEME_COLORS             "themecolors"

#define NVS_CONFIG_SPIFFS_UPDATING          "spiffsupd"

#define NVS_CONFIG_BM_MODE                  "bm_mode"
#define NVS_CONFIG_BM_FREQ_MIN              "bm_freq_min"
#define NVS_CONFIG_BM_FREQ_MAX              "bm_freq_max"
#define NVS_CONFIG_BM_FREQ_STEP             "bm_freq_step"
#define NVS_CONFIG_BM_VCORE_MIN             "bm_vcore_min"
#define NVS_CONFIG_BM_VCORE_MAX             "bm_vcore_max"
#define NVS_CONFIG_BM_VCORE_STEP            "bm_vcore_step"
#define NVS_CONFIG_BM_SAMPLE_INTV           "bm_smp_intv"
#define NVS_CONFIG_BM_TIME                  "bm_time"
#define NVS_CONFIG_BM_STAB_TIME             "bm_stab_time"
#define NVS_CONFIG_BM_CUR_FREQ              "bm_cur_freq"
#define NVS_CONFIG_BM_CUR_VCORE             "bm_cur_vcore"
#define NVS_CONFIG_BM_RESULT                "bm_result"
#define NVS_CONFIG_BM_START_TS              "bm_start_ts"
#define NVS_CONFIG_BM_TOTAL_SEC             "bm_total_sec"
