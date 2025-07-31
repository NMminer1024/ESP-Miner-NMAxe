#include <ESPAsyncWebServer.h>
#include <Arduino.h>
#include <Update.h>
#include <SPIFFS.h>
#include "ArduinoJson.h"
#include "logger.h"
#include "global.h"
#include "http_server.h"
#include "nvs_config.h"

static AsyncWebServer  webServer(80);
WebSocketsServer       webSocket(81);

static void file_system_init() {
    if (!SPIFFS.begin(true, "", 5, NULL)) {
        LOG_E("An Error has occurred while mounting SPIFFS");
        return;
    }
    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();
    LOG_I("File system totalBytes: %d, usedBytes: %d", totalBytes, usedBytes);
}
static String get_content_type_from_file(String filepath) {
    if(filepath.endsWith(".html")) return "text/html";
    else if(filepath.endsWith(".css")) return "text/css";
    else if(filepath.endsWith(".js")) return "application/javascript";
    else if(filepath.endsWith(".png")) return "image/png";
    else if(filepath.endsWith(".ico")) return "image/x-icon";
    else return "text/plain";
}
static void rest_common_get_handler(AsyncWebServerRequest *request) {
   String base_path = "";
    if (request->url().endsWith("/")) base_path = "/index.html";
    else base_path = request->url();
    String contentType = get_content_type_from_file(base_path);

    base_path = base_path + ".gz";
    File file = SPIFFS.open(base_path, "r");
    if (!file) {
        request->send(302, "text/plain", "Redirect to the captive portal");
        request->redirect("/");
        LOG_W("Redirecting to root");
        return;
    }

    AsyncWebServerResponse *response = request->beginResponse(contentType, file.size(), [file](uint8_t *buffer, size_t maxLen, size_t total) mutable -> size_t {
        size_t bytesRead = file.read(buffer, maxLen);
        if (bytesRead == 0) {
            file.close();
        }
        return bytesRead;
    });


    if (!request->url().endsWith("/")) {
        response->addHeader("Cache-Control", "max-age=86400"); // cache for 24 hour
    }
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
    LOG_L("File sending complete: %s, free heap: %d", base_path.c_str(), ESP.getFreeHeap());
}
static void get_system_info(AsyncWebServerRequest* request){
    const uint16_t json_size_max  =  1024;

    StaticJsonDocument<json_size_max> root = StaticJsonDocument<json_size_max>();

    root.clear();
    root["power"] = (g_nmaxe.board.ibus /1000.0f) * (g_nmaxe.board.vbus / 1000.0f);
    root["voltage"] = g_nmaxe.board.vbus;
    root["current"] = g_nmaxe.board.ibus;
    root["temp"] = g_nmaxe.temp.asic;
    root["vrTemp"] = g_nmaxe.temp.vcore;
    root["mcuTemp"] = g_nmaxe.temp.mcu;
    root["hashRate"] = g_nmaxe.mstatus.hashrate._3m/1000/1000/1000;
    root["bestDiff"] = formatNumber(g_nmaxe.mstatus.diff.best_ever, 4);
    root["bestSessionDiff"] = formatNumber(g_nmaxe.mstatus.diff.best_session, 4);
    root["freeHeap"] = ESP.getFreeHeap();
    root["coreVoltage"] = g_nmaxe.asic.vcore_req;
    root["coreVoltageActual"] = g_nmaxe.asic.vcore_measured;
    root["frequency"] = g_nmaxe.asic.frequency_req;
    root["hostname"] = g_nmaxe.board.hostname;
    root["timezone"] = g_nmaxe.mstatus.timezone;
    root["ssid"] = g_nmaxe.connection.wifi.conn_param.ssid;
    root["wifiStatus"] = ((g_nmaxe.connection.wifi.status_param.status == WL_CONNECTED) ? "connected" : "disconnected");
    root["sharesAccepted"] = g_nmaxe.mstatus.share_accepted;
    root["sharesRejected"] = g_nmaxe.mstatus.share_rejected;
    root["uptimeSeconds"] = g_nmaxe.mstatus.uptime_session;
    root["asicCount"] = g_nmaxe.miner->get_asic_count();
    root["smallCoreCount"] = g_nmaxe.miner->get_asic_small_cores();
    root["ASICModel"] = g_nmaxe.asic.model;
    root["stratumUserUSED"] = g_nmaxe.connection.stratum_use.user;
    root["stratumURLUSED"] = g_nmaxe.connection.pool_use.ssl ? ("stratum+ssl://" + g_nmaxe.connection.pool_use.url + ":" + String(g_nmaxe.connection.pool_use.port)) : ("stratum+tcp://" + g_nmaxe.connection.pool_use.url + ":" + String(g_nmaxe.connection.pool_use.port));
    root["stratumUser1"] = g_nmaxe.connection.stratum_primary.user;
    root["stratumPassword1"] = g_nmaxe.connection.stratum_primary.pwd;
    root["stratumUser2"]     = g_nmaxe.connection.stratum_fallback.user;
    root["stratumPassword2"] = g_nmaxe.connection.stratum_fallback.pwd;
    root["stratumURL1"] = g_nmaxe.connection.pool_primary.ssl ? ("stratum+ssl://" + g_nmaxe.connection.pool_primary.url + ":" + String(g_nmaxe.connection.pool_primary.port)) : ("stratum+tcp://" + g_nmaxe.connection.pool_primary.url + ":" + String(g_nmaxe.connection.pool_primary.port));
    root["stratumURL2"] = g_nmaxe.connection.pool_fallback.ssl ? ("stratum+ssl://" + g_nmaxe.connection.pool_fallback.url + ":" + String(g_nmaxe.connection.pool_fallback.port)) : ("stratum+tcp://" + g_nmaxe.connection.pool_fallback.url + ":" + String(g_nmaxe.connection.pool_fallback.port));
    root["version"] = g_nmaxe.board.fw_version;
    root["boardVersion"] = g_nmaxe.board.hw_model;
    root["flipscreen"] = g_nmaxe.preference.screen.flip;
    root["ledindicator"] = g_nmaxe.preference.led.enable;
    root["overheat_mode"] = 0;
    root["invertscreen"]  = 1;
    root["invertfanpolarity"] = g_nmaxe.preference.fan.invert_ploarity;
    root["autofanspeed"] = g_nmaxe.preference.fan.is_auto_speed;
    root["autoscreen"]   = g_nmaxe.preference.screen.auto_screen;
    root["fanspeed"] = g_nmaxe.preference.fan.speed;
    root["fanrpm"] = g_nmaxe.preference.fan.rpm;
    root["brightness"] = g_nmaxe.preference.screen.brightness_last;
    root["coin"] = g_nmaxe.coin;
    String json_str;
    serializeJson(root, json_str);
    request->send(200, "application/json", json_str);
}
static void get_hr_distribution(AsyncWebServerRequest* request){
    const uint16_t json_size_max  =  512;
    StaticJsonDocument<json_size_max> root = StaticJsonDocument<json_size_max>();

    root.clear();
    root["max_bars"]    = g_nmaxe.mstatus.hr_dist.max_x_bars;
    root["max_hr"]      = g_nmaxe.mstatus.hr_dist.max_x_hr;
    root["times"]       = g_nmaxe.mstatus.hr_dist.times;
    root["dura"]        = g_nmaxe.mstatus.hr_dist.dura;
    JsonObject dist_map = root.createNestedObject("dist");
    for (const auto& pair : g_nmaxe.mstatus.hr_dist.dist_map) {
        dist_map[String(pair.first)] = pair.second; 
    }

    String json_str;
    serializeJson(root, json_str);
    request->send(200, "application/json", json_str);
}
static void get_swarm_info_handler(AsyncWebServerRequest* request){
    uint16_t json_size_max = 1024 * 40; // in bytes, 40kB about 120 devices

    void* buffer = psramAllocator(json_size_max);
    if (!buffer) {
        request->send(500, "application/json", "{\"error\":\"Failed to allocate memory in PSRAM\"}");
        LOG_E("Failed to allocate memory in PSRAM for swarm info");
        return;
    }
    DynamicJsonDocument* root = new(buffer) DynamicJsonDocument(json_size_max);

    JsonArray devicesArray = root->createNestedArray("devices");
    for (auto it = g_nmaxe.swarm.begin(); it != g_nmaxe.swarm.end(); it++) {
        String ip        = it->first;
        String swarm_str = it->second;

        DynamicJsonDocument deviceDoc(1024);
        DeserializationError error = deserializeJson(deviceDoc, swarm_str);
        if (error) continue;
        
        JsonObject deviceObj = devicesArray.createNestedObject();
        deviceObj["ip"] = ip;
        deviceObj["BoardType"] = deviceDoc["BoardType"].as<std::string>();
        deviceObj["HashRate"] = deviceDoc["HashRate"].as<std::string>();
        deviceObj["Share"] = deviceDoc["Share"].as<std::string>();
        deviceObj["PoolDiff"] = deviceDoc["PoolDiff"].as<std::string>();
        deviceObj["NetDiff"] = deviceDoc["NetDiff"].as<std::string>();
        deviceObj["LastDiff"] = deviceDoc["LastDiff"].as<std::string>();
        deviceObj["BestDiff"] = deviceDoc["BestDiff"].as<std::string>();
        deviceObj["Valid"] = deviceDoc["Valid"].as<int>();
        deviceObj["Power"] = (deviceDoc.containsKey("Power")) ? deviceDoc["Power"].as<std::string>() : "---";
        deviceObj["Temp"] = deviceDoc["Temp"].as<float>();
        deviceObj["RSSI"] = deviceDoc["RSSI"].as<int>();
        deviceObj["FreeHeap"] = deviceDoc["FreeHeap"].as<float>();
        deviceObj["Version"] = deviceDoc["Version"].as<std::string>();
        deviceObj["Uptime"] = deviceDoc["Uptime"].as<std::string>();
        deviceObj["Lastseen"] = deviceDoc["Lastseen"].as<int>() / 1000.0;
    }

    String swarm_info;
    serializeJson(*root, swarm_info);

    // request->send(200, "application/json", swarm_info);

    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", swarm_info);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    request->send(response);

    //free memory
    root->~DynamicJsonDocument();
    psramDeallocator(buffer);
}
static void options_theme_handler(AsyncWebServerRequest* request){
    request->send(200, "application/json", "");
}
static void get_theme_handler(AsyncWebServerRequest* request){
    char *scheme = nvs_config_get_string(NVS_CONFIG_THEME_SCHEME, "dark");
    char *name = nvs_config_get_string(NVS_CONFIG_THEME_NAME, "dark");
    char *colors = nvs_config_get_string(NVS_CONFIG_THEME_COLORS, 
        "{"
        "\"--primary-color\":\"#F80421\","
        "\"--primary-color-text\":\"#ffffff\","
        "\"--highlight-bg\":\"#F80421\","
        "\"--highlight-text-color\":\"#ffffff\","
        "\"--focus-ring\":\"0 0 0 0.2rem rgba(248,4,33,0.2)\","
        "\"--slider-bg\":\"#dee2e6\","
        "\"--slider-range-bg\":\"#F80421\","
        "\"--slider-handle-bg\":\"#F80421\","
        "\"--progressbar-bg\":\"#dee2e6\","
        "\"--progressbar-value-bg\":\"#F80421\","
        "\"--checkbox-border\":\"#F80421\","
        "\"--checkbox-bg\":\"#F80421\","
        "\"--checkbox-hover-bg\":\"#df031d\","
        "\"--button-bg\":\"#F80421\","
        "\"--button-hover-bg\":\"#df031d\","
        "\"--button-focus-shadow\":\"0 0 0 2px #ffffff, 0 0 0 4px #F80421\","
        "\"--togglebutton-bg\":\"#F80421\","
        "\"--togglebutton-border\":\"1px solid #F80421\","
        "\"--togglebutton-hover-bg\":\"#df031d\","
        "\"--togglebutton-hover-border\":\"1px solid #df031d\","
        "\"--togglebutton-text-color\":\"#ffffff\""
        "}"
    );
    
    DynamicJsonDocument root(1024), colors_json(512);
    DeserializationError error = deserializeJson(colors_json, colors);

    root["colorScheme"] = scheme;
    root["theme"] = name;
    if(error.code() == DeserializationError::Ok){
        root["accentColors"] = colors_json;
    }

    String colors_str;
    serializeJson(root, colors_str);

    request->send(200, "application/json", colors_str);

    //free memory
    free(scheme);
    free(name);
    free(colors);
}
static void post_theme_handler(AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total){
    if (!data) {
        request->send(500, "application/json", "{\"error\":\"Failed to allocate memory\"}");
        LOG_E("Failed to allocate memory for theme update request.");
        return;
    }

    DynamicJsonDocument root = DynamicJsonDocument(1024*2);
    DeserializationError error = deserializeJson(root, data);
    if(error){
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }
    if(!root.is<JsonObject>()){
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }

    if(root.containsKey("colorScheme")){
        nvs_config_set_string(NVS_CONFIG_THEME_SCHEME, root["colorScheme"].as<String>().c_str());
    }
    if(root.containsKey("theme")){
        nvs_config_set_string(NVS_CONFIG_THEME_NAME, root["theme"].as<String>().c_str());
    }
    if(root.containsKey("accentColors")){
        String colors_str;
        serializeJson(root["accentColors"], colors_str);
        nvs_config_set_string(NVS_CONFIG_THEME_COLORS, colors_str.c_str());
    }
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}
static void echo_handler(AsyncWebServerRequest* request){
    LOG_I("Echo Request...");
}
static void post_restart(AsyncWebServerRequest * request){
    LOG_I("Restarting System because of API Request");
    // Send HTTP response before restarting
    request->send(200, "text/plain", "System will restart shortly.");
    delay(500);
    xSemaphoreGive(g_nmaxe.board.reboot_xsem);
}
static void patch_update_settings_handler(AsyncWebServerRequest * request, uint8_t *data, size_t len, size_t index, size_t total){
    static uint16_t SCRATCH_BUFSIZE = 512;
    
    LOG_W("Update Settings Request, request contentLength: %d, index: %d, total: %d", request->contentLength(), index, total);
    if (total >= SCRATCH_BUFSIZE) {
        request->send(500, "text/plain", "Content too long");
        LOG_E("request %s too long", request->url().c_str());
        return;
    }
    char *buffer = (char*)malloc(SCRATCH_BUFSIZE);
    memset(buffer, 0, SCRATCH_BUFSIZE);
    if (index + len <= SCRATCH_BUFSIZE) {
        memcpy(buffer + index, data, len);
    }

    if (index + len == total) {
        buffer[total] = '\0'; 
        StaticJsonDocument<1024> root;
        DeserializationError error = deserializeJson(root, buffer);
        if(error){
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
            free(buffer);
            return;
        }
        if(!root.is<JsonObject>()){
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
            free(buffer);
            return;
        }

        //primary pool
        if(root.containsKey("stratumUser1")){
            nvs_config_set_string(NVS_CONFIG_STRATUM_USER_PRIMARY,root["stratumUser1"].as<String>().c_str());
        }
        if(root.containsKey("stratumURL1")){
            nvs_config_set_string(NVS_CONFIG_STRATUM_URL_PRIMARY, root["stratumURL1"].as<String>().c_str());
        }
        if(root.containsKey("stratumPassword1")){
            nvs_config_set_string(NVS_CONFIG_STRATUM_PASS_PRIMARY,root["stratumPassword1"].as<String>().c_str());
        }
        //fallback pool
        if(root.containsKey("stratumUser2")){
            nvs_config_set_string(NVS_CONFIG_STRATUM_USER_FALLBACK,root["stratumUser2"].as<String>().c_str());
        }
        if(root.containsKey("stratumURL2")){
            nvs_config_set_string(NVS_CONFIG_STRATUM_URL_FALLBACK, root["stratumURL2"].as<String>().c_str());
        }
        if(root.containsKey("stratumPassword2")){
            nvs_config_set_string(NVS_CONFIG_STRATUM_PASS_FALLBACK,root["stratumPassword2"].as<String>().c_str());
        }
        if(root.containsKey("timezone")){
            nvs_config_set_string(NVS_CONFIG_TIMEZONE, root["timezone"].as<String>().c_str());
            g_nmaxe.mstatus.timezone = root["timezone"].as<String>();
        }
        if(root.containsKey("ssid")){
            nvs_config_set_string(NVS_CONFIG_WIFI_SSID,root["ssid"].as<String>().c_str());
        }
        if(root.containsKey("wifiPass")){
            nvs_config_set_string(NVS_CONFIG_WIFI_PASS,root["wifiPass"].as<String>().c_str());
        }
        if(root.containsKey("hostname")){
            nvs_config_set_string(NVS_CONFIG_HOSTNAME,root["hostname"].as<String>().c_str());
            nvs_config_set_string(NVS_CONFIG_AP_SSID, root["hostname"].as<String>().c_str());
            g_nmaxe.board.hostname                    = root["hostname"].as<String>();
            g_nmaxe.connection.wifi.softap_param.ssid = root["hostname"].as<String>();
        }
        if(root.containsKey("coreVoltage")){
            uint16_t req_mv = root["coreVoltage"].as<uint16_t>();
            g_nmaxe.asic.vcore_req = req_mv;
            g_nmaxe.power->set_vcore_voltage(req_mv);
            nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, root["coreVoltage"].as<uint16_t>());
        }
        if(root.containsKey("brightness")){
            g_nmaxe.preference.screen.brightness = root["brightness"].as<uint8_t>();
            g_nmaxe.preference.screen.brightness_last = g_nmaxe.preference.screen.brightness;
            nvs_config_set_u8(NVS_CONFIG_SCREEN_BRIGHTNESS, g_nmaxe.preference.screen.brightness);
        }
        if(root.containsKey("frequency")){
            nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, root["frequency"].as<uint16_t>());
        }
        if(root.containsKey("flipscreen")){
            nvs_config_set_u8(NVS_CONFIG_FLIP_SCREEN, root["flipscreen"].as<uint8_t>());
        }
        if(root.containsKey("ledindicator")){
            g_nmaxe.preference.led.enable = root["ledindicator"].as<uint8_t>();
            g_nmaxe.preference.led.sleep  = false;
            nvs_config_set_u8(NVS_CONFIG_LED_INDICATOR, root["ledindicator"].as<uint8_t>());
        }
        if(root.containsKey("overheat_mode")){

        }
        if(root.containsKey("coin")){
            nvs_config_set_string(NVS_CONFIG_MINING_COIN,root["coin"].as<String>().c_str());
            g_nmaxe.coin = root["coin"].as<String>();
        }
        if(root.containsKey("invertscreen")){

        }
        if(root.containsKey("invertfanpolarity")){
            nvs_config_set_u16(NVS_CONFIG_INVERT_FAN_POLARITY, root["invertfanpolarity"].as<uint16_t>());
            g_nmaxe.preference.fan.invert_ploarity = root["invertfanpolarity"].as<uint16_t>();
        }
        if(root.containsKey("autofanspeed")){
            nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, root["autofanspeed"].as<uint16_t>());
            g_nmaxe.preference.fan.is_auto_speed = root["autofanspeed"].as<uint16_t>();
        }
        if(root.containsKey("autoscreen")){
            nvs_config_set_u8(NVS_CONFIG_AUTO_SCREEN, root["autoscreen"].as<uint8_t>());
            g_nmaxe.preference.screen.auto_screen = root["autoscreen"].as<uint8_t>();
        }
        if(root.containsKey("fanspeed")){
            nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, root["fanspeed"].as<uint16_t>());
            g_nmaxe.preference.fan.speed = root["fanspeed"].as<uint16_t>();
        }

        for (JsonPair kv : root.as<JsonObject>()) {
            String key = kv.key().c_str();
            String value = kv.value().as<String>();
            LOG_I("Key: %s, Value: %s", key.c_str(), value.c_str());
        }
        request->send(200, "application/json", "{\"status\":\"success\"}");
    }
    free(buffer);
}
static void file_upload_handler(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
    uint64_t flen = request->contentLength();
    if (index == 0) {
        LOG_I("OTA Update Started, File name: %s, Index: %d, contentLength: %d, len: %d, Final: %d", filename.c_str(), index, flen, len, final);
        size_t bin_size = UPDATE_SIZE_UNKNOWN;
        int update_type = U_FLASH; // Default to firmware update

        if(filename == "firmware.bin"){
            bin_size = UPDATE_SIZE_UNKNOWN;
            update_type = U_FLASH;
        }
        if(filename == "spiffs.bin"){
            bin_size = SPIFFS.totalBytes();
            update_type = U_SPIFFS;
        } 

        if (!Update.begin(bin_size, update_type)) { //start with max available size for firmware
            Update.printError(Serial);
            request->send(500, "text/plain", "OTA Update Failed. Not enough space.");
            return;
        }else{
            g_nmaxe.ota.ota_running = true;
            g_nmaxe.ota.progress    = 0;
            g_nmaxe.ota.firmware    = filename;
        }
    }

    if (Update.write(data, len) != len) {
        Update.printError(Serial);
        request->send(500, "text/plain", "OTA Update Failed. Write error.");
        return;
    }
    else{
        static int lastPercentage = -1;
        g_nmaxe.ota.progress = (int)((index + len) * 100.0 / flen);
        if (g_nmaxe.ota.progress != lastPercentage) {
            LOG_I("OTA Update: %d%%", g_nmaxe.ota.progress);
            lastPercentage = g_nmaxe.ota.progress;
        }
    }
    delay(1);//yield to avoid WDT and UI thread freeze 
    if (final) {
        if (Update.end(true)) {
            g_nmaxe.ota.ota_running = false;
            g_nmaxe.ota.progress = 100;
            // request->send(200);
            AsyncWebServerResponse *response = request->beginResponse(200);
            response->addHeader("Access-Control-Allow-Origin", "*");
            request->send(response);



            LOG_I("Update Success: %u bytes, rebooting...", index + len);
            delay(1000);
            xSemaphoreGive(g_nmaxe.board.reboot_xsem);
        } else {
            Update.printError(Serial);
            request->send(500, "text/plain", "OTA Update Failed. End error.");
        }
    }
}
static void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            LOG_W("[%u] webSocket disconnected!", num);
            break;
        case WStype_CONNECTED: {
            IPAddress ip = webSocket.remoteIP(num);
            LOG_W("[%u] webSocket connected from %s", num, ip.toString().c_str());
            break;
        }
        case WStype_TEXT:
            LOG_W("[%u] webSocket get Text: %s", num, payload);
            webSocket.sendTXT(num, payload, length);
            break;
        case WStype_BIN:
            LOG_W("[%u] webSocket get binary length: %u", num, length);
            break;
        case WStype_PING:
            LOG_W("[%u] webSocket get ping", num);
            break;
        case WStype_PONG:
            LOG_W("[%u] webSocket get pong", num);
            break;
        case WStype_ERROR:
            LOG_W("[%u] webSocket get error", num);
            break;
    }
}
static void websocket_loop(void *args){
    char *name = (char*)malloc(20);
    strcpy(name, (char*)args);
    delay(100);
    LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
    free(name);

    while (true){
        if(g_nmaxe.connection.wifi.status_param.status == WL_CONNECTED){
            webSocket.loop();
        }
        delay(250);
        
        // static uint32_t cnt = 0;
        // if(cnt++ % 20 == 0){
        //     UBaseType_t highWaterMark = uxTaskGetStackHighWaterMark(NULL);
        //     LOG_D("%s free stack: %d", name, highWaterMark);
        // }
    }
}

void start_http_server(void) {
    file_system_init();
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    String name = "(websocket)";
    xTaskCreatePinnedToCore(websocket_loop, name.c_str(), 1024*5, (void*)name.c_str(), TASK_PRIORITY_WS, NULL, 1);

    webServer.on("/api/system/info", HTTP_GET, get_system_info);
    webServer.on("/api/system/hr/dist", HTTP_GET, get_hr_distribution);
    // webServer.on("/api/system/hr/history", HTTP_GET, NULL);
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
}


