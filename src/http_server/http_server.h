#ifndef AXE_HTTP_SERVER_H_
#define AXE_HTTP_SERVER_H_
#include <ESPAsyncWebServer.h>

void file_system_init();
void get_system_info(AsyncWebServerRequest *request);
void get_hr_distribution(AsyncWebServerRequest *request);
void get_status_history(AsyncWebServerRequest *request);
void get_status_realtime(AsyncWebServerRequest *request);
void get_lucky_history(AsyncWebServerRequest *request);
void echo_handler(AsyncWebServerRequest* request);
void post_restart(AsyncWebServerRequest * request);
void get_theme_handler(AsyncWebServerRequest * request);
void options_theme_handler(AsyncWebServerRequest * request);
void get_swarm_info_handler(AsyncWebServerRequest * request);
void patch_update_settings_handler(AsyncWebServerRequest * request, uint8_t *data, size_t len, size_t index, size_t total);
void file_upload_handler(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final);
void post_theme_handler(AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total);
void rest_common_get_handler(AsyncWebServerRequest *request);
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);

extern WebSocketsServer webSocket;
extern AsyncWebServer   webServer;
#endif