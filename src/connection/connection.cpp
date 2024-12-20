#include "logger.h"
#include "connection.h"
#include "global.h"
#include "axe_http_server.h"  
#include "axe_nvs_config.h"

#define CONFIG_TIMEOUT 60*5

static void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    const char* reason = NULL;
    static uint8_t retry_cnt = 0, max_retries = 120;
    wifi_event_sta_disconnected_t disconnected;
    switch (event) {
        case SYSTEM_EVENT_STA_CONNECTED:
            LOG_I("WiFi connected to [%s], waiting for IP...", WiFi.SSID().c_str());
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            g_nmaxe.connection.wifi.status_param.ip = WiFi.localIP();
            g_nmaxe.connection.wifi.status_param.gateway = WiFi.gatewayIP();
            g_nmaxe.connection.wifi.status_param.subnet = WiFi.subnetMask();
            g_nmaxe.connection.wifi.status_param.dns = WiFi.dnsIP();
            g_nmaxe.connection.wifi.status_param.status = WL_CONNECTED;
            retry_cnt = 0;
            LOG_I("Got IP : %s", WiFi.localIP().toString().c_str());
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            g_nmaxe.connection.wifi.status_param.ip = IPAddress(0, 0, 0, 0);
            g_nmaxe.connection.wifi.status_param.gateway = IPAddress(0, 0, 0, 0);
            g_nmaxe.connection.wifi.status_param.subnet = IPAddress(0, 0, 0, 0);
            g_nmaxe.connection.wifi.status_param.dns = IPAddress(0, 0, 0, 0);
            g_nmaxe.connection.wifi.status_param.status = WL_DISCONNECTED;
            disconnected = info.wifi_sta_disconnected;
            reason = WiFi.disconnectReasonName((wifi_err_reason_t)disconnected.reason);
            retry_cnt++;
            LOG_W("WiFi disconnected, reason: %s", reason);
            break;
        default:
            break;
    }
    if(retry_cnt >= max_retries){
        LOG_W("WiFi connection retry limit reached, rebooting...");
        delay(1000);
        ESP.restart();
    }
}

static void config_timeout_monitor_thread_entry(void *args){
    char *name = (char*)malloc(20);
    strcpy(name, (char*)args);
    LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
    free(name);
    
    uint16_t timeout = 0;
    g_nmaxe.connection.wifi.status_param.config_timeout = CONFIG_TIMEOUT;
    while(true){
        if (WL_CONNECTED == g_nmaxe.connection.wifi.status_param.status) {
            break;
        }
        if(timeout++ >= CONFIG_TIMEOUT){
            LOG_W("WiFi configuration timeout, rebooting...");
            delay(1000);
            ESP.restart();
        }
        g_nmaxe.connection.wifi.status_param.config_timeout = CONFIG_TIMEOUT - timeout;
        delay(1000);
    }
    LOG_I("WiFi configuration monitor exit...");
    vTaskDelete(NULL);
}

void axe_wifi_connecet(axe_wifi_conn_param_t param){
    LOG_I("Initializing WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_15dBm);
    WiFi.onEvent(WiFiEvent);

    LOG_I("Try to connect [%s]...", param.ssid.c_str());
    WiFi.setHostname(g_nmaxe.connection.wifi.conn_param.hostname.c_str());
    WiFi.begin(param.ssid.c_str(), param.pwd.c_str());
    //start http server
    start_http_server();
    //force config
    if(g_nmaxe.force_config){
        nvs_config_set_u8(NVS_CONFIG_FORCE_CONFIG, false);
        LOG_I("Set softAP [%s]...", g_nmaxe.connection.wifi.conn_param.hostname.c_str());
        WiFi.mode(WIFI_AP);
        WiFi.softAP(g_nmaxe.connection.wifi.softap_param.ssid, g_nmaxe.connection.wifi.softap_param.pwd.c_str());
        WiFi.softAPConfig(g_nmaxe.connection.wifi.softap_param.ip, g_nmaxe.connection.wifi.softap_param.ip, IPAddress(255, 255, 255, 0));
        delay(1000);
        xSemaphoreGive(g_nmaxe.connection.wifi.force_cfg_xsem);
        //config time out monitor
        String taskName = "(config_monitor)";
        xTaskCreatePinnedToCore(config_timeout_monitor_thread_entry, taskName.c_str(), 1024*4, (void*)taskName.c_str(), TASK_PRIORITY_CONFIG_MONITOR, NULL, 1);
        while(true){
            delay(1000);
            LOG_W("Force configuration, timeout: %ds...", g_nmaxe.connection.wifi.status_param.config_timeout);
        }
    }
    //wait for connection
    int maxRetries = 0;
    while (WiFi.status() != WL_CONNECTED && maxRetries < 60*5) {
        maxRetries++;
        LOG_I("Try to connect [%s] %ds...", param.ssid.c_str(), maxRetries);
        if(maxRetries >= 15){
            LOG_I("Set softAP [%s]...", g_nmaxe.connection.wifi.conn_param.hostname.c_str());
            WiFi.mode(WIFI_AP);
            WiFi.softAP(g_nmaxe.connection.wifi.softap_param.ssid, g_nmaxe.connection.wifi.softap_param.pwd.c_str());
            WiFi.softAPConfig(g_nmaxe.connection.wifi.softap_param.ip, g_nmaxe.connection.wifi.softap_param.ip, IPAddress(255, 255, 255, 0));
            delay(1000);
            xSemaphoreGive(g_nmaxe.connection.wifi.force_cfg_xsem);

            //config time out monitor
            String taskName = "(config_monitor)";
            xTaskCreatePinnedToCore(config_timeout_monitor_thread_entry, taskName.c_str(), 1024*4, (void*)taskName.c_str(), TASK_PRIORITY_CONFIG_MONITOR, NULL, 1);
            
            while (true){
                if (WiFi.softAPgetStationNum() > 0) {
                    LOG_I("Connected: %d client.", WiFi.softAPgetStationNum());
                }
                delay(1000);
            }
        }
        delay(1000);
    }
    LOG_I("------------------------------------");
    LOG_I("SSID     : %s ", WiFi.SSID().c_str());
    LOG_I("IP       : %s ", WiFi.localIP().toString().c_str());
    LOG_I("RSSI     : %d dBm", WiFi.RSSI());
    LOG_I("Channel  : %d", WiFi.channel());
    LOG_I("DNS      : %s, %s", WiFi.dnsIP(0).toString().c_str(), WiFi.dnsIP(1).toString().c_str());
    LOG_I("Gateway  : %s", WiFi.gatewayIP().toString().c_str());
    LOG_I("------------------------------------");
}





