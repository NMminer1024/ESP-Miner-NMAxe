#include <ESPAsyncWebServer.h>
#include <Arduino.h>
#include <Update.h>
#include <SPIFFS.h>
#include "ArduinoJson.h"
#include "logger.h"
#include "global.h"
#include "http_server.h"
#include "nvs_config.h"
#include "utils/helper.h"

static AsyncWebServer  webServer(80);
WebSocketsServer       webSocket(81);

// 自适应内存分配历史记录
struct MemoryAllocationHistory {
    size_t last_history_size = 0;          // 上次处理的历史记录数量
    size_t last_sampled_count = 0;         // 上次实际采样数量
    size_t last_buffer_size = 1024;        // 上次分配的缓冲区大小 (初始1KB)
    size_t last_json_size = 0;             // 上次实际JSON序列化大小
    float bytes_per_sample = 120.0f;       // 每个样本的平均字节数 (动态学习)
    uint32_t allocation_count = 0;         // 分配次数计数器
    bool has_learned_data = false;         // 是否已有学习数据
    
    // 计算下次建议的缓冲区大小
    size_t calculate_next_buffer_size(size_t expected_samples) {
        if (!has_learned_data) {
            // 首次分配或无历史数据，使用保守估计
            return 1024 + (expected_samples * 120); // 1KB基础 + 120字节/样本
        }
        
        // 基于历史数据学习的预测
        size_t predicted_size = 2048 + (size_t)(expected_samples * bytes_per_sample);
        
        // 添加20%安全边际
        predicted_size = predicted_size + (predicted_size / 5);
        
        // 确保最小分配
        if (predicted_size < 1024) predicted_size = 1024;
        
        return predicted_size;
    }
    
    // 更新学习数据
    void update_learning_data(size_t history_size, size_t sampled_count, size_t buffer_size, size_t actual_json_size) {
        last_history_size = history_size;
        last_sampled_count = sampled_count;
        last_buffer_size = buffer_size;
        last_json_size = actual_json_size;
        allocation_count++;
        
        if (sampled_count > 0 && actual_json_size > 0) {
            // 计算实际每样本字节数
            float current_bytes_per_sample = (float)actual_json_size / sampled_count;
            
            if (has_learned_data) {
                // 使用指数移动平均更新学习参数 (权重0.3新数据，0.7历史数据)
                bytes_per_sample = (bytes_per_sample * 0.7f) + (current_bytes_per_sample * 0.3f);
            } else {
                // 首次学习
                bytes_per_sample = current_bytes_per_sample;
                has_learned_data = true;
            }
            
            LOG_W("Memory learning update: samples=%d, json_size=%d, bytes_per_sample=%.1f (previous=%.1f)", 
                  sampled_count, actual_json_size, current_bytes_per_sample, bytes_per_sample);
        }
    }
    
    // 获取分配效率统计
    float get_allocation_efficiency() {
        if (last_buffer_size == 0 || last_json_size == 0) return 0.0f;
        return (float)last_json_size / last_buffer_size;
    }
};

static MemoryAllocationHistory memory_history;


