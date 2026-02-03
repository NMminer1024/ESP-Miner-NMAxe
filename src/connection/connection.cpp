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
            g_board.info.connection.wifi.status.ip = WiFi.localIP();
            g_board.info.connection.wifi.status.gateway = WiFi.gatewayIP();
            g_board.info.connection.wifi.status.subnet = WiFi.subnetMask();
            g_board.info.connection.wifi.status.dns = WiFi.dnsIP();
            g_board.info.connection.wifi.status.status = WL_CONNECTED;
            retry_cnt = 0;
            LOG_I("Got IP : %s", WiFi.localIP().toString().c_str());
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            g_board.info.connection.wifi.status.ip = IPAddress(0, 0, 0, 0);
            g_board.info.connection.wifi.status.gateway = IPAddress(0, 0, 0, 0);
            g_board.info.connection.wifi.status.subnet = IPAddress(0, 0, 0, 0);
            g_board.info.connection.wifi.status.dns = IPAddress(0, 0, 0, 0);
            g_board.info.connection.wifi.status.status = WL_DISCONNECTED;
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

void webserver_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;
    String taskName = "(webserver)";
    LOG_I("%s thread started on core %d...", taskName, xPortGetCoreID());
    LOG_I("Initializing webserver...");

    file_system_init();
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    webServer.on("/api/system/info", HTTP_GET, get_system_info);
    webServer.on("/api/system/hr/dist", HTTP_GET, get_hr_distribution);
    webServer.on("/api/system/gauge/limits", HTTP_GET, get_gauge_limits);
    webServer.on("/api/system/status/history", HTTP_GET, get_status_history);
    webServer.on("/api/system/status/realtime", HTTP_GET, get_status_realtime);
    webServer.on("/api/system/luck/history", HTTP_GET, get_lucky_history);
    // webServer.on("/api/system/luck/realtime", HTTP_GET, get_lucky_realtime);

    webServer.on("/api/ws", HTTP_GET, echo_handler);
    webServer.on("/api/system/restart", HTTP_POST, post_restart);
    webServer.on("/api/system/OTA", HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler);
    webServer.on("/api/system/OTAWWW", HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler);
    webServer.on("/api/system", HTTP_PATCH, [](AsyncWebServerRequest *request){}, NULL, patch_update_settings_handler);
    webServer.on("/api/theme", HTTP_GET, get_theme_handler);
    webServer.on("/api/theme", HTTP_OPTIONS, options_theme_handler);
    webServer.on("/api/theme", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, post_theme_handler);
    webServer.on("/api/swarm", HTTP_GET, get_swarm_info_handler);
    webServer.on("/*", HTTP_GET, rest_common_get_handler);
    webServer.on("/*", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(200);
        response->addHeader("Access-Control-Allow-Origin", "*");
        response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE");
        response->addHeader("Access-Control-Allow-Headers", "Accept,Content-Type");
        request->send(response);
    });
    webServer.begin();
    while (true){
        if(g_board.info.connection.wifi.status.status == WL_CONNECTED){
            webSocket.loop();
        }
        delay(250);
    }
}