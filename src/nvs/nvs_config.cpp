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

    board_model_t model = get_board_model();
    if(NMAXE == model){
        g_nmaxe.board.hw_model              = BOARD_NMAxe;
        g_nmaxe.asic.model                  = ASIC_1366;
        g_nmaxe.asic.job_frq_ms             = 2000;//send job every 2 seconds
        g_nmaxe.mstatus.hr_dist.max_x       = 1000;// 1000 GH/s for x axis
        g_nmaxe.mstatus.hr_dist.scale       = 50;  // 50 GH/s for every step
        g_nmaxe.asic.frequency_req          = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ,    550);
        g_nmaxe.asic.vcore_req              = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, 1200);
    }
    else if(NMAXE_GAMMA == model){
        g_nmaxe.board.hw_model              = BOARD_NMAxeGamma;
        g_nmaxe.asic.model                  = ASIC_1370;
        g_nmaxe.asic.job_frq_ms             = 500; //send job every 0.5 seconds
        g_nmaxe.mstatus.hr_dist.max_x       = 2000;// 2000 GH/s for x axis
        g_nmaxe.mstatus.hr_dist.scale       = 100; // 100 GH/s for every step
        g_nmaxe.asic.frequency_req          = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ,    600);
        g_nmaxe.asic.vcore_req              = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, 1125);
    }
    else{
        LOG_E("Board model not supported!");
        return false;
    }

    String stratum_pri                          = String(nvs_config_get_string(NVS_CONFIG_STRATUM_URL_PRIMARY,  PRIMARY_POOL_URL));
    String stratum_fb                           = String(nvs_config_get_string(NVS_CONFIG_STRATUM_URL_FALLBACK, FALLBACK_POOL_URL));
    
    g_nmaxe.connection.pool_primary.ssl         = (stratum_pri.indexOf("ssl") != -1);
    g_nmaxe.connection.pool_primary.url         = stratum_pri.substring(stratum_pri.indexOf(":") + 3, stratum_pri.lastIndexOf(":"));
    g_nmaxe.connection.pool_primary.port        = stratum_pri.substring(stratum_pri.lastIndexOf(":") + 1, stratum_pri.length()).toInt();
    g_nmaxe.connection.pool_fallback.ssl        = (stratum_fb.indexOf("ssl") != -1);
    g_nmaxe.connection.pool_fallback.url        = stratum_fb.substring(stratum_fb.indexOf(":") + 3, stratum_fb.lastIndexOf(":"));
    g_nmaxe.connection.pool_fallback.port       = stratum_fb.substring(stratum_fb.lastIndexOf(":") + 1, stratum_fb.length()).toInt();
    g_nmaxe.connection.pool_use                 = g_nmaxe.connection.pool_primary;

    g_nmaxe.connection.stratum_primary.user     = String(nvs_config_get_string(NVS_CONFIG_STRATUM_USER_PRIMARY, (String(PRIMARY_USER) + "." + g_nmaxe.board.hw_model).c_str()));
    g_nmaxe.connection.stratum_primary.pwd      = String(nvs_config_get_string(NVS_CONFIG_STRATUM_PASS_PRIMARY, "x"));
    g_nmaxe.connection.stratum_fallback.user    = String(nvs_config_get_string(NVS_CONFIG_STRATUM_USER_FALLBACK, (String(FALLBACK_USER) + "." + g_nmaxe.board.hw_model).c_str()));
    g_nmaxe.connection.stratum_fallback.pwd     = String(nvs_config_get_string(NVS_CONFIG_STRATUM_PASS_FALLBACK, "d=1024"));
    g_nmaxe.connection.stratum_use              = g_nmaxe.connection.stratum_primary;

    g_nmaxe.board.fw_version                    = CURRENT_FW_VERSION;
    g_nmaxe.board.hw_version                    = CURRENT_HW_VERSION;
    g_nmaxe.board.devcie_code                   = gen_device_code();
    g_nmaxe.board.reboot_xsem                   = xSemaphoreCreateCounting(1, 0);
    g_nmaxe.board.fw_latest_release             = "";
    g_nmaxe.temp.mcu                            = 0.0f;
    g_nmaxe.temp.vcore                          = 0.0f;
    g_nmaxe.temp.asic                           = 0.0f;
    g_nmaxe.mstatus.nvs_save_xsem               = xSemaphoreCreateCounting(1, 0);
    g_nmaxe.connection.wifi.reconnect_xsem      = xSemaphoreCreateCounting(1, 0);
    g_nmaxe.connection.wifi.force_cfg_xsem      = xSemaphoreCreateCounting(1, 0);
    g_nmaxe.mstatus.update_xsem                 = xSemaphoreCreateCounting(1, 0);
    g_nmaxe.connection.wifi.softap_param.ip     = IPAddress(192, 168, 4, 1);
    g_nmaxe.connection.wifi.softap_param.pwd    = "12345678";
    g_nmaxe.connection.wifi.softap_param.ssid   = String(nvs_config_get_string(NVS_CONFIG_AP_SSID, ("NMAxe_" + g_nmaxe.board.devcie_code.substring(0, 5)).c_str())); 
    g_nmaxe.mstatus.hits                        = nvs_config_get_u16(NVS_CONFIG_BLOCK_HITS, 0);
    g_nmaxe.mstatus.last_hits                   = g_nmaxe.mstatus.hits;
    g_nmaxe.connection.force_config             = nvs_config_get_u8(NVS_CONFIG_FORCE_CONFIG, false);
    g_nmaxe.connection.client_connected         = false;
    g_nmaxe.connection.wifi.conn_param.ssid     = String(nvs_config_get_string(NVS_CONFIG_WIFI_SSID, "NMTech-2.4G"));
    g_nmaxe.connection.wifi.conn_param.pwd      = String(nvs_config_get_string(NVS_CONFIG_WIFI_PASS, "NMMiner2048"));
    g_nmaxe.board.hostname                      = String(nvs_config_get_string(NVS_CONFIG_HOSTNAME, g_nmaxe.connection.wifi.softap_param.ssid.c_str()));
    g_nmaxe.connection.stratum_update           = millis();
    g_nmaxe.ota.ota_running                     = false;
    g_nmaxe.ota.progress                        = 0;
    g_nmaxe.ota.firmware                        = "";
    g_nmaxe.mstatus.diff.best_ever              = strtoull(nvs_config_get_string(NVS_CONFIG_BEST_EVER, "0"), NULL, 10);
    g_nmaxe.preference.fan.is_auto_speed        = nvs_config_get_u16(NVS_CONFIG_AUTO_FAN_SPEED, true);
    g_nmaxe.preference.fan.invert_ploarity      = nvs_config_get_u16(NVS_CONFIG_INVERT_FAN_POLARITY, true); 
    g_nmaxe.preference.fan.speed                = nvs_config_get_u16(NVS_CONFIG_FAN_SPEED, 100);
    g_nmaxe.preference.fan.self_test            = false;
    g_nmaxe.preference.screen.flip              = nvs_config_get_u8(NVS_CONFIG_FLIP_SCREEN, true);
    g_nmaxe.preference.screen.auto_screen       = nvs_config_get_u8(NVS_CONFIG_AUTO_SCREEN, false);
    g_nmaxe.preference.screen.brightness        = nvs_config_get_u8(NVS_CONFIG_SCREEN_BRIGHTNESS, 90);
    g_nmaxe.preference.screen.brightness_last   = g_nmaxe.preference.screen.brightness;
    g_nmaxe.preference.led.enable               = nvs_config_get_u8(NVS_CONFIG_LED_INDICATOR, true);
    g_nmaxe.preference.led.sleep                = false;
    g_nmaxe.preference.led.sleep_last           = g_nmaxe.preference.led.sleep;
    g_nmaxe.mstatus.uptime_ever                 = nvs_config_get_u64(NVS_CONFIG_UPTIME, 0);
    g_nmaxe.mstatus.timezone                    = String(nvs_config_get_string(NVS_CONFIG_TIMEZONE, "8.0"));
    g_nmaxe.coin                                = String(nvs_config_get_string(NVS_CONFIG_MINING_COIN, "BTC"));
    g_nmaxe.coin.toUpperCase();
    g_nmaxe.miner                               = NULL;

    void* market_buf                            = psramAllocator(sizeof(MarketClass));
    void* stratum_buf                           = psramAllocator(sizeof(StratumClass));
    void* power_buf                             = psramAllocator(sizeof(NMAxePowerClass));
    if(!market_buf || !stratum_buf || !power_buf){
        LOG_E("Failed to allocate memory in PSRAM for market, stratum or power class");
        return false;
    }
    g_nmaxe.market                              = new(market_buf)  MarketClass();
    g_nmaxe.stratum                             = new(stratum_buf) StratumClass(g_nmaxe.connection.pool_use, g_nmaxe.connection.stratum_use, 10);
    g_nmaxe.power                               = new(power_buf)   NMAxePowerClass({NM_AXE_POWER_BM13xx_VPLL_ENABLE_PIN, NM_AXE_POWER_BM13xx_VDD_ENABLE_PIN, NM_AXE_POWER_BM13xx_VCORE_ENABLE_PIN},
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