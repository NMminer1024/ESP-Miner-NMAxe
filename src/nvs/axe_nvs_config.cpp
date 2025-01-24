#include "axe_nvs_config.h"
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
    String stratum_url                          = String(nvs_config_get_string(NVS_CONFIG_STRATUM_URL, "stratum+ssl://hk.kxsw.pro"));
    g_nmaxe.board.fw_version                    = CURRENT_FW_VERSION;
    g_nmaxe.board.hw_version                    = CURRENT_HW_VERSION;
    g_nmaxe.board.hw_model                      = BOARD_MODEL;
    g_nmaxe.board.devcie_code                   = gen_device_code();
    g_nmaxe.board.temp_mcu                      = 0.0f;
    g_nmaxe.board.temp_vcore                    = 0.0f;
    g_nmaxe.asic.temp                           = 0.0f;
    g_nmaxe.mstatus.nvs_save_xsem               = xSemaphoreCreateCounting(1, 0);
    g_nmaxe.connection.wifi.reconnect_xsem      = xSemaphoreCreateCounting(1, 0);
    g_nmaxe.connection.wifi.force_cfg_xsem      = xSemaphoreCreateCounting(1, 0);
    g_nmaxe.mstatus.update_xsem                 = xSemaphoreCreateCounting(1, 0);
    g_nmaxe.connection.wifi.softap_param.ip     = IPAddress(192, 168, 4, 1);
    g_nmaxe.connection.wifi.softap_param.pwd    = "12345678";
    g_nmaxe.connection.wifi.softap_param.ssid   = "NMAxe_" + g_nmaxe.board.devcie_code.substring(0, 5);
    g_nmaxe.mstatus.block_hits                  = nvs_config_get_u16(NVS_CONFIG_BLOCK_HITS, 0);
    g_nmaxe.connection.force_config             = nvs_config_get_u8(NVS_CONFIG_FORCE_CONFIG, false);
    g_nmaxe.connection.client_connected         = false;
    g_nmaxe.connection.pool.ssl                 = (stratum_url.indexOf("ssl") != -1);
    g_nmaxe.connection.pool.url                 = stratum_url.substring(stratum_url.indexOf(":") + 3);
    g_nmaxe.connection.pool.port                = nvs_config_get_u16(NVS_CONFIG_STRATUM_PORT, 9527);
    g_nmaxe.connection.stratum.user             = String(nvs_config_get_string(NVS_CONFIG_STRATUM_USER, "18dK8EfyepKuS74fs27iuDJWoGUT4rPto1"));
    g_nmaxe.connection.stratum.pwd              = String(nvs_config_get_string(NVS_CONFIG_STRATUM_PASS, "d=15000"));
    g_nmaxe.connection.wifi.conn_param.ssid     = String(nvs_config_get_string(NVS_CONFIG_WIFI_SSID, "NMTech-2.4G"));
    g_nmaxe.connection.wifi.conn_param.pwd      = String(nvs_config_get_string(NVS_CONFIG_WIFI_PASS, "NMMiner2048"));
    g_nmaxe.connection.wifi.conn_param.hostname = String(nvs_config_get_string(NVS_CONFIG_HOSTNAME, g_nmaxe.connection.wifi.softap_param.ssid.c_str()));
    g_nmaxe.connection.stratum_update           = millis();
    g_nmaxe.mstatus.best_ever                   = strtoull(nvs_config_get_string(NVS_CONFIG_BEST_EVER, "0"), NULL, 10);
    g_nmaxe.asic.type                           = String(nvs_config_get_string(NVS_CONFIG_ASIC_MODEL, ASIC_MODEL));
    g_nmaxe.asic.frequency_req                  = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, 575);
    g_nmaxe.asic.vcore_req                      = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, 1300);
    g_nmaxe.preference.fan.is_auto_speed        = nvs_config_get_u16(NVS_CONFIG_AUTO_FAN_SPEED, true);
    g_nmaxe.preference.fan.invert_ploarity      = nvs_config_get_u16(NVS_CONFIG_INVERT_FAN_POLARITY, false); 
    g_nmaxe.preference.fan.speed                = nvs_config_get_u16(NVS_CONFIG_FAN_SPEED, 100);
    g_nmaxe.preference.fan.self_test            = false;
    g_nmaxe.preference.led.indicator            = nvs_config_get_u8(NVS_CONFIG_LED_INDICATOR, true);
    g_nmaxe.preference.screen.flip              = nvs_config_get_u8(NVS_CONFIG_FLIP_SCREEN, true);
    g_nmaxe.preference.screen.brightness        = nvs_config_get_u8(NVS_CONFIG_SCREEN_BRIGHTNESS, 99);
    g_nmaxe.mstatus.uptime_ever                 = nvs_config_get_u64(NVS_CONFIG_UPTIME, 0);
    g_nmaxe.coin                                = String(nvs_config_get_string(NVS_CONFIG_MINING_COIN, "btc"));
    g_nmaxe.miner                               = NULL;
    g_nmaxe.market                              = new MarketClass(MARKET_URL, MARKET_PORT, "/ws/" + g_nmaxe.coin + "usdt@avgPrice");
    g_nmaxe.stratum                             = new StratumClass(g_nmaxe.connection.pool, g_nmaxe.connection.stratum, 10);
    g_nmaxe.power                               = new NMAxePowerClass({NM_AXE_POWER_BM13xx_VPLL_ENABLE_PIN, NM_AXE_POWER_BM13xx_VDD_ENABLE_PIN, NM_AXE_POWER_BM13xx_VCORE_ENABLE_PIN},
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