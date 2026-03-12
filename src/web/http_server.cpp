
#include <Arduino.h>
#include <Update.h>
#include <SPIFFS.h>
#include "ArduinoJson.h"
#include "utils/logger/logger.h"
#include "global.h"
#include "http_server.h"
#include "nvs/nvs_config.h"
#include "utils/helper.h"
#include "board/board.h"

AsyncWebServer  webServer(80);
// AsyncWebSocket runs on the same port 80 at path /ws, no separate port needed.
AsyncWebSocket  webSocket("/ws");

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
    LOG_I("File system totalBytes: %d KB, usedBytes: %d KB", totalBytes / 1024, usedBytes / 1024);
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
    root[HTTP_API_SYS_JSON_KEY_BOARD_TIME_FORMAT]       = g_board.status.time.format.time;
    root[HTTP_API_SYS_JSON_KEY_BOARD_DATE_FORMAT]       = g_board.status.time.format.date;
    root[HTTP_API_SYS_JSON_KEY_BOARD_WIFI_SSID]         = g_board.info.connection.wifi.sta.ssid;
    root[HTTP_API_SYS_JSON_KEY_BOARD_WIFI_STATUS]       = ((g_board.status.wifi.status == WL_CONNECTED) ? "connected" : "disconnected");

    root[HTTP_API_SYS_JSON_KEY_PERFORMANCE_SCREEN_FLIP]         = g_board.status.preference.screen.flip;
    root[HTTP_API_SYS_JSON_KEY_PERFORMANCE_LED_INDICATOR]       = g_board.status.preference.led.enable;
    root[HTTP_API_SYS_JSON_KEY_PERFORMANCE_FAN_AUTO_SPEED]      = g_board.status.preference.fan.is_auto_speed;
    root[HTTP_API_SYS_JSON_KEY_PERFORMANCE_SCREEN_AUTO_ROLL]    = g_board.status.preference.screen.auto_rolling;
    root[HTTP_API_SYS_JSON_KEY_PERFORMANCE_ASIC_TARGET_TEMP]    = String(g_board.status.preference.fan.target_temp);
    root[HTTP_API_SYS_JSON_KEY_PERFORMANCE_SCREEN_BRIGHTNESS]   = g_board.status.preference.screen.brightness;

    root[HTTP_API_SYS_JSON_KEY_COIN_PRICE_DISPLAY]              = g_board.info.base.coin_price;
    root[HTTP_API_SYS_JSON_KEY_COIN_WATCHLIST]                  = g_board.info.base.coin_watchlist;

    JsonObject stratumObj = root.createNestedObject("stratum");
    JsonObject usedObj = stratumObj.createNestedObject("used");
    usedObj["url"]                = g_board.info.connection.pool.use.ssl ? ("stratum+ssl://" + g_board.info.connection.pool.use.url + ":" + String(g_board.info.connection.pool.use.port)) : ("stratum+tcp://" + g_board.info.connection.pool.use.url + ":" + String(g_board.info.connection.pool.use.port));
    usedObj["user"]               = g_board.info.connection.stratum.use.user;
    usedObj["pwd"]                = g_board.info.connection.stratum.use.pwd;
    JsonObject primaryObj = stratumObj.createNestedObject("primary");
    primaryObj["url"]             = g_board.info.connection.pool.primary.ssl ? ("stratum+ssl://" + g_board.info.connection.pool.primary.url + ":" + String(g_board.info.connection.pool.primary.port)) : ("stratum+tcp://" + g_board.info.connection.pool.primary.url + ":" + String(g_board.info.connection.pool.primary.port));
    primaryObj["user"]            = g_board.info.connection.stratum.primary.user;
    primaryObj["pwd"]             = g_board.info.connection.stratum.primary.pwd;
    JsonObject fallbackObj = stratumObj.createNestedObject("fallback");
    fallbackObj["url"]            = g_board.info.connection.pool.fallback.ssl ? ("stratum+ssl://" + g_board.info.connection.pool.fallback.url + ":" + String(g_board.info.connection.pool.fallback.port)) : ("stratum+tcp://" + g_board.info.connection.pool.fallback.url + ":" + String(g_board.info.connection.pool.fallback.port));
    fallbackObj["user"]           = g_board.info.connection.stratum.fallback.user;
    fallbackObj["pwd"]            = g_board.info.connection.stratum.fallback.pwd;

    // adjust multiple fans status
    root[HTTP_API_SYS_JSON_KEY_FAN_CNT]    = g_board.status.fan.count;
    JsonArray fansArray = root.createNestedArray("fans");
    for(auto & fan : g_board.status.fan.list){
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
    root["max_bars"]    = g_board.info.spec.ui.hashrate_dist_page.max_x_bars;
    root["max_hr"]      = g_board.info.spec.ui.hashrate_dist_page.max_x_hr;
    root["count"]       = g_board.info.spec.ui.hashrate_dist_page.count;
    root["time"]        = g_board.info.spec.ui.hashrate_dist_page.time;
    JsonObject dist_map = root.createNestedObject("dist");
    for (const auto& pair : g_board.info.spec.ui.hashrate_dist_page.dist_map) {
        dist_map[String(pair.first)] = pair.second; 
    }

    String json_str;
    serializeJson(root, json_str);
    request->send(200, "application/json", json_str);
}
void get_gauge_limits(AsyncWebServerRequest* request){
    const uint16_t json_size_max  =  1024;
    StaticJsonDocument<json_size_max> root = StaticJsonDocument<json_size_max>();

    root.clear();
    
    // Power limits
    JsonObject power = root.createNestedObject("power");
    JsonObject power_vbus = power.createNestedObject("vbus");
    power_vbus["min"] = g_board.info.spec.ui.dashboard_page.power.vbus.min;
    power_vbus["max"] = g_board.info.spec.ui.dashboard_page.power.vbus.max;
    
    JsonObject power_ibus = power.createNestedObject("ibus");
    power_ibus["min"] = g_board.info.spec.ui.dashboard_page.power.ibus.min;
    power_ibus["max"] = g_board.info.spec.ui.dashboard_page.power.ibus.max;
    
    JsonObject power_power = power.createNestedObject("power");
    power_power["min"] = g_board.info.spec.ui.dashboard_page.power.power.min;
    power_power["max"] = g_board.info.spec.ui.dashboard_page.power.power.max;
    
    // Heat limits
    JsonObject heat = root.createNestedObject("heat");
    JsonObject heat_mcu = heat.createNestedObject("mcu");
    heat_mcu["min"] = g_board.info.spec.ui.dashboard_page.heat.mcu.min;
    heat_mcu["max"] = g_board.info.spec.ui.dashboard_page.heat.mcu.max;
    
    JsonObject heat_asic = heat.createNestedObject("asic");
    heat_asic["min"] = g_board.info.spec.ui.dashboard_page.heat.asic.min;
    heat_asic["max"] = g_board.info.spec.ui.dashboard_page.heat.asic.max;
    
    JsonObject heat_vcore = heat.createNestedObject("vcore");
    heat_vcore["min"] = g_board.info.spec.ui.dashboard_page.heat.vcore.min;
    heat_vcore["max"] = g_board.info.spec.ui.dashboard_page.heat.vcore.max;
    
    JsonObject heat_fan = heat.createNestedObject("fan");
    heat_fan["min"] = g_board.info.spec.ui.dashboard_page.heat.fan.min;
    heat_fan["max"] = g_board.info.spec.ui.dashboard_page.heat.fan.max;
    
    // Performance limits
    JsonObject performance = root.createNestedObject("performance");
    JsonObject perf_asic_freq = performance.createNestedObject("asic_freq_req");
    perf_asic_freq["min"] = g_board.info.spec.ui.dashboard_page.performance.asic_freq_req.min;
    perf_asic_freq["max"] = g_board.info.spec.ui.dashboard_page.performance.asic_freq_req.max;
    
    JsonObject perf_vcore_req = performance.createNestedObject("vcore_req");
    perf_vcore_req["min"] = g_board.info.spec.ui.dashboard_page.performance.vcore_req.min;
    perf_vcore_req["max"] = g_board.info.spec.ui.dashboard_page.performance.vcore_req.max;
    
    JsonObject perf_vcore_measure = performance.createNestedObject("vcore_measure");
    perf_vcore_measure["min"] = g_board.info.spec.ui.dashboard_page.performance.vcore_measure.min;
    perf_vcore_measure["max"] = g_board.info.spec.ui.dashboard_page.performance.vcore_measure.max;

    String json_str;
    serializeJsonPretty(root, json_str);
    request->send(200, "application/json", json_str);
}
void get_oc_vc_list(AsyncWebServerRequest* request){
    const uint16_t json_size_max  =  1024*2;
    StaticJsonDocument<json_size_max> root = StaticJsonDocument<json_size_max>();

    root.clear();

    const std::vector<work_option_t>& oc_opts = g_board.info.spec.ui.setting_page.oc;
    const std::vector<work_option_t>& vc_opts = g_board.info.spec.ui.setting_page.vc;

    JsonObject overclock = root.createNestedObject("overclock");
    JsonArray oc_options = overclock.createNestedArray("options");
    for (const auto& o : oc_opts) {
        JsonObject item = oc_options.createNestedObject();
        item["name"]  = o.name;
        item["value"] = o.value;
    }

    JsonObject vcore = root.createNestedObject("vcore");
    JsonArray vc_options = vcore.createNestedArray("options");
    for (const auto& v : vc_opts) {
        JsonObject item = vc_options.createNestedObject();
        item["name"]  = v.name;
        item["value"] = v.value;
    }

    String json_str;
    serializeJsonPretty(root, json_str);
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
    labels.add("latency");
    labels.add("epoch");
    
    JsonArray data = root.createNestedArray("statistics");
    
    int idx = 0;
    int sampled_count = 0;
    size_t actual_history_size = 0;
    
    // Acquire mutex for history traversal
    // NOTE: portMAX_DELAY in async_tcp context can deadlock the entire TCP stack.
    //       Use a bounded timeout; if the mutex is unavailable, return 503.
    if (xSemaphoreTake(g_board.status.miner.history_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
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
                dataPoint.add(history.latency);
                dataPoint.add(history.epoch);
                sampled_count++;
                
                // Yield every 100 samples to prevent watchdog timeout
                // NOTE: taskYIELD() is safe in async_tcp context; delay() is not
                if (sampled_count % 100 == 0) {
                    taskYIELD();
                }
            }
            idx++;
            
            // Yield every 500 raw data points
            if (idx % 500 == 0) {
                taskYIELD();
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
            taskYIELD(); // Small yield before retry; delay() must not be used in async_tcp context
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
            taskYIELD(); // Small yield before retry
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
            taskYIELD(); // Small yield before retry
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
    uint32_t json_size_max = 1024; // in bytes 
    
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
    labels.add("latency");
    labels.add("epoch");
    
    JsonArray data = root.createNestedArray("statistics");
    
    // Protect status_history access with mutex
    if (xSemaphoreTake(g_board.status.miner.history_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
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
            dataPoint.add(history.latency);            // latency (ms)
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
    if (xSemaphoreTake(g_board.status.miner.block_proximity_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        LOG_D("Starting data collection: %d total records", history_size);
        
        for (const auto& history : g_board.status.miner.block_proximity_history) {
            JsonArray dataPoint = data.createNestedArray();
            
            dataPoint.add(history.block_proximity);
            dataPoint.add(history.share_diff);
            dataPoint.add(history.net_diff);
            dataPoint.add(history.epoch);
            sampled_count++;
            // Yield every 100 samples to prevent watchdog timeout
            if (sampled_count % 100 == 0) taskYIELD();
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

        DynamicJsonDocument deviceDoc(2048);
        DeserializationError error = deserializeJson(deviceDoc, swarm_str);
        if (error) continue;
        
        JsonObject deviceObj = devicesArray.createNestedObject();
        deviceObj["ip"] = ip;
        deviceObj["Hostname"] = (deviceDoc.containsKey("Hostname")) ? deviceDoc["Hostname"].as<std::string>() : "";
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
    // Send HTTP response before restarting.
    // NOTE: delay() must not be called in async_tcp context.
    //       daemon_thread_entry already waits ~1s before acting on reboot_xsem.
    request->send(200, "text/plain", "System will restart shortly.");
    xSemaphoreGive(g_board.status.reboot_xsem);
}
void get_market_available_pairs_handler(AsyncWebServerRequest *request) {
    // Return all USDT pairs discovered from Binance (populated at startup).
    // Format: {"pairs":["BTCUSDT","ETHUSDT",...]}
    // Returns an empty array if fetch hasn't completed yet (WiFi not connected at boot).
    String json;
    json.reserve(10240);  // ~600 pairs × ~15 chars each
    json = "{\"pairs\":[";
    if (g_board.market) {
        const std::vector<String>& pairs = g_board.market->availablePairs;
        for (size_t i = 0; i < pairs.size(); i++) {
            json += "\"";
            json += pairs[i];
            json += "\"";
            if (i + 1 < pairs.size()) json += ",";
        }
    }
    json += "]}";
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}
void patch_update_settings_handler(AsyncWebServerRequest * request, uint8_t *data, size_t len, size_t index, size_t total){
    static uint16_t SCRATCH_BUFSIZE = 2048;
    
    LOG_D("Update Settings Request, request contentLength: %d, index: %d, total: %d", request->contentLength(), index, total);
    if (total >= SCRATCH_BUFSIZE) {
        request->send(500, "text/plain", "Content too long");
        LOG_E("request %s too long", request->url().c_str());
        return;
    }
    char *buffer = (char*)heap_caps_malloc(SCRATCH_BUFSIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    memset(buffer, 0, SCRATCH_BUFSIZE);
    if (index + len <= SCRATCH_BUFSIZE) {
        memcpy(buffer + index, data, len);
    }
    LOG_D("Update Settings Payload: %s", buffer);


    if (index + len == total) {
        buffer[total] = '\0'; 
        StaticJsonDocument<2048> root;
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
            uint16_t req_mhz = root["asicFreqReq"].as<uint16_t>();
            g_board.info.spec.asic.req_frq = req_mhz;
            nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, root["asicFreqReq"].as<uint16_t>());
        }
        /************************************** settings->market config ***************************************************************/
        if(root.containsKey("mainprice")){
            nvs_config_set_string(NVS_CONFIG_PRICE_DISPLAY_COIN, root["mainprice"].as<String>().c_str());
            g_board.info.base.coin_price = root["mainprice"].as<String>();
            g_board.info.base.coin_price.toUpperCase();
        }
        if(root.containsKey("coinWatchlist")){
            nvs_config_set_string(NVS_CONFIG_COIN_WATCHLIST, root["coinWatchlist"].as<String>().c_str());
            g_board.info.base.coin_watchlist = root["coinWatchlist"].as<String>();
        }
        
        /************************************** settings->time config ***************************************************************/
        if(root.containsKey("timezone")){
            nvs_config_set_string(NVS_CONFIG_TIMEZONE, root["timezone"].as<String>().c_str());
            g_board.status.time.tz = root["timezone"].as<String>();
        }
        if(root.containsKey("timeFormat")){
            nvs_config_set_u8(NVS_CONFIG_TIME_FORMAT, root["timeFormat"].as<uint8_t>());
            g_board.status.time.format.time = root["timeFormat"].as<uint8_t>();
        }
        if(root.containsKey("dateFormat")){
            nvs_config_set_string(NVS_CONFIG_DATE_FORMAT, root["dateFormat"].as<String>().c_str());
            g_board.status.time.format.date = root["dateFormat"].as<String>();
        }
        /************************************** settings->network config ***************************************************************/
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
            g_board.info.connection.wifi.ap.info.ssid = root["hostname"].as<String>();
        }

        /************************************** settings->preference config ***************************************************************/
        if(root.containsKey("brightness")){
            g_board.status.preference.screen.brightness = root["brightness"].as<uint8_t>();
            nvs_config_set_u8(NVS_CONFIG_SCREEN_BRIGHTNESS, g_board.status.preference.screen.brightness);
            xSemaphoreGive(g_board.status.brightness_update_xsem);
            LOG_D("Screen brightness set to %d", g_board.status.preference.screen.brightness);
        }
        if(root.containsKey("flipscreen")){
            nvs_config_set_u8(NVS_CONFIG_FLIP_SCREEN, root["flipscreen"].as<uint8_t>());
        }
        if(root.containsKey("ledindicator")){
            g_board.status.preference.led.enable = root["ledindicator"].as<uint8_t>();
            g_board.status.preference.led.sleep  = false;
            nvs_config_set_u8(NVS_CONFIG_LED_INDICATOR, root["ledindicator"].as<uint8_t>());
        }

        if(root.containsKey("autofanspeed")){
            nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, root["autofanspeed"].as<uint16_t>());
            g_board.status.preference.fan.is_auto_speed = root["autofanspeed"].as<uint16_t>();
        }
        if(root.containsKey("targetAsicTemp")){
            nvs_config_set_string(NVS_CONFIG_ASIC_TARGET_TEMP, root["targetAsicTemp"].as<String>().c_str());
            g_board.status.preference.fan.target_temp = root["targetAsicTemp"].as<String>().toFloat();
        }
        if(root.containsKey("autoscreen")){
            nvs_config_set_u8(NVS_CONFIG_AUTO_SCREEN, root["autoscreen"].as<uint8_t>());
            g_board.status.preference.screen.auto_rolling = root["autoscreen"].as<uint8_t>();
        }
        if(root.containsKey("fanspeed")){
            nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, root["fanspeed"].as<uint16_t>());
            g_board.status.fan.list[0].speed = root["fanspeed"].as<uint16_t>();
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
    // Accumulate TCP segments into a larger write buffer before calling Update.write().
    // AsyncWebServer calls this handler once per TCP segment (~1460 bytes). Writing each
    // segment individually is inefficient because:
    //   1. Update.write() is most efficient at multiples of the flash sector size (4096 bytes).
    //   2. Every Update.write() call is followed by vTaskDelay(1) to feed the IDLE watchdog;
    //      fewer calls means less artificial delay.
    // OTA_WRITE_BUFFER_SIZE = 8192 bytes = 2 flash sectors per write call.
    static const size_t OTA_WRITE_BUFFER_SIZE = 8192;
    static uint8_t  ota_buf[OTA_WRITE_BUFFER_SIZE];
    static size_t   ota_buf_len = 0;
    static int      lastPercentage = -1;

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
            // Must use UPDATE_SIZE_UNKNOWN here. SPIFFS.totalBytes() returns the usable
            // filesystem capacity (after metadata overhead), which is smaller than the
            // actual partition size (0x380000 = 3.5MB). The generated spiffs.bin is sized
            // to the full partition, so using totalBytes() as the limit causes "Not Enough
            // Space" at ~91%. UPDATE_SIZE_UNKNOWN lets the Update library read the true
            // partition size from the partition table.
            bin_size = UPDATE_SIZE_UNKNOWN;
            update_type = U_SPIFFS;
        } 

        if (!Update.begin(bin_size, update_type)) { //start with max available size for firmware
            LOG_E("OTA Update error: %s", Update.errorString());
            request->send(500, "text/plain", "OTA Update Failed. Not enough space.");
            return;
        }else{
            g_board.status.ota.running = true;
            g_board.status.ota.progress    = 0;
            g_board.status.ota.filename    = filename;
            ota_buf_len = 0;
            lastPercentage = -1;
        }
    }

    // Accumulate incoming data into ota_buf, flush when buffer is full or this is the last chunk.
    size_t offset = 0;
    while (offset < len) {
        size_t copy_len = min(len - offset, OTA_WRITE_BUFFER_SIZE - ota_buf_len);
        memcpy(ota_buf + ota_buf_len, data + offset, copy_len);
        ota_buf_len += copy_len;
        offset      += copy_len;

        bool flush = (ota_buf_len == OTA_WRITE_BUFFER_SIZE) || (final && offset == len);
        if (flush) {
            if (Update.write(ota_buf, ota_buf_len) != ota_buf_len) {
                LOG_E("OTA Update error: %s", Update.errorString());
                request->send(500, "text/plain", "OTA Update Failed. Write error.");
                ota_buf_len = 0;
                return;
            }
            ota_buf_len = 0;

            // vTaskDelay(1): block async_tcp for 1ms so IDLE task can reset the task watchdog.
            // taskYIELD() is insufficient — it only yields to equal/higher-priority tasks,
            // and IDLE (priority 0) would never run.
            vTaskDelay(pdMS_TO_TICKS(1));

            // Update progress after each flush
            int progress = (int)((index + offset) * 100.0 / flen);
            if (progress != lastPercentage) {
                g_board.status.ota.progress = progress;
                LOG_I("%s ota: %d%%", filename.c_str(), progress);
                lastPercentage = progress;
            }
        }
    }

    if (final) {
        if (Update.end(true)) {
            g_board.status.ota.running = false;
            g_board.status.ota.progress = 100;
            LOG_I("%s ota: %d%%", filename.c_str(), g_board.status.ota.progress);
            AsyncWebServerResponse *response = request->beginResponse(200);
            response->addHeader("Access-Control-Allow-Origin", "*");
            request->send(response);

            LOG_W("*************** Update Success: %u bytes, rebooting *************** ", index + len);
            // NOTE: delay() in async_tcp context blocks the TCP stack.
            //       daemon_thread_entry waits ~1s before restarting, giving time for the HTTP response to be sent.
            xSemaphoreGive(g_board.status.reboot_xsem);
        } else {
            LOG_E("OTA Update error: %s", Update.errorString());
            request->send(500, "text/plain", "OTA Update Failed. End error.");
        }
    }
}
void webSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    // NOTE: Do NOT use LOG_W/LOG_E etc. here — those macros call webSocket.textAll(),
    //       which is re-entrant inside a WebSocket event callback. Use Serial.printf directly.
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("₿ [WS] client #%u connected from %s\r\n",
                          client->id(), client->remoteIP().toString().c_str());
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("₿ [WS] client #%u disconnected\r\n", client->id());
            break;
        case WS_EVT_DATA:
            // Echo text back (mirrors previous behaviour)
            if (len > 0 && data != nullptr) {
                client->text(data, len);
            }
            break;
        case WS_EVT_PONG:
            Serial.printf("₿ [WS] client #%u pong\r\n", client->id());
            break;
        case WS_EVT_ERROR:
            Serial.printf("₿ [WS] client #%u error\r\n", client->id());
            break;
    }
}