static bool isValidNumber(const String& str) {
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
static void get_status_history(AsyncWebServerRequest* request){
    LOG_W("Starting status history request processing...");
    
    // Maximum data points limit to prevent frontend overload
    const size_t MAX_DATA_POINTS = 2000;
    
    // Safely check history data size with mutex protection
    size_t history_size = 0;
    if (xSemaphoreTake(g_nmaxe.mstatus.history_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        history_size = g_nmaxe.mstatus.status_history.size();
        xSemaphoreGive(g_nmaxe.mstatus.history_mutex);
        LOG_W("History size retrieved: %d records", history_size);
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
        LOG_W("User requested sample interval: %d", user_sample_interval);
    }
    
    // Calculate actual sample interval to ensure max 2000 data points
    int actual_sample_interval = user_sample_interval;
    size_t estimated_samples = (history_size + actual_sample_interval - 1) / actual_sample_interval;
    
    // Adjust sample interval if estimated samples exceed limit
    if (estimated_samples > MAX_DATA_POINTS) {
        actual_sample_interval = (history_size + MAX_DATA_POINTS - 1) / MAX_DATA_POINTS;
        estimated_samples = (history_size + actual_sample_interval - 1) / actual_sample_interval;
        LOG_W("Adjusted sample interval from %d to %d to limit data points to %d (estimated: %d)", 
              user_sample_interval, actual_sample_interval, MAX_DATA_POINTS, estimated_samples);
    } else {
        LOG_W("Using requested sample interval %d, estimated samples: %d", actual_sample_interval, estimated_samples);
    }
    
    // Calculate JSON document size based on estimated samples
    uint32_t base_overhead = 2048;
    uint32_t per_sample_size = 200;
    uint32_t json_size_max = base_overhead + (estimated_samples * per_sample_size);
    
    // Add 25% safety margin
    json_size_max = json_size_max + (json_size_max / 4);
    
    // Ensure minimum size
    if (json_size_max < 32 * 1024) json_size_max = 32 * 1024;
    
    LOG_W("Creating JSON document with %dKB for %d estimated samples", json_size_max/1024, estimated_samples);
    
    // Create JSON document
    DynamicJsonDocument root(json_size_max);
    
    // Build JSON structure
    uint64_t ms = g_nmaxe.mstatus.utc * 1000ULL;
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
    if (xSemaphoreTake(g_nmaxe.mstatus.history_mutex, portMAX_DELAY) == pdTRUE) {
        actual_history_size = g_nmaxe.mstatus.status_history.size();
        LOG_W("Starting sampling: %d total records, interval: %d, max points: %d", 
              actual_history_size, actual_sample_interval, MAX_DATA_POINTS);
        
        for (const auto& history : g_nmaxe.mstatus.status_history) {
            if(idx % actual_sample_interval == 0) {
                // Stop if we've reached the maximum data points limit
                if (sampled_count >= MAX_DATA_POINTS) {
                    LOG_W("Reached maximum data points limit (%d), stopping sampling", MAX_DATA_POINTS);
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
        
        xSemaphoreGive(g_nmaxe.mstatus.history_mutex);
        LOG_W("Sampling completed: %d samples from %d total records, interval: %d", 
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
    
    // Serialize and send response
    String json_str;
    size_t json_size = serializeJson(root, json_str);
    
    if (json_size == 0) {
        LOG_E("JSON serialization failed");
        request->send(500, "application/json", "{\"error\":\"JSON serialization failed\"}");
        return;
    }
    
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json_str);
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    request->send(response);
    
    LOG_W("Response sent: %d bytes, history=%d, sampled=%d, interval=%d/%d, max_points=%d", 
          json_size, actual_history_size, sampled_count, actual_sample_interval, user_sample_interval, MAX_DATA_POINTS);
}
static void get_status_realtime(AsyncWebServerRequest* request){
    uint32_t json_size_max = 512; // in bytes
    
    // Use local document instead of static to prevent memory leaks
    DynamicJsonDocument root(json_size_max);

    uint64_t ms = g_nmaxe.mstatus.utc*1000ULL;
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
    if (xSemaphoreTake(g_nmaxe.mstatus.history_mutex, pdMS_TO_TICKS(portMAX_DELAY)) == pdTRUE) {
        if (!g_nmaxe.mstatus.status_history.empty()) {
            auto& history = g_nmaxe.mstatus.status_history.back();
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
        xSemaphoreGive(g_nmaxe.mstatus.history_mutex);
    }
    String json_str;
    serializeJson(root, json_str);
    request->send(200, "application/json", json_str);

    LOG_W("Status realtime sent, history size: %d...", g_nmaxe.mstatus.status_history.size());
}
static void get_swarm_info_handler(AsyncWebServerRequest* request){
    uint32_t json_size_max = 1024 * 40; // in bytes, 40kB about 120 devices
    
    // Use local document instead of static to prevent memory leaks
    DynamicJsonDocument root(json_size_max);

    JsonArray devicesArray = root.createNestedArray("devices");
    for (auto it = g_nmaxe.swarm.begin(); it != g_nmaxe.swarm.end(); it++) {
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
    webServer.on("/api/system/status/history", HTTP_GET, get_status_history);
    webServer.on("/api/system/status/realtime", HTTP_GET, get_status_realtime);
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


