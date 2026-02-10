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
            g_board.status.wifi.ip = WiFi.localIP();
            g_board.status.wifi.gateway = WiFi.gatewayIP();
            g_board.status.wifi.subnet = WiFi.subnetMask();
            g_board.status.wifi.dns = WiFi.dnsIP();
            g_board.status.wifi.status = WL_CONNECTED;
            retry_cnt = 0;
            LOG_I("Got IP : %s", WiFi.localIP().toString().c_str());
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            g_board.status.wifi.ip = IPAddress(0, 0, 0, 0);
            g_board.status.wifi.gateway = IPAddress(0, 0, 0, 0);
            g_board.status.wifi.subnet = IPAddress(0, 0, 0, 0);
            g_board.status.wifi.dns = IPAddress(0, 0, 0, 0);
            g_board.status.wifi.status = WL_DISCONNECTED;
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
