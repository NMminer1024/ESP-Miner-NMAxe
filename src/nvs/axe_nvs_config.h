#ifndef AXE_NVS_CONFIG_H
#define AXE_NVS_CONFIG_H

#include <stdint.h>

#define NVS_CONFIG_NAMESPACE "main"

#define NVS_CONFIG_WIFI_SSID "wifissid"
#define NVS_CONFIG_WIFI_PASS "wifipass"
#define NVS_CONFIG_HOSTNAME "hostname"
#define NVS_CONFIG_STRATUM_URL "stratumurl"
#define NVS_CONFIG_STRATUM_PORT "stratumport"
#define NVS_CONFIG_STRATUM_USER "stratumuser"
#define NVS_CONFIG_STRATUM_PASS "stratumpass"
#define NVS_CONFIG_ASIC_FREQ "asicfrequency"
#define NVS_CONFIG_ASIC_VOLTAGE "asicvoltage"
#define NVS_CONFIG_SCREEN_BRIGHTNESS "brightness"
#define NVS_CONFIG_ASIC_MODEL "asicmodel"
#define NVS_CONFIG_DEVICE_MODEL "devicemodel"
#define NVS_CONFIG_FLIP_SCREEN "flipscreen"
#define NVS_CONFIG_LED_INDICATOR "ledindicator"
#define NVS_CONFIG_INVERT_FAN_POLARITY "invertfanpol"
#define NVS_CONFIG_AUTO_FAN_SPEED "autofanspeed"
#define NVS_CONFIG_FAN_SPEED "fanspeed"
#define NVS_CONFIG_BEST_EVER "bestever"
#define NVS_CONFIG_SELF_TEST "selftest"
#define NVS_CONFIG_OVERHEAT_MODE "overheat_mode"
#define NVS_CONFIG_BLOCK_HITS "blockhits"
#define NVS_CONFIG_UPTIME     "uptime"
#define NVS_CONFIG_FORCE_CONFIG "forceconfig"

// #define NVS_CONFIG_SWARM "swarmconfig"

char * nvs_config_get_string(const char * key, const char * default_value);
void nvs_config_set_string(const char * key, const char * value);

uint8_t  nvs_config_get_u8(const char * key,  const uint8_t default_value);
void nvs_config_set_u8(const char * key, const uint8_t value);

uint16_t nvs_config_get_u16(const char * key, const uint16_t default_value);
void nvs_config_set_u16(const char * key, const uint16_t value);

uint64_t nvs_config_get_u64(const char * key, const uint64_t default_value);
void nvs_config_set_u64(const char * key, const uint64_t value);

bool load_g_nmaxe(void);
void clear_g_nmaxe(void);

#endif // MAIN_NVS_CONFIG_H
