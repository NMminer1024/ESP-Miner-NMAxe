#include "nvs_config.h"
#include "esp_log.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <string.h>
#include "global.h"
#include "logger.h"
#include "helper.h"

char * nvs_config_get_string(const char * key, const char * default_value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return strdup(default_value);
    }

    size_t size = 0;
    err = nvs_get_str(handle, key, NULL, &size);

    if (err != ESP_OK) {
        return strdup(default_value);
    }

    char * out = (char *)malloc(size);
    err = nvs_get_str(handle, key, out, &size);

    if (err != ESP_OK) {
        free(out);
        return strdup(default_value);
    }

    nvs_close(handle);
    return out;
}

void nvs_config_set_string(const char * key, const char * value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return;
    }

    err = nvs_set_str(handle, key, value);
    if (err != ESP_OK) {
        return;
    }

    nvs_close(handle);
    return;
}


uint8_t nvs_config_get_u8(const char * key, const uint8_t default_value){
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return default_value;
    }

    uint8_t out;
    err = nvs_get_u8(handle, key, &out);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        return default_value;
    }
    return out;
}

void nvs_config_set_u8(const char * key, const uint8_t value){
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return;
    }

    err = nvs_set_u8(handle, key, value);
    if (err != ESP_OK) {
        return;
    }

    nvs_close(handle);
    return;
}

uint16_t nvs_config_get_u16(const char * key, const uint16_t default_value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return default_value;
    }

    uint16_t out;
    err = nvs_get_u16(handle, key, &out);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        return default_value;
    }
    return out;
}

void nvs_config_set_u16(const char * key, const uint16_t value)
{

    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return;
    }

    err = nvs_set_u16(handle, key, value);
    if (err != ESP_OK) {
        return;
    }

    nvs_close(handle);
    return;
}

uint64_t nvs_config_get_u64(const char * key, const uint64_t default_value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return default_value;
    }

    uint64_t out;
    err = nvs_get_u64(handle, key, &out);

    if (err != ESP_OK) {
        return default_value;
    }

    nvs_close(handle);
    return out;
}

void nvs_config_set_u64(const char * key, const uint64_t value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return;
    }

    err = nvs_set_u64(handle, key, value);
    if (err != ESP_OK) {
        return;
    }
    nvs_close(handle);
    return;
}

board_model_t get_board_model(){
    board_model_t model = BOARD_UNKNOWN;
    pinMode(NM_AXE_MODEL_SELECT_PIN0, INPUT_PULLUP);
    pinMode(NM_AXE_MODEL_SELECT_PIN1, INPUT_PULLUP);
    delay(100);
    
    uint8_t sel0 = digitalRead(NM_AXE_MODEL_SELECT_PIN0);
    uint8_t sel1 = digitalRead(NM_AXE_MODEL_SELECT_PIN1);
  
    if(sel0 == HIGH && sel1 == HIGH) model = NMAXE;//0b11
    else if(sel0 == LOW && sel1 == HIGH) model = NMAXE_GAMMA;//0b01
    else model = BOARD_UNKNOWN;// 0b10 or 0b00
  
    return model;
}

