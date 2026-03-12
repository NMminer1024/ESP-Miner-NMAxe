#ifndef AXE_HTTP_SERVER_H_
#define AXE_HTTP_SERVER_H_
#include <ESPAsyncWebServer.h>

#define HTTP_API_SYS_JSON_KEY_MINER_POWER                   "power"
#define HTTP_API_SYS_JSON_KEY_MINER_VOLT                    "voltage"
#define HTTP_API_SYS_JSON_KEY_MINER_CURRENT                 "current"
#define HTTP_API_SYS_JSON_KEY_MCU_TEMP                      "mcuTemp"
#define HTTP_API_SYS_JSON_KEY_VCORE_TEMP                    "vcoreTemp"
#define HTTP_API_SYS_JSON_KEY_ASIC_TEMP                     "asicTemp"
#define HTTP_API_SYS_JSON_KEY_ASIC_CNT                      "asicCount"
#define HTTP_API_SYS_JSON_KEY_ASIC_VCORE_REQ                "vcoreReq"
#define HTTP_API_SYS_JSON_KEY_ASIC_VCORE_ACTUAL             "vcoreActual"
#define HTTP_API_SYS_JSON_KEY_ASIC_FREQ_REQ                 "freqReq"
#define HTTP_API_SYS_JSON_KEY_ASIC_SMALL_CORE_CNT           "smallCoreCnt"
#define HTTP_API_SYS_JSON_KEY_ASIC_MODEL_NAME               "asic"

#define HTTP_API_SYS_JSON_KEY_FAN_CNT                       "fanCount"

#define HTTP_API_SYS_JSON_KEY_MINER_HR_REALTIME             "hashRate"
#define HTTP_API_SYS_JSON_KEY_MINER_BEST_DIFF_EVER          "bestDiffEver"
#define HTTP_API_SYS_JSON_KEY_MINER_BEST_DIFF_SESSION       "bestDiffSession"
#define HTTP_API_SYS_JSON_KEY_MINER_FREE_HEAP               "freeHeap"
#define HTTP_API_SYS_JSON_KEY_MINER_SHARES_ACCEPTED         "sharesAccepted"
#define HTTP_API_SYS_JSON_KEY_MINER_SHARES_REJECTED         "sharesRejected"
#define HTTP_API_SYS_JSON_KEY_MINER_UPTIME_SECONDS          "uptimeSeconds"

#define HTTP_API_SYS_JSON_KEY_BOARD_FW_VERSION              "fwVersion"
#define HTTP_API_SYS_JSON_KEY_BOARD_HW_MODEL                "hwModel"
#define HTTP_API_SYS_JSON_KEY_BOARD_HOSTNAME                "hostName"
#define HTTP_API_SYS_JSON_KEY_BOARD_TIMEZONE                "timeZone"
#define HTTP_API_SYS_JSON_KEY_BOARD_TIME_FORMAT             "timeFormat"
#define HTTP_API_SYS_JSON_KEY_BOARD_DATE_FORMAT             "dateFormat"
#define HTTP_API_SYS_JSON_KEY_BOARD_WIFI_SSID               "wifiSSID"
#define HTTP_API_SYS_JSON_KEY_BOARD_WIFI_STATUS             "wifiStatus"

#define HTTP_API_SYS_JSON_KEY_COIN_PRICE_DISPLAY             "coinPriceDisplay"
#define HTTP_API_SYS_JSON_KEY_COIN_WATCHLIST                 "coinWatchlist"

#define HTTP_API_SYS_JSON_KEY_PERFORMANCE_SCREEN_FLIP               "screenFlip"
#define HTTP_API_SYS_JSON_KEY_PERFORMANCE_SCREEN_AUTO_ROLL          "screenAutoRoll"
#define HTTP_API_SYS_JSON_KEY_PERFORMANCE_SCREEN_BRIGHTNESS         "Brightness"
#define HTTP_API_SYS_JSON_KEY_PERFORMANCE_ASIC_TARGET_TEMP          "asicTargetTemp"
#define HTTP_API_SYS_JSON_KEY_PERFORMANCE_FAN_AUTO_SPEED            "fanAutoSpeed"
#define HTTP_API_SYS_JSON_KEY_PERFORMANCE_LED_INDICATOR             "ledIndicator"

void file_system_init();
void get_system_info(AsyncWebServerRequest *request);
void get_hr_distribution(AsyncWebServerRequest *request);
void get_gauge_limits(AsyncWebServerRequest* request);
void get_oc_vc_list(AsyncWebServerRequest* request);
void get_status_history(AsyncWebServerRequest *request);
void get_status_realtime(AsyncWebServerRequest *request);
void get_lucky_history(AsyncWebServerRequest *request);
void echo_handler(AsyncWebServerRequest* request);
void post_restart(AsyncWebServerRequest * request);
void get_theme_handler(AsyncWebServerRequest * request);
void options_theme_handler(AsyncWebServerRequest * request);
void get_swarm_info_handler(AsyncWebServerRequest * request);
void get_market_available_pairs_handler(AsyncWebServerRequest *request);
void patch_update_settings_handler(AsyncWebServerRequest * request, uint8_t *data, size_t len, size_t index, size_t total);
void file_upload_handler(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final);
void post_theme_handler(AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total);
void rest_common_get_handler(AsyncWebServerRequest *request);
// AsyncWebSocket event handler (part of ESPAsyncWebServer, no extra library needed)
void webSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

extern AsyncWebSocket   webSocket;
extern AsyncWebServer   webServer;
#endif