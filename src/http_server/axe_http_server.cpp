#include <ESPAsyncWebServer.h>
#include <Arduino.h>
#include <Update.h>
#include <SPIFFS.h>
#include "ArduinoJson.h"
#include "logger.h"
#include "global.h"
#include "axe_http_server.h"
#include "axe_nvs_config.h"

static AsyncWebServer  webServer(80);
WebSocketsServer       webSocket(81);

void file_system_init() {
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
        response->addHeader("Cache-Control", "max-age=86400"); // cache for 1 day
    }
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
    LOG_L("File sending complete: %s, free heap: %d", base_path.c_str(), ESP.getFreeHeap());
}
static void get_system_info(AsyncWebServerRequest* request){
    const uint16_t json_size_max  =  1024;
    const uint16_t small_core_count = 894;
    static StaticJsonDocument<json_size_max> root;
    root.clear();
    root["power"] = (g_nmaxe.board.ibus /1000.0f) * (g_nmaxe.board.vbus / 1000.0f);
    root["voltage"] = g_nmaxe.board.vbus;
    root["current"] = g_nmaxe.board.ibus;
    root["temp"] = g_nmaxe.asic.temp;
    root["vrTemp"] = g_nmaxe.board.temp_vcore;
    root["mcuTemp"] = g_nmaxe.board.temp_mcu;
    root["hashRate"] = g_nmaxe.mstatus.hashrate/1000/1000/1000;
    root["bestDiff"] = formatNumber(g_nmaxe.mstatus.best_ever, 4);
    root["bestSessionDiff"] = formatNumber(g_nmaxe.mstatus.best_session, 4);
    root["freeHeap"] = ESP.getFreeHeap();
    root["coreVoltage"] = g_nmaxe.asic.vcore_req;
    root["coreVoltageActual"] = g_nmaxe.asic.vcore_measured;
    root["frequency"] = g_nmaxe.asic.frequency_req;
    root["hostname"] = g_nmaxe.connection.wifi.conn_param.hostname;
    root["ssid"] = g_nmaxe.connection.wifi.conn_param.ssid;
    root["wifiStatus"] = ((g_nmaxe.connection.wifi.status_param.status == WL_CONNECTED) ? "connected" : "disconnected");
    root["sharesAccepted"] = g_nmaxe.mstatus.share_accepted;
    root["sharesRejected"] = g_nmaxe.mstatus.share_rejected;
    root["uptimeSeconds"] = g_nmaxe.mstatus.uptime;
    root["asicCount"] = g_nmaxe.miner->get_asic_count();
    root["smallCoreCount"] = small_core_count;
    root["ASICModel"] = g_nmaxe.asic.type;
    root["stratumURL"] = g_nmaxe.connection.pool.ssl ? ("stratum+ssl://" + g_nmaxe.connection.pool.url) : ("stratum+tcp://" + g_nmaxe.connection.pool.url);
    root["stratumPort"] = g_nmaxe.connection.pool.port;
    root["stratumUser"] = g_nmaxe.connection.stratum.user;
    root["version"] = g_nmaxe.board.fw_version;
    root["boardVersion"] = g_nmaxe.board.hw_version;
    root["runningPartition"] = "part1";
    root["flipscreen"] = g_nmaxe.screen.flip;
    root["ledindicator"] = g_nmaxe.led.indicator;
    root["overheat_mode"] = 0;
    root["invertscreen"]  = 1;
    root["invertfanpolarity"] = g_nmaxe.fan.invert_ploarity;
    root["autofanspeed"] = g_nmaxe.fan.is_auto_speed;
    root["fanspeed"] = g_nmaxe.fan.speed;
    root["fanrpm"] = g_nmaxe.fan.rpm;
    root["brightness"] = g_nmaxe.screen.brightness;

    String sys_info;
    serializeJson(root, sys_info);

    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", sys_info);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    request->send(response);
}
static void echo_handler(AsyncWebServerRequest* request){
    LOG_I("Echo Request...");
}
static void post_restart(AsyncWebServerRequest * request){
    LOG_I("Restarting System because of API Request");
    // Send HTTP response before restarting
    const char* resp_str = "System will restart shortly.";
    request->send(200, "text/plain", resp_str);

    // Delay to ensure the response is sent
    delay(1000);
    // Restart the system
    ESP.restart();
    delay(1000);
}
static void patch_update_settings(AsyncWebServerRequest * request, uint8_t *data, size_t len, size_t index, size_t total){
    static uint16_t SCRATCH_BUFSIZE = 512;
    LOG_W("Update Settings Request, Index: %d, Total: %d", index, total);
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

        if(root.containsKey("stratumURL")){
            nvs_config_set_string(NVS_CONFIG_STRATUM_URL, root["stratumURL"].as<String>().c_str());
        }
        if(root.containsKey("stratumUser")){
            nvs_config_set_string(NVS_CONFIG_STRATUM_USER,root["stratumUser"].as<String>().c_str());
        }
        if(root.containsKey("stratumPassword")){
            nvs_config_set_string(NVS_CONFIG_STRATUM_PASS,root["stratumPassword"].as<String>().c_str());
        }
        if(root.containsKey("stratumPort")){
            nvs_config_set_u16(NVS_CONFIG_STRATUM_PORT, root["stratumPort"].as<uint16_t>());
        }
        if(root.containsKey("ssid")){
            nvs_config_set_string(NVS_CONFIG_WIFI_SSID,root["ssid"].as<String>().c_str());
        }
        if(root.containsKey("wifiPass")){
            nvs_config_set_string(NVS_CONFIG_WIFI_PASS,root["wifiPass"].as<String>().c_str());
        }
        if(root.containsKey("hostname")){
            nvs_config_set_string(NVS_CONFIG_HOSTNAME,root["hostname"].as<String>().c_str());
        }
        if(root.containsKey("coreVoltage")){
            uint16_t req_mv = root["coreVoltage"].as<uint16_t>();
            g_nmaxe.asic.vcore_req = req_mv;
            g_nmaxe.power.set_vcore_voltage(req_mv);
            nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, root["coreVoltage"].as<uint16_t>());
        }
        if(root.containsKey("brightness")){
            g_nmaxe.screen.brightness = root["brightness"].as<uint8_t>();
            nvs_config_set_u8(NVS_CONFIG_SCREEN_BRIGHTNESS, g_nmaxe.screen.brightness);
        }
        if(root.containsKey("frequency")){
            nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, root["frequency"].as<uint16_t>());
        }
        if(root.containsKey("flipscreen")){
            nvs_config_set_u8(NVS_CONFIG_FLIP_SCREEN, root["flipscreen"].as<uint8_t>());
        }
        if(root.containsKey("ledindicator")){
            nvs_config_set_u8(NVS_CONFIG_LED_INDICATOR, root["ledindicator"].as<uint8_t>());
        }
        if(root.containsKey("overheat_mode")){

        }
        if(root.containsKey("invertscreen")){

        }
        if(root.containsKey("invertfanpolarity")){
            nvs_config_set_u16(NVS_CONFIG_INVERT_FAN_POLARITY, root["invertfanpolarity"].as<uint16_t>());
            g_nmaxe.fan.invert_ploarity = root["invertfanpolarity"].as<uint16_t>();
        }
        if(root.containsKey("autofanspeed")){
            nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, root["autofanspeed"].as<uint16_t>());
            g_nmaxe.fan.is_auto_speed = root["autofanspeed"].as<uint16_t>();
        }
        if(root.containsKey("fanspeed")){
            nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, root["fanspeed"].as<uint16_t>());
            g_nmaxe.fan.speed = root["fanspeed"].as<uint16_t>();
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
static void handleFileUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
    uint64_t flen = request->contentLength();
    if (index == 0) {
        LOG_I("OTA Update Started, File name: %s, Index: %d, Length: %d, Final: %d", filename.c_str(), index, flen, final);
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
            request->send(200, "text/plain", "OTA Update Successful. Rebooting...");
            LOG_I("Update Success: %u bytes, rebooting...", index + len);
            g_nmaxe.ota.ota_running = false;
            g_nmaxe.ota.progress = 100;
            delay(1000);
            ESP.restart();
        } else {
            Update.printError(Serial);
            request->send(500, "text/plain", "OTA Update Failed. End error.");
        }
    }
}
static void post_ota_update(AsyncWebServerRequest *request) {
    LOG_W("OTA Update Request, content length: %d", request->contentLength());
}
static void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            LOG_W("[%u] webSocket disconnected!\n", num);
            break;
        case WStype_CONNECTED: {
            IPAddress ip = webSocket.remoteIP(num);
            LOG_W("[%u] webSocket connected from %s\n", num, ip.toString().c_str());
            break;
        }
        case WStype_TEXT:
            LOG_W("[%u] webSocket get Text: %s\n", num, payload);
            webSocket.sendTXT(num, payload, length);
            break;
        case WStype_BIN:
            LOG_W("[%u] webSocket get binary length: %u\n", num, length);
            break;
        case WStype_PING:
            LOG_W("[%u] webSocket get ping\n", num);
            break;
        case WStype_PONG:
            LOG_W("[%u] webSocket get pong\n", num);
            break;
        case WStype_ERROR:
            LOG_W("[%u] webSocket get error\n", num);
            break;
    }
}
static void websocket_loop(void *args){
    char *name = (char*)malloc(20);
    strcpy(name, (char*)args);
    LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
    free(name);
    uint32_t cnt = 0;
    while (true){
        if(g_nmaxe.connection.wifi.status_param.status == WL_CONNECTED){
            webSocket.loop();
        }
        // if(cnt++ % 20 == 0){
        //     UBaseType_t highWaterMark = uxTaskGetStackHighWaterMark(NULL);
        //     LOG_D("%s free stack: %d", name, highWaterMark);
        // }
        delay(100);
    }
}

void start_http_server(void) {
    file_system_init();
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    String name = "(websocket)";
    xTaskCreate(websocket_loop, name.c_str(), 1024*5, (void*)name.c_str(), TASK_PRIORITY_WS, NULL);

    webServer.on("/api/system/info", HTTP_GET, get_system_info);
    webServer.on("/api/ws", HTTP_GET, echo_handler);
    webServer.on("/api/system/restart", HTTP_POST, post_restart);
    webServer.on("/api/system/OTA", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", "POST method allowed");
    }, handleFileUpload);
    webServer.on("/api/system/OTAWWW", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", "POST method allowed");
    }, handleFileUpload);
    webServer.on("/api/system", HTTP_PATCH, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", "PATCH method allowed");
    }, NULL, patch_update_settings);
    webServer.on("/*", HTTP_GET, rest_common_get_handler);
    webServer.begin();
}