bool load_g_nmaxe(void){
    esp_err_t ret = nvs_flash_init();
    while (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        LOG_W("NVS partition is full or has invalid version, erasing...");
        if(nvs_flash_erase() != ESP_OK){
            LOG_E("NVS partition erase failed");
        }
        LOG_I("Reinitializing NVS...");
        ret = nvs_flash_init();
        delay(1000);
    }
    g_board.info.base.hw_model                       = BOARD_MODEL;
    g_board.status.asic.model                        = ASIC_MODEL;

    String stratum_pri                               = String(nvs_config_get_string(NVS_CONFIG_STRATUM_URL_PRIMARY,  PRIMARY_POOL_URL));
    String stratum_fb                                = String(nvs_config_get_string(NVS_CONFIG_STRATUM_URL_FALLBACK, FALLBACK_POOL_URL));
    
    g_board.info.connection.pool_primary.ssl         = (stratum_pri.indexOf("ssl") != -1);
    g_board.info.connection.pool_primary.url         = stratum_pri.substring(stratum_pri.indexOf(":") + 3, stratum_pri.lastIndexOf(":"));
    g_board.info.connection.pool_primary.port        = stratum_pri.substring(stratum_pri.lastIndexOf(":") + 1, stratum_pri.length()).toInt();
    g_board.info.connection.pool_fallback.ssl        = (stratum_fb.indexOf("ssl") != -1);
    g_board.info.connection.pool_fallback.url        = stratum_fb.substring(stratum_fb.indexOf(":") + 3, stratum_fb.lastIndexOf(":"));
    g_board.info.connection.pool_fallback.port       = stratum_fb.substring(stratum_fb.lastIndexOf(":") + 1, stratum_fb.length()).toInt();
    g_board.info.connection.pool_use                 = g_board.info.connection.pool_primary;

    g_board.info.base.fw_version                    = CURRENT_FW_VERSION;
    g_board.info.base.hw_version                    = CURRENT_HW_VERSION;
    g_board.info.base.devcie_code                   = gen_device_code();

    g_board.info.connection.stratum_primary.user     = String(nvs_config_get_string(NVS_CONFIG_STRATUM_USER_PRIMARY, (String(PRIMARY_USER) + "." + g_board.info.base.hw_model + "_" + g_board.info.base.devcie_code.substring(0, 5)).c_str()));
    g_board.info.connection.stratum_primary.pwd      = String(nvs_config_get_string(NVS_CONFIG_STRATUM_PASS_PRIMARY, "x"));
    g_board.info.connection.stratum_fallback.user    = String(nvs_config_get_string(NVS_CONFIG_STRATUM_USER_FALLBACK, (String(FALLBACK_USER) + "." + g_board.info.base.hw_model + "_" + g_board.info.base.devcie_code.substring(0, 5)).c_str()));
    g_board.info.connection.stratum_fallback.pwd     = String(nvs_config_get_string(NVS_CONFIG_STRATUM_PASS_FALLBACK, "d=512"));
    g_board.info.connection.stratum_use              = g_board.info.connection.stratum_primary;
    g_board.status.reboot_xsem                       = xSemaphoreCreateCounting(1, 0);
    g_board.info.base.fw_latest_release              = "";
    g_board.status.asic.job_frq_ms                   = ASIC_JOB_INTERVAL_MS;
    g_board.status.miner.hr_dist.max_x_hr            = HR_DIST_HEALTH_X_AXIS_MAX_HR;
    g_board.status.miner.hr_dist.max_x_bars          = HR_DIST_HEALTH_X_AXIS_BARS;
    g_board.status.asic.frequency_req                = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ,    ASIC_DEFAULT_FREQ);
    g_board.status.asic.vcore_req                    = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, ASIC_VCORE_DEFAULT);
    g_board.status.nvs_save_xsem                     = xSemaphoreCreateCounting(1, 0);
    g_board.status.miner.history_mutex               = xSemaphoreCreateMutex();
    g_board.status.miner.block_proximity_mutex       = xSemaphoreCreateMutex();
    g_board.info.connection.wifi.reconnect_xsem      = xSemaphoreCreateCounting(1, 0);
    g_board.info.connection.wifi.force_cfg_xsem      = xSemaphoreCreateCounting(1, 0);
    g_board.status.miner.update_xsem                 = xSemaphoreCreateCounting(1, 0);
    g_board.info.connection.wifi.softap_param.ip     = IPAddress(192, 168, 4, 1);
    g_board.info.connection.wifi.softap_param.pwd    = "12345678";
    g_board.info.connection.wifi.softap_param.ssid   = String(nvs_config_get_string(NVS_CONFIG_AP_SSID, ("NMAxe_" + g_board.info.base.devcie_code.substring(0, 5)).c_str())); 
    g_board.status.miner.hits                        = nvs_config_get_u16(NVS_CONFIG_BLOCK_HITS, 0);
    g_board.status.miner.last_hits                   = g_board.status.miner.hits;
    g_board.info.connection.force_config             = nvs_config_get_u8(NVS_CONFIG_FORCE_CONFIG, false);
    g_board.info.connection.client_connected         = false;
    g_board.info.connection.wifi.conn_param.ssid     = String(nvs_config_get_string(NVS_CONFIG_WIFI_SSID, "NMTech-2.4G"));
    g_board.info.connection.wifi.conn_param.pwd      = String(nvs_config_get_string(NVS_CONFIG_WIFI_PASS, "NMMiner2048"));
    g_board.info.base.hostname                       = String(nvs_config_get_string(NVS_CONFIG_HOSTNAME, g_board.info.connection.wifi.softap_param.ssid.c_str()));
    g_board.info.connection.stratum_update           = millis();
    g_board.status.ota.running                       = false;
    g_board.status.ota.progress                      = 0;
    g_board.status.ota.firmware                      = "";
    g_board.status.miner.diff.best_ever              = strtoull(nvs_config_get_string(NVS_CONFIG_BEST_EVER, "0"), NULL, 10);
    g_board.info.preference.fan.is_auto_speed        = nvs_config_get_u16(NVS_CONFIG_AUTO_FAN_SPEED, true);
    g_board.info.preference.fan.invert_ploarity      = nvs_config_get_u16(NVS_CONFIG_INVERT_FAN_POLARITY, true); 
    g_board.info.preference.fan.speed                = nvs_config_get_u16(NVS_CONFIG_FAN_SPEED, 100);
    g_board.info.preference.fan.target_temp          = String(nvs_config_get_string(NVS_CONFIG_ASIC_TARGET_TEMP, "45.0")).toFloat();
    g_board.info.preference.fan.self_test            = false;
    g_board.info.preference.screen.flip              = nvs_config_get_u8(NVS_CONFIG_FLIP_SCREEN, true);
    g_board.info.preference.screen.auto_screen       = nvs_config_get_u8(NVS_CONFIG_AUTO_SCREEN, false);
    g_board.info.preference.screen.brightness        = nvs_config_get_u8(NVS_CONFIG_SCREEN_BRIGHTNESS, 90);
    g_board.info.preference.screen.brightness_last   = g_board.info.preference.screen.brightness;
    g_board.info.preference.led.enable               = nvs_config_get_u8(NVS_CONFIG_LED_INDICATOR, true);
    g_board.info.preference.led.sleep                = false;
    g_board.info.preference.led.sleep_last           = g_board.info.preference.led.sleep;
    g_board.status.miner.uptime_ever                 = nvs_config_get_u64(NVS_CONFIG_UPTIME, 0);
    g_board.status.miner.timezone                    = String(nvs_config_get_string(NVS_CONFIG_TIMEZONE, "8.0"));
    g_board.info.base.coin                           = String(nvs_config_get_string(NVS_CONFIG_MINING_COIN, "BTC"));
    g_board.info.base.coin.toUpperCase();
    g_board.miner                                    = nullptr;
    g_board.market                                   = new MarketClass();
    g_board.stratum                                  = new StratumClass(g_board.info.connection.pool_use, g_board.info.connection.stratum_use, 10);
    g_board.power                                    = new NMAxePowerClass({NM_AXE_POWER_BM13xx_VPLL_ENABLE_PIN, NM_AXE_POWER_BM13xx_VDD_ENABLE_PIN, NM_AXE_POWER_BM13xx_VCORE_ENABLE_PIN},
                                                                      {NM_AXE_POWER_BM13xx_VBUS_ADC_PIN, NM_AXE_POWER_BM13xx_IBUS_ADC_PIN, NM_AXE_POWER_BM13xx_VCORE_ADC_PIN},
                                                                       NM_AXE_POWER_BM13xx_VCORE_REGULATOR_PWM_PIN, NM_AXE_POWER_BM13xx_VCORE_P_GOOD_DET_PIN, NM_AXE_POWER_BM13xx_VBUS_PLUG_SENSE_DET_PIN);
    
    
    
    return true;
}

void clear_g_nmaxe(void){
    esp_err_t err;
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        LOG_W("NVS partition is full or has invalid version, erasing...");
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            LOG_E("Failed to erase NVS partition: %s", esp_err_to_name(err));
            return;
        }
        err = nvs_flash_init();
        if (err != ESP_OK) {
            LOG_E("Failed to initialize NVS after erase: %s", esp_err_to_name(err));
            return;
        }
    } else if (err != ESP_OK) {
        LOG_E("Failed to initialize NVS: %s", esp_err_to_name(err));
        return;
    }
    err = nvs_flash_erase();
    if (err != ESP_OK) {
        LOG_E("Failed to erase NVS partition: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_flash_init();
    if (err != ESP_OK) {
        LOG_E("Failed to initialize NVS after erase: %s", esp_err_to_name(err));
        return;
    }
    LOG_I("NVS partition erased and reinitialized successfully");
}