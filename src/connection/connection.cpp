#include "logger.h"
#include "connection.h"
#include "global.h"
#include "http_server.h"  
#include "nvs_config.h"

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    const char* reason = NULL;
    static uint8_t retry_cnt = 0, max_retries = 120;
    wifi_event_sta_disconnected_t disconnected;
    switch (event) {
        case SYSTEM_EVENT_STA_CONNECTED:
            LOG_I("WiFi connected to [%s], waiting for IP...", WiFi.SSID().c_str());
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            g_board.info.connection.wifi.status_param.ip = WiFi.localIP();
            g_board.info.connection.wifi.status_param.gateway = WiFi.gatewayIP();
            g_board.info.connection.wifi.status_param.subnet = WiFi.subnetMask();
            g_board.info.connection.wifi.status_param.dns = WiFi.dnsIP();
            g_board.info.connection.wifi.status_param.status = WL_CONNECTED;
            retry_cnt = 0;
            LOG_I("Got IP : %s", WiFi.localIP().toString().c_str());
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            g_board.info.connection.wifi.status_param.ip = IPAddress(0, 0, 0, 0);
            g_board.info.connection.wifi.status_param.gateway = IPAddress(0, 0, 0, 0);
            g_board.info.connection.wifi.status_param.subnet = IPAddress(0, 0, 0, 0);
            g_board.info.connection.wifi.status_param.dns = IPAddress(0, 0, 0, 0);
            g_board.info.connection.wifi.status_param.status = WL_DISCONNECTED;
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
