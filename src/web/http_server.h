#ifndef AXE_HTTP_SERVER_H_
#define AXE_HTTP_SERVER_H_
#include <ESPAsyncWebServer.h>


bool file_system_init();

// ── Dashboard status ─────────────────────────────────────────────────────────
void get_system_info(AsyncWebServerRequest *request);

// ── System utilities ─────────────────────────────────────────────────────────
void get_hr_distribution(AsyncWebServerRequest *request);
void get_gauge_limits(AsyncWebServerRequest* request);
void get_status_history(AsyncWebServerRequest *request);
void get_status_realtime(AsyncWebServerRequest *request);
void get_lucky_history(AsyncWebServerRequest *request);
void echo_handler(AsyncWebServerRequest* request);
void post_restart(AsyncWebServerRequest * request);
void get_theme_handler(AsyncWebServerRequest * request);
void options_theme_handler(AsyncWebServerRequest * request);
void get_swarm_info_handler(AsyncWebServerRequest * request);
void post_reset_block_hits(AsyncWebServerRequest * request);
void get_ota_progress(AsyncWebServerRequest* request);
// ── Reboot history (planned vs crash, persisted across boots) ───────────────
void get_reboot_last(AsyncWebServerRequest * request);
void get_reboot_list(AsyncWebServerRequest * request);
void delete_reboot_list(AsyncWebServerRequest * request);
// ── Coredump (post-mortem of the most recent crash, ELF in flash) ───────────
void get_coredump_info(AsyncWebServerRequest * request);
void delete_coredump(AsyncWebServerRequest * request);
void file_upload_handler(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final);
void post_theme_handler(AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total);
void rest_common_get_handler(AsyncWebServerRequest *request);

// ── Settings – per-card GET / PATCH ─────────────────────────────────────────
void get_setting_network(AsyncWebServerRequest *request);
void patch_setting_network(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

void get_setting_time(AsyncWebServerRequest *request);
void patch_setting_time(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

void get_setting_mining(AsyncWebServerRequest *request);
void patch_setting_mining(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

void get_setting_market(AsyncWebServerRequest *request);
void patch_setting_market(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

void get_setting_preference(AsyncWebServerRequest *request);
void patch_setting_preference(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

// AsyncWebSocket event handler (part of ESPAsyncWebServer, no extra library needed)
void webSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

extern AsyncWebSocket   webSocket;
extern AsyncWebServer   webServer;
#endif