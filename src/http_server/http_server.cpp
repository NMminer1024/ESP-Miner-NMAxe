
#include <Arduino.h>
#include <Update.h>
#include <SPIFFS.h>
#include "ArduinoJson.h"
#include "logger.h"
#include "global.h"
#include "http_server.h"
#include "nvs_config.h"
#include "utils/helper.h"

AsyncWebServer    webServer(80);
WebSocketsServer  webSocket(81);


bool isValidNumber(const String& str) {
    if (str.length() == 0) return false;
    bool hasDot = false;
    bool hasDigit = false;
    for (int i = 0; i < str.length(); i++) {
        char c = str.charAt(i);
        if (i == 0 && (c == '-' || c == '+')) continue;
        if (c == '.') {
            if (hasDot) return false;
            hasDot = true;
            continue;
        }
        if (c >= '0' && c <= '9') {
            hasDigit = true;
            continue;
        }
        return false;
    }
    return hasDigit;
}
void file_system_init() {
    if (!SPIFFS.begin(true, "", 5, NULL)) {
        LOG_E("An Error has occurred while mounting SPIFFS");
        return;
    }
    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();
    LOG_I("File system totalBytes: %d, usedBytes: %d", totalBytes, usedBytes);
}
 String get_content_type_from_file(String filepath) {
    if(filepath.endsWith(".html")) return "text/html";
    else if(filepath.endsWith(".css")) return "text/css";
    else if(filepath.endsWith(".js")) return "application/javascript";
    else if(filepath.endsWith(".png")) return "image/png";
    else if(filepath.endsWith(".ico")) return "image/x-icon";
    else return "text/plain";
}
void rest_common_get_handler(AsyncWebServerRequest *request) {
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


    // if (!request->url().endsWith("/")) {
    //     response->addHeader("Cache-Control", "max-age=86400"); // cache for 24 hour
    // }

    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
    response->addHeader("Content-Encoding", "gzip");

    request->send(response);
    LOG_L("File sending complete: %s, free heap: %d", base_path.c_str(), ESP.getFreeHeap());
}
void get_system_info(AsyncWebServerRequest* request){
    const uint16_t json_size_max  =  2048;
    StaticJsonDocument<json_size_max> root = StaticJsonDocument<json_size_max>();
    root.clear();
    root[HTTP_API_SYS_JSON_KEY_MINER_POWER]             = (g_board.status.power.ibus /1000.0f) * (g_board.status.power.vbus / 1000.0f);
    root[HTTP_API_SYS_JSON_KEY_MINER_VOLT]              = g_board.status.power.vbus;
    root[HTTP_API_SYS_JSON_KEY_MINER_CURRENT]           = g_board.status.power.ibus;
    root[HTTP_API_SYS_JSON_KEY_VCORE_TEMP]              = g_board.status.temp.vcore;
    root[HTTP_API_SYS_JSON_KEY_MCU_TEMP]                = g_board.status.temp.mcu;

    root[HTTP_API_SYS_JSON_KEY_ASIC_CNT]                = g_board.miner->get_asic_count();
    root[HTTP_API_SYS_JSON_KEY_ASIC_MODEL_NAME]         = g_board.info.spec.asic.name;
    root[HTTP_API_SYS_JSON_KEY_ASIC_TEMP]               = g_board.status.temp.asic;
    root[HTTP_API_SYS_JSON_KEY_ASIC_VCORE_REQ]          = g_board.info.spec.asic.req_vcore;
    root[HTTP_API_SYS_JSON_KEY_ASIC_VCORE_ACTUAL]       = g_board.status.power.vcore;
    root[HTTP_API_SYS_JSON_KEY_ASIC_SMALL_CORE_CNT]     = g_board.miner->get_asic_small_cores();
    root[HTTP_API_SYS_JSON_KEY_ASIC_FREQ_REQ]           = g_board.info.spec.asic.req_frq;


    root[HTTP_API_SYS_JSON_KEY_MINER_HR_REALTIME]       = g_board.status.miner.hashrate._3m/1000/1000/1000;
    root[HTTP_API_SYS_JSON_KEY_MINER_BEST_DIFF_EVER]    = formatNumber(g_board.status.miner.diff.best_ever, 4);
    root[HTTP_API_SYS_JSON_KEY_MINER_BEST_DIFF_SESSION] = formatNumber(g_board.status.miner.diff.best_session, 4);
    root[HTTP_API_SYS_JSON_KEY_MINER_FREE_HEAP]         = ESP.getFreeHeap();
    root[HTTP_API_SYS_JSON_KEY_MINER_SHARES_ACCEPTED]   = g_board.status.miner.share_accepted;
    root[HTTP_API_SYS_JSON_KEY_MINER_SHARES_REJECTED]   = g_board.status.miner.share_rejected;
    root[HTTP_API_SYS_JSON_KEY_MINER_UPTIME_SECONDS]    = g_board.status.miner.uptime_session;
    
    root[HTTP_API_SYS_JSON_KEY_BOARD_FW_VERSION]        = g_board.info.base.fw_version;
    root[HTTP_API_SYS_JSON_KEY_BOARD_HW_MODEL]          = g_board.info.spec.name;
    root[HTTP_API_SYS_JSON_KEY_BOARD_HOSTNAME]          = g_board.info.base.hostname;
    root[HTTP_API_SYS_JSON_KEY_BOARD_TIMEZONE]          = g_board.status.time.tz;
    root[HTTP_API_SYS_JSON_KEY_BOARD_WIFI_SSID]         = g_board.info.connection.wifi.conn_param.ssid;
    root[HTTP_API_SYS_JSON_KEY_BOARD_WIFI_STATUS]       = ((g_board.info.connection.wifi.status_param.status == WL_CONNECTED) ? "connected" : "disconnected");

    root[HTTP_API_SYS_JSON_KEY_PERFORMANCE_SCREEN_FLIP]         = g_board.info.preference.screen.flip;
    root[HTTP_API_SYS_JSON_KEY_PERFORMANCE_LED_INDICATOR]       = g_board.info.preference.led.enable;
    root[HTTP_API_SYS_JSON_KEY_PERFORMANCE_FAN_AUTO_SPEED]      = g_board.info.preference.fan.is_auto_speed;
    root[HTTP_API_SYS_JSON_KEY_PERFORMANCE_SCREEN_AUTO_ROLL]    = g_board.info.preference.screen.auto_rolling;
    root[HTTP_API_SYS_JSON_KEY_PERFORMANCE_ASIC_TARGET_TEMP]    = String(g_board.info.preference.fan.target_temp);
    root[HTTP_API_SYS_JSON_KEY_PERFORMANCE_SCREEN_BRIGHTNESS]   = g_board.info.preference.screen.brightness;

    root[HTTP_API_SYS_JSON_KEY_COIN_PRICE_DISPLAY]              = g_board.info.base.coin_price;

    JsonObject stratumObj = root.createNestedObject("stratum");
    JsonObject usedObj = stratumObj.createNestedObject("used");
    usedObj["url"]                = g_board.info.connection.pool_use.ssl ? ("stratum+ssl://" + g_board.info.connection.pool_use.url + ":" + String(g_board.info.connection.pool_use.port)) : ("stratum+tcp://" + g_board.info.connection.pool_use.url + ":" + String(g_board.info.connection.pool_use.port));
    usedObj["user"]               = g_board.info.connection.stratum_use.user;
    usedObj["pwd"]                = g_board.info.connection.stratum_use.pwd;
    JsonObject primaryObj = stratumObj.createNestedObject("primary");
    primaryObj["url"]             = g_board.info.connection.pool_primary.ssl ? ("stratum+ssl://" + g_board.info.connection.pool_primary.url + ":" + String(g_board.info.connection.pool_primary.port)) : ("stratum+tcp://" + g_board.info.connection.pool_primary.url + ":" + String(g_board.info.connection.pool_primary.port));
    primaryObj["user"]            = g_board.info.connection.stratum_primary.user;
    primaryObj["pwd"]             = g_board.info.connection.stratum_primary.pwd;
    JsonObject fallbackObj = stratumObj.createNestedObject("fallback");
    fallbackObj["url"]            = g_board.info.connection.pool_fallback.ssl ? ("stratum+ssl://" + g_board.info.connection.pool_fallback.url + ":" + String(g_board.info.connection.pool_fallback.port)) : ("stratum+tcp://" + g_board.info.connection.pool_fallback.url + ":" + String(g_board.info.connection.pool_fallback.port));
    fallbackObj["user"]           = g_board.info.connection.stratum_fallback.user;
    fallbackObj["pwd"]            = g_board.info.connection.stratum_fallback.pwd;

    // adjust multiple fans status
    root[HTTP_API_SYS_JSON_KEY_FAN_CNT]    = g_board.info.spec.fans.size();
    JsonArray fansArray = root.createNestedArray("fans");
    for(auto & fan : g_board.status.fans){
        JsonObject fanObj = fansArray.createNestedObject();
        fanObj["id"]    = fan.id;        
        fanObj["speed"] = fan.speed;  
        fanObj["rpm"]   = fan.rpm;     
    }

    String json_str;
    serializeJson(root, json_str);
    request->send(200, "application/json", json_str);
}
void get_hr_distribution(AsyncWebServerRequest* request){
    const uint16_t json_size_max  =  512;
    StaticJsonDocument<json_size_max> root = StaticJsonDocument<json_size_max>();

    root.clear();
    root["max_bars"]    = g_board.info.spec.ui.hr_dist_page.max_x_bars;
    root["max_hr"]      = g_board.info.spec.ui.hr_dist_page.max_x_hr;
    root["times"]       = g_board.info.spec.ui.hr_dist_page.times;
    root["dura"]        = g_board.info.spec.ui.hr_dist_page.dura;
    JsonObject dist_map = root.createNestedObject("dist");
    for (const auto& pair : g_board.info.spec.ui.hr_dist_page.dist_map) {
        dist_map[String(pair.first)] = pair.second; 
    }

    String json_str;
    serializeJson(root, json_str);
    request->send(200, "application/json", json_str);
}
void get_status_history(AsyncWebServerRequest* request){
    LOG_D("Starting status history request processing...");
    
    // Maximum data points limit to prevent frontend overload
    const size_t MAX_DATA_POINTS = 2000;
    
    // Safely check history data size with mutex protection
    size_t history_size = 0;
    if (xSemaphoreTake(g_board.status.miner.history_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        history_size = g_board.status.miner.status_history.size();
        xSemaphoreGive(g_board.status.miner.history_mutex);
        LOG_D("History size retrieved: %d records", history_size);
    } else {
        LOG_E("Failed to acquire history mutex, aborting request");
        request->send(500, "application/json", "{\"error\":\"Failed to access history data\"}");
        return;
    }
    
    if (history_size == 0) {
        LOG_W("No history data available, returning empty response");
        request->send(200, "application/json", "{\"statistics\":[],\"size\":0,\"sampledSize\":0}");
        return;
    }
    
    // Parse sample interval from request parameters (user preference)
    int user_sample_interval = 10;
    if(request->hasParam("interval")) {
        user_sample_interval = request->getParam("interval")->value().toInt();
        if(user_sample_interval < 1) user_sample_interval = 1;
        if(user_sample_interval > 100) user_sample_interval = 100;
        LOG_D("User requested sample interval: %d", user_sample_interval);
    }
    
    // Calculate actual sample interval to ensure max 2000 data points
    int actual_sample_interval = user_sample_interval;
    size_t estimated_samples = (history_size + actual_sample_interval - 1) / actual_sample_interval;
    
    // Adjust sample interval if estimated samples exceed limit
    if (estimated_samples > MAX_DATA_POINTS) {
        actual_sample_interval = (history_size + MAX_DATA_POINTS - 1) / MAX_DATA_POINTS;
        estimated_samples = (history_size + actual_sample_interval - 1) / actual_sample_interval;
        LOG_D("Adjusted sample interval from %d to %d to limit data points to %d (estimated: %d)", 
              user_sample_interval, actual_sample_interval, MAX_DATA_POINTS, estimated_samples);
    } else {
        LOG_D("Using requested sample interval %d, estimated samples: %d", actual_sample_interval, estimated_samples);
    }
    
    // Calculate JSON document size based on estimated samples
    uint32_t base_overhead = 2048;
    uint32_t per_sample_size = 200;
    uint32_t json_size_max = base_overhead + (estimated_samples * per_sample_size);
    
    // Add 25% safety margin
    json_size_max = json_size_max + (json_size_max / 4);
    
    // Ensure minimum size
    if (json_size_max < 32 * 1024) json_size_max = 32 * 1024;
    
    LOG_D("Creating JSON document with %dKB for %d estimated samples", json_size_max/1024, estimated_samples);
    
    // Create JSON document
    DynamicJsonDocument root(json_size_max);
    
    // Build JSON structure
    uint64_t ms = g_board.status.time.utc * 1000ULL;
    root["timestamp"] = ms;
    JsonArray labels = root.createNestedArray("labels");
    labels.add("hashRate");
    labels.add("asicTemp");
    labels.add("vcoreTemp");
    labels.add("Pbus");
    labels.add("Vbus");
    labels.add("Ibus");
    labels.add("Vcore");
    labels.add("fanspeed");
    labels.add("fanrpm");
    labels.add("wifiRSSI");
    labels.add("freeHeap");
    labels.add("freePsram");
    labels.add("epoch");
    
    JsonArray data = root.createNestedArray("statistics");
    
    int idx = 0;
    int sampled_count = 0;
    size_t actual_history_size = 0;
    
    // Acquire mutex for history traversal
    if (xSemaphoreTake(g_board.status.miner.history_mutex, portMAX_DELAY) == pdTRUE) {
        actual_history_size = g_board.status.miner.status_history.size();
        LOG_D("Starting sampling: %d total records, interval: %d, max points: %d", 
              actual_history_size, actual_sample_interval, MAX_DATA_POINTS);
        
        for (const auto& history : g_board.status.miner.status_history) {
            if(idx % actual_sample_interval == 0) {
                // Stop if we've reached the maximum data points limit
                if (sampled_count >= MAX_DATA_POINTS) {
                    LOG_D("Reached maximum data points limit (%d), stopping sampling", MAX_DATA_POINTS);
                    break;
                }
                
                JsonArray dataPoint = data.createNestedArray();
                
                // Data validation
                String hashrate = history.hashrate;
                String asic_temp = history.asic_temp;
                String vcore_temp = history.vcore_temp;
                String pbus = history.pbus;
                String vbus = history.vbus;
                String ibus = history.ibus;
                
                if (hashrate.length() == 0 || !isValidNumber(hashrate)) hashrate = "0";
                if (asic_temp.length() == 0 || !isValidNumber(asic_temp)) asic_temp = "0";
                if (vcore_temp.length() == 0 || !isValidNumber(vcore_temp)) vcore_temp = "0";
                if (pbus.length() == 0 || !isValidNumber(pbus)) pbus = "0";
                if (vbus.length() == 0 || !isValidNumber(vbus)) vbus = "0";
                if (ibus.length() == 0 || !isValidNumber(ibus)) ibus = "0";
                
                dataPoint.add(hashrate);
                dataPoint.add(asic_temp);
                dataPoint.add(vcore_temp);
                dataPoint.add(pbus);
                dataPoint.add(vbus);
                dataPoint.add(ibus);
                dataPoint.add(history.vcore);
                dataPoint.add(history.fanspeed);
                dataPoint.add(history.fanrpm);
                dataPoint.add(history.wifi_rssi);
                dataPoint.add(history.free_ram);
                dataPoint.add(history.free_psram);
                dataPoint.add(history.epoch);
                sampled_count++;
                
                // Yield every 100 samples to prevent watchdog timeout
                if (sampled_count % 100 == 0) {
                    delay(1);
                }
            }
            idx++;
            
            // Yield every 500 raw data points
            if (idx % 500 == 0) {
                delay(1);
            }
        }
        
        xSemaphoreGive(g_board.status.miner.history_mutex);
        LOG_D("Sampling completed: %d samples from %d total records, interval: %d", 
              sampled_count, actual_history_size, actual_sample_interval);
    } else {
        LOG_E("Failed to acquire history mutex for data collection");
        request->send(500, "application/json", "{\"error\":\"Failed to access history data\"}");
        return;
    }
    
    // Add metadata
    root["size"] = actual_history_size;
    root["sampledSize"] = sampled_count;
    root["sampleInterval"] = actual_sample_interval;
    root["userRequestedInterval"] = user_sample_interval;
    root["maxDataPoints"] = MAX_DATA_POINTS;
    
    // Serialize and validate JSON response with retry mechanism
    String json_str;
    size_t json_size = 0;
    bool json_valid = false;
    int validation_attempts = 0;
    const int MAX_VALIDATION_ATTEMPTS = 10;
    
    while (!json_valid && (validation_attempts < MAX_VALIDATION_ATTEMPTS)) {
        validation_attempts++;
        
        // Clear previous attempt
        json_str = "";
        
        // Serialize JSON
        json_size = serializeJson(root, json_str);
        
        if (json_size == 0) {
            LOG_E("JSON serialization failed on attempt %d", validation_attempts);
            if (validation_attempts >= MAX_VALIDATION_ATTEMPTS) {
                request->send(500, "application/json", "{\"error\":\"JSON serialization failed after multiple attempts\"}");
                return;
            }
            delay(10); // Small delay before retry
            continue;
        }
        
        // Validate by attempting to deserialize
        DynamicJsonDocument validation_doc(json_size_max);
        DeserializationError error = deserializeJson(validation_doc, json_str);
        
        if (error) {
            // LOG_E("JSON validation failed on attempt %d: %s", validation_attempts, error.c_str());
            // LOG_E("JSON validation failed, json_str length: %d", json_str.length());
            // LOG_E("JSON validation error context (first 200 chars): %.200s", json_str.c_str());
            
            if (validation_attempts >= MAX_VALIDATION_ATTEMPTS) {
                request->send(500, "application/json", "{\"error\":\"JSON validation failed after multiple attempts\"}");
                return;
            }
            delay(10); // Small delay before retry
            continue;
        }
        
        // Additional validation: check key fields exist
        if (!validation_doc.containsKey("timestamp") || 
            !validation_doc.containsKey("labels") || 
            !validation_doc.containsKey("statistics") ||
            !validation_doc.containsKey("size")) {
            LOG_E("JSON validation failed on attempt %d: missing required fields", validation_attempts);
            
            if (validation_attempts >= MAX_VALIDATION_ATTEMPTS) {
                request->send(500, "application/json", "{\"error\":\"JSON structure validation failed\"}");
                return;
            }
            delay(10); // Small delay before retry
            continue;
        }
        
        // Validation successful
        json_valid = true;
        LOG_D("JSON validation successful on attempt %d, size: %d bytes", validation_attempts, json_size);
    }
    
    // Send validated response
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json_str);
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    request->send(response);
    
    LOG_D("Validated response sent: %d bytes, history=%d, sampled=%d, interval=%d/%d, max_points=%d, attempts=%d", 
          json_size, actual_history_size, sampled_count, actual_sample_interval, user_sample_interval, MAX_DATA_POINTS, validation_attempts);
}
void get_status_realtime(AsyncWebServerRequest* request){
    uint32_t json_size_max = 512; // in bytes
    
    // Use local document instead of static to prevent memory leaks
    DynamicJsonDocument root(json_size_max);

    uint64_t ms = g_board.status.time.utc*1000ULL;
    root["timestamp"] = ms;
    JsonArray labels = root.createNestedArray("labels");
    labels.add("hashRate");
    labels.add("asicTemp");
    labels.add("vcoreTemp");
    labels.add("Pbus");
    labels.add("Vbus");
    labels.add("Ibus");
    labels.add("Vcore");
    labels.add("fanspeed");
    labels.add("fanrpm");
    labels.add("wifiRSSI");
    labels.add("freeHeap");
    labels.add("freePsram");
    labels.add("epoch");
    
    JsonArray data = root.createNestedArray("statistics");
    
    // Protect status_history access with mutex
    if (xSemaphoreTake(g_board.status.miner.history_mutex, pdMS_TO_TICKS(portMAX_DELAY)) == pdTRUE) {
        if (!g_board.status.miner.status_history.empty()) {
            auto& history = g_board.status.miner.status_history.back();
            JsonArray dataPoint = data.createNestedArray();
            dataPoint.add(history.hashrate);           // hashRate (GH/s)
            dataPoint.add(history.asic_temp);          // asic_temp (°C)
            dataPoint.add(history.vcore_temp);         // vcore_temp (°C)
            dataPoint.add(history.pbus);               // power (W)
            dataPoint.add(history.vbus);               // voltage (V)
            dataPoint.add(history.ibus);               // current (A)
            dataPoint.add(history.vcore);              // coreVoltageActual (mV)
            dataPoint.add(history.fanspeed);           // fanspeed (%)
            dataPoint.add(history.fanrpm);             // fanrpm (RPM)
            dataPoint.add(history.wifi_rssi);          // wifiRSSI (dBm)
            dataPoint.add(history.free_ram);           // freeHeap (KB)
            dataPoint.add(history.free_psram);         // freePsram (KB)
            dataPoint.add(history.epoch);              // timestamp (ms)
        }
        xSemaphoreGive(g_board.status.miner.history_mutex);
    }
    String json_str;
    serializeJson(root, json_str);
    request->send(200, "application/json", json_str);

    LOG_D("Status realtime sent, history size: %d...", g_board.status.miner.status_history.size());
}
void get_lucky_history(AsyncWebServerRequest* request){
    LOG_D("Starting lucky history request processing...");
    
    // Safely check history data size with mutex protection
    size_t history_size = 0;
    if (xSemaphoreTake(g_board.status.miner.block_proximity_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        history_size = g_board.status.miner.block_proximity_history.size();
        xSemaphoreGive(g_board.status.miner.block_proximity_mutex);
        LOG_D("Lucky history size retrieved: %d records", history_size);
    } else {
        LOG_E("Failed to acquire history mutex, aborting request");
        request->send(500, "application/json", "{\"error\":\"Failed to access history data\"}");
        return;
    }
    
    if (history_size == 0) {
        LOG_W("No lucky history data available, returning empty response");
        request->send(200, "application/json", "{\"statistics\":[],\"size\":0}");
        return;
    }
    
    // Calculate JSON document size based on all data (no sampling)
    uint32_t base_overhead = 2048;
    uint32_t per_sample_size = 200;
    uint32_t json_size_max = base_overhead + (history_size * per_sample_size);
    
    // Add 25% safety margin
    json_size_max = json_size_max + (json_size_max / 4);
    
    // Ensure minimum size
    if (json_size_max < 32 * 1024) json_size_max = 32 * 1024;
    
    LOG_D("Creating lucky JSON document with %dKB for %d samples", json_size_max/1024, history_size);
    
    // Create JSON document
    DynamicJsonDocument root(json_size_max);
    
    // Build JSON structure
    uint64_t ms = g_board.status.time.utc * 1000ULL;
    root["timestamp"] = ms;
    JsonArray labels = root.createNestedArray("labels");
    labels.add("proximity");
    labels.add("share_diff");
    labels.add("net_diff");
    labels.add("epoch");
    
    JsonArray data = root.createNestedArray("statistics");
    
    size_t sampled_count = 0;
    
    // Acquire mutex for history traversal
    if (xSemaphoreTake(g_board.status.miner.block_proximity_mutex, portMAX_DELAY) == pdTRUE) {
        LOG_D("Starting data collection: %d total records", history_size);
        
        for (const auto& history : g_board.status.miner.block_proximity_history) {
            JsonArray dataPoint = data.createNestedArray();
            
            dataPoint.add(history.block_proximity);
            dataPoint.add(history.share_diff);
            dataPoint.add(history.net_diff);
            dataPoint.add(history.epoch);
            sampled_count++;
            // Yield every 100 samples to prevent watchdog timeout
            if (sampled_count % 100 == 0) delay(1);
        }
        
        xSemaphoreGive(g_board.status.miner.block_proximity_mutex);
        LOG_D("Data collection completed: %d samples from %d total records", sampled_count, history_size);
    } else {
        LOG_E("Failed to acquire history mutex for data collection");
        request->send(500, "application/json", "{\"error\":\"Failed to access history data\"}");
        return;
    }
    
    // Add metadata
    root["size"] = sampled_count;
    
    // Serialize JSON response
    String json_str;
    size_t json_size = serializeJson(root, json_str);
    
    if (json_size == 0) {
        LOG_E("JSON serialization failed");
        request->send(500, "application/json", "{\"error\":\"JSON serialization failed\"}");
        return;
    }
    
    // Send response
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json_str);
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    request->send(response);
    
    LOG_D("Lucky history sent: %d bytes, %d records", json_size, sampled_count);
}
void get_swarm_info_handler(AsyncWebServerRequest* request){
    uint32_t json_size_max = 1024 * 40; // in bytes, 40kB about 120 devices
    
    // Use local document instead of static to prevent memory leaks
    DynamicJsonDocument root(json_size_max);

    JsonArray devicesArray = root.createNestedArray("devices");
    for (auto it = g_board.status.swarm.map.begin(); it != g_board.status.swarm.map.end(); it++) {
        String ip        = it->first;
        String swarm_str = it->second;

        DynamicJsonDocument deviceDoc(1024);
        DeserializationError error = deserializeJson(deviceDoc, swarm_str);
        if (error) continue;
        
        JsonObject deviceObj = devicesArray.createNestedObject();
        deviceObj["ip"] = ip;
        deviceObj["BoardType"] = deviceDoc["BoardType"].as<std::string>();
        deviceObj["PoolInUse"] = (deviceDoc.containsKey("PoolInUse")) ? deviceDoc["PoolInUse"].as<std::string>() : "N/A";
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
    serializeJson(root, swarm_info);

    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", swarm_info);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    request->send(response);
    
    LOG_D("Swarm info sent, json size: %d, devices: %d, heap free: %.3f KB", 
          swarm_info.length(), devicesArray.size(), ESP.getFreeHeap() / 1024.0f);
}
void options_theme_handler(AsyncWebServerRequest* request){
    request->send(200, "application/json", "");
}
void get_theme_handler(AsyncWebServerRequest* request){
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
void post_theme_handler(AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total){
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
void echo_handler(AsyncWebServerRequest* request){
    LOG_I("Echo Request...");
}
void post_restart(AsyncWebServerRequest * request){
    LOG_W("************** Restarting System because of API Request ***************");
    // Send HTTP response before restarting
    request->send(200, "text/plain", "System will restart shortly.");
    delay(500);
    xSemaphoreGive(g_board.status.reboot_xsem);
}
void patch_update_settings_handler(AsyncWebServerRequest * request, uint8_t *data, size_t len, size_t index, size_t total){
    static uint16_t SCRATCH_BUFSIZE = 1024;
    
    LOG_D("Update Settings Request, request contentLength: %d, index: %d, total: %d", request->contentLength(), index, total);
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
    LOG_D("Update Settings Payload: %s", buffer);


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

        /************************************** settings->mining config ***************************************************************/
        if(root.containsKey("stratum") && root["stratum"].is<JsonObject>()) {
            JsonObject stratum = root["stratum"].as<JsonObject>();
            // Primary pool
            if(stratum.containsKey("primary") && stratum["primary"].is<JsonObject>()) {
                JsonObject primary = stratum["primary"].as<JsonObject>();
                if(primary.containsKey("url")) {
                    nvs_config_set_string(NVS_CONFIG_STRATUM_URL_PRIMARY, primary["url"].as<String>().c_str());
                }
                if(primary.containsKey("user")) {
                    nvs_config_set_string(NVS_CONFIG_STRATUM_USER_PRIMARY, primary["user"].as<String>().c_str());
                }
                if(primary.containsKey("pwd")) {
                    nvs_config_set_string(NVS_CONFIG_STRATUM_PASS_PRIMARY, primary["pwd"].as<String>().c_str());
                }
            }
            // Fallback pool
            if(stratum.containsKey("fallback") && stratum["fallback"].is<JsonObject>()) {
                JsonObject fallback = stratum["fallback"].as<JsonObject>();
                if(fallback.containsKey("url")) {
                    nvs_config_set_string(NVS_CONFIG_STRATUM_URL_FALLBACK, fallback["url"].as<String>().c_str());
                }
                if(fallback.containsKey("user")) {
                    nvs_config_set_string(NVS_CONFIG_STRATUM_USER_FALLBACK, fallback["user"].as<String>().c_str());
                }
                if(fallback.containsKey("pwd")) {
                    nvs_config_set_string(NVS_CONFIG_STRATUM_PASS_FALLBACK, fallback["pwd"].as<String>().c_str());
                }
            }
        }
        if(root.containsKey("asicVcoreReq")){
            uint16_t req_mv = root["asicVcoreReq"].as<uint16_t>();
            g_board.info.spec.asic.req_vcore = req_mv;
            g_board.power->set_vcore_voltage(req_mv);
            nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, req_mv);
        }
        if(root.containsKey("asicFreqReq")){
            nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, root["asicFreqReq"].as<uint16_t>());
        }
        if(root.containsKey("coinDisplay")){
            nvs_config_set_string(NVS_CONFIG_PRICE_DISPLAY_COIN, root["coinDisplay"].as<String>().c_str());
            g_board.info.base.coin_price = root["coinDisplay"].as<String>();
            g_board.info.base.coin_price.toUpperCase();
        }
        
        /************************************** settings->network config ***************************************************************/
        if(root.containsKey("timezone")){
            nvs_config_set_string(NVS_CONFIG_TIMEZONE, root["timezone"].as<String>().c_str());
            g_board.status.time.tz = root["timezone"].as<String>();
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
            g_board.info.base.hostname                    = root["hostname"].as<String>();
            g_board.info.connection.wifi.softap_param.ssid = root["hostname"].as<String>();
        }

        /************************************** settings->preference config ***************************************************************/
        if(root.containsKey("brightness")){
            g_board.info.preference.screen.brightness = root["brightness"].as<uint8_t>();
            nvs_config_set_u8(NVS_CONFIG_SCREEN_BRIGHTNESS, g_board.info.preference.screen.brightness);
            xSemaphoreGive(g_board.status.brightness_update_xsem);
            LOG_D("Screen brightness set to %d", g_board.info.preference.screen.brightness);
        }
        if(root.containsKey("flipscreen")){
            nvs_config_set_u8(NVS_CONFIG_FLIP_SCREEN, root["flipscreen"].as<uint8_t>());
        }
        if(root.containsKey("ledindicator")){
            g_board.info.preference.led.enable = root["ledindicator"].as<uint8_t>();
            g_board.info.preference.led.sleep  = false;
            nvs_config_set_u8(NVS_CONFIG_LED_INDICATOR, root["ledindicator"].as<uint8_t>());
        }

        if(root.containsKey("autofanspeed")){
            nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, root["autofanspeed"].as<uint16_t>());
            g_board.info.preference.fan.is_auto_speed = root["autofanspeed"].as<uint16_t>();
        }
        if(root.containsKey("targetAsicTemp")){
            nvs_config_set_string(NVS_CONFIG_ASIC_TARGET_TEMP, root["targetAsicTemp"].as<String>().c_str());
            g_board.info.preference.fan.target_temp = root["targetAsicTemp"].as<String>().toFloat();
        }
        if(root.containsKey("autoscreen")){
            nvs_config_set_u8(NVS_CONFIG_AUTO_SCREEN, root["autoscreen"].as<uint8_t>());
            g_board.info.preference.screen.auto_rolling = root["autoscreen"].as<uint8_t>();
        }
        if(root.containsKey("fanspeed")){
            nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, root["fanspeed"].as<uint16_t>());
            g_board.status.fans[0].speed = root["fanspeed"].as<uint16_t>();
        }
        if(root.containsKey("blockhits")){
            nvs_config_set_u16(NVS_CONFIG_BLOCK_HITS, root["blockhits"].as<uint16_t>());
            g_board.status.miner.hits = root["blockhits"].as<uint16_t>();
            g_board.status.miner.last_hits = g_board.status.miner.hits;
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
void file_upload_handler(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
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
            g_board.status.ota.running = true;
            g_board.status.ota.progress    = 0;
            g_board.status.ota.filename    = filename;
        }
    }

    if (Update.write(data, len) != len) {
        Update.printError(Serial);
        request->send(500, "text/plain", "OTA Update Failed. Write error.");
        return;
    }
    else{
        static int lastPercentage = -1;
        g_board.status.ota.progress = (int)((index + len) * 100.0 / flen);
        if (g_board.status.ota.progress != lastPercentage) {
            LOG_I("%s ota: %d%%", filename.c_str(), g_board.status.ota.progress);
            lastPercentage = g_board.status.ota.progress;
        }
    }
    delay(1);//yield to avoid WDT and UI thread freeze 
    if (final) {
        if (Update.end(true)) {
            g_board.status.ota.running = false;
            g_board.status.ota.progress = 100;
            // request->send(200);
            AsyncWebServerResponse *response = request->beginResponse(200);
            response->addHeader("Access-Control-Allow-Origin", "*");
            request->send(response);

            LOG_W("*************** Update Success: %u bytes, rebooting... *************** ", index + len);
            delay(1000);
            xSemaphoreGive(g_board.status.reboot_xsem);
        } else {
            Update.printError(Serial);
            request->send(500, "text/plain", "OTA Update Failed. End error.");
        }
    }
}
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
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
