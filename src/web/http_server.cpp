
#include <Arduino.h>
#include <Update.h>
#include <SPIFFS.h>
#include "ArduinoJson.h"
#include "utils/logger/logger.h"
#include "global.h"
#include "http_server.h"
#include "nvs/nvs_config.h"
#include "utils/helper.h"
#include "utils/reboot_log/reboot_log.h"
#include "utils/reboot_log/coredump.h"
#include "board/board.h"
#include <esp_ota_ops.h>
#include <esp_partition.h>

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

// Initialize SPIFFS; log total/used capacity. Called once at boot.
// Returns true only when SPIFFS mounts AND all essential web files are present.
//
// Failure scenarios that return false (triggering recovery mode):
//   1. A previous SPIFFS OTA upload was interrupted — NVS_CONFIG_SPIFFS_UPDATING is
//      still set, meaning the partition was being overwritten and may be partially valid.
//      Even if SPIFFS can mount afterwards, some Angular chunks may be missing, causing
//      the app to silently fail. We force recovery mode unconditionally in this case.
//   2. SPIFFS partition is unformatted / structurally corrupt → begin() fails.
//   3. Partition mounts but files are missing — e.g. upload was interrupted
//      mid-transfer, leaving an empty or partially-populated SPIFFS.
//
// We check the four fixed-name core files every Angular build produces.
// Hash-suffixed chunk files are not checked because their names change per build,
// but these four are always present and necessary for the app to start:
//   index.html.gz  — HTML entry point
//   runtime.js.gz  — Angular module loader (tiny, loaded first)
//   main.js.gz     — main application bundle
//   styles.css.gz  — global stylesheet
//
// formatOnFail=false: never auto-format. With formatOnFail=true the library
// would silently erase the partition and re-mount it empty, causing begin() to
// return true even though there are no web files, bypassing recovery entirely.
bool file_system_init() {
    // If a SPIFFS OTA was interrupted last boot, the partition is in an unknown
    // state — force recovery mode regardless of what SPIFFS.begin() would return.
    if (nvs_config_get_u8(NVS_CONFIG_SPIFFS_UPDATING, 0)) {
        LOG_E("SPIFFS update was interrupted — forcing recovery mode (NVS flag set)");
        return false;
    }

    if (!SPIFFS.begin(false, "", 5, NULL)) {
        LOG_E("SPIFFS mount failed (partition corrupt or unformatted)");
        return false;
    }
    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();
    LOG_I("File system totalBytes: %d KB, usedBytes: %d KB", totalBytes / 1024, usedBytes / 1024);

    // Verify that all essential web files are present.
    static const char* const REQUIRED_FILES[] = {
        "/index.html.gz",
        "/runtime.js.gz",
        "/main.js.gz",
        "/styles.css.gz",
    };
    for (const char* f : REQUIRED_FILES) {
        if (!SPIFFS.exists(f)) {
            LOG_E("SPIFFS incomplete: required file missing: %s — entering recovery mode", f);
            return false;
        }
    }


    // // ── Normal mode ───────────────────────────────────────────────────────────
    // // [DEBUG] Dump every file on SPIFFS so we can verify uploads (screensaver.gif etc.)
    // {
    //     File root = SPIFFS.open("/");
    //     File f    = root.openNextFile();
    //     size_t totalUsed = SPIFFS.usedBytes(), totalSize = SPIFFS.totalBytes();
    //     LOG_I("[SPIFFS] used %u / %u bytes", (unsigned)totalUsed, (unsigned)totalSize);
    //     if (!f) {
    //         LOG_W("[SPIFFS] filesystem is empty (no files found)");
    //     }
    //     while (f) {
    //         LOG_I("[SPIFFS] %-40s  %u bytes", f.name(), (unsigned)f.size());
    //         f = root.openNextFile();
    //     }
    // }


    return true;
}

// Derive MIME type from file extension for SPIFFS serving.
 String get_content_type_from_file(String filepath) {
    if(filepath.endsWith(".html")) return "text/html";
    else if(filepath.endsWith(".css")) return "text/css";
    else if(filepath.endsWith(".js")) return "application/javascript";
    else if(filepath.endsWith(".png")) return "image/png";
    else if(filepath.endsWith(".ico")) return "image/x-icon";
    else return "text/plain";
}

// Serve gzip-compressed static files from SPIFFS; redirects unknown paths to /.
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

// GET /api/system/info -- dashboard summary: power, temps, ASIC, mining stats, board identity.
void get_system_info(AsyncWebServerRequest* request){
    const uint16_t json_size_max = 1024;
    StaticJsonDocument<json_size_max> root;
    root.clear();

    // Power & electrical
    JsonObject powerObj = root.createNestedObject("power");
    powerObj["power"]   = (g_board.status.power.ibus / 1000.0f) * (g_board.status.power.vbus / 1000.0f);
    powerObj["vbus"]    = g_board.status.power.vbus;
    powerObj["ibus"]    = g_board.status.power.ibus;

    // Temperatures
    JsonObject tempObj  = root.createNestedObject("temps");
    tempObj["vcore"]    = g_board.status.temp.vcore;
    tempObj["asic"]     = g_board.status.temp.asic;

    // ASIC status
    JsonObject asicObj      = root.createNestedObject("asic");
    asicObj["count"]        = g_board.miner->get_asic_count();
    asicObj["model"]        = g_board.info.spec.asic.name;
    asicObj["vcoreReq"]     = g_board.info.spec.asic.req_vcore;
    asicObj["vcoreReal"]    = g_board.status.power.vcore;
    asicObj["freqReq"]      = g_board.info.spec.asic.req_frq;
    asicObj["smallCoreCnt"] = g_board.miner->get_asic_small_cores();

    // Mining stats
    JsonObject minerObj         = root.createNestedObject("miner");
    minerObj["hashRate"]        = g_board.status.miner.hashrate._3m / 1000 / 1000 / 1000;
    minerObj["bestDiffEver"]    = formatNumber(g_board.status.miner.diff.best_ever, 4);
    minerObj["bestDiffSession"] = formatNumber(g_board.status.miner.diff.best_session, 4);
    minerObj["networkDiff"]     = formatNumber(g_board.status.miner.diff.network, 4);
    minerObj["poolDiff"]        = formatNumber(g_board.status.miner.diff.pool, 4);
    minerObj["lastDiff"]        = formatNumber(g_board.status.miner.diff.last, 4);
    minerObj["blkhits"]         = g_board.status.miner.hits;
    minerObj["freeHeap"]        = ESP.getFreeHeap();
    minerObj["sAccepted"]       = g_board.status.miner.share_accepted;
    minerObj["sRejected"]       = g_board.status.miner.share_rejected;
    minerObj["uptimeSeconds"]   = g_board.status.miner.uptime_session;
    minerObj["uptimeEver"]      = g_board.status.miner.uptime_ever;

    // Board identity
    JsonObject identityObj    = root.createNestedObject("identity");
    identityObj["fwVersion"]  = g_board.info.base.fw_version;
    identityObj["hwModel"]    = g_board.info.spec.name;
    identityObj["hostName"]   = g_board.info.base.hostname;
    identityObj["ssid"]       = g_board.info.connection.wifi.sta.ssid;
    identityObj["rssi"]       = g_board.status.wifi.rssi;
    // SHA256 of the running app's ELF (embedded by the linker into
    // esp_app_desc.app_elf_sha256). This is the SAME value the IDF panic
    // handler prints as "ELF file SHA256: ..." in coredumps, so the dashboard
    // can be matched 1:1 against crash logs / saved firmware.elf.
    // (Note: this is NOT the partition/.bin SHA — those are computed over
    // different artifacts and will never match the coredump line.)
    // We expose only the first 16 hex chars (8 bytes) to keep JSON small.
    {
        const esp_app_desc_t* desc = esp_ota_get_app_description();
        if (desc) {
            char hex[17] = {0};
            for (int i = 0; i < 8; ++i)
                snprintf(hex + i * 2, 3, "%02x", desc->app_elf_sha256[i]);
            identityObj["appSha256"] = hex;
        }
    }

    // Currently-active pool (read-only status)
    JsonObject stratumObj = root.createNestedObject("stratum");
    stratumObj["url"]  = g_board.info.connection.pool.use.ssl
        ? ("stratum+ssl://" + g_board.info.connection.pool.use.url + ":" + String(g_board.info.connection.pool.use.port))
        : ("stratum+tcp://" + g_board.info.connection.pool.use.url + ":" + String(g_board.info.connection.pool.use.port));
    stratumObj["user"] = g_board.info.connection.stratum.use.user;
    stratumObj["pwd"]  = g_board.info.connection.stratum.use.pwd;

    // Fan status
    JsonArray fansArray = root.createNestedArray("fans");
    for (auto & fan : g_board.status.fan.list) {
        JsonObject fanObj = fansArray.createNestedObject();
        fanObj["id"]    = fan.id;
        fanObj["speed"] = fan.speed;
        fanObj["rpm"]   = fan.rpm;
    }

    String json_str;
    serializeJson(root, json_str);
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json_str);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}

// GET /api/setting/network -- hostname, SSID, WiFi status and IP.
void get_setting_network(AsyncWebServerRequest* request){
    StaticJsonDocument<256> root;
    root.clear();
    root["hostName"] = g_board.info.base.hostname;
    root["ssid"]     = g_board.info.connection.wifi.sta.ssid;
    root["status"]   = (g_board.status.wifi.status == WL_CONNECTED) ? "connected" : "disconnected";
    root["ip"]       = g_board.status.wifi.ip.toString();
    String json_str;
    serializeJson(root, json_str);
    request->send(200, "application/json", json_str);
}

// GET /api/setting/network -- PATCH: save hostname, SSID, password to NVS.
void patch_setting_network(AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total){
    static const uint16_t BUF_SIZE = 512;
    if (total >= BUF_SIZE) { request->send(400, "application/json", "{\"error\":\"payload too large\"}"); return; }
    char *buf = (char*)heap_caps_malloc(BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { request->send(500, "application/json", "{\"error\":\"oom\"}"); return; }
    memset(buf, 0, BUF_SIZE);
    if (index + len < BUF_SIZE) memcpy(buf + index, data, len);
    if (index + len == total) {
        buf[total] = '\0';
        StaticJsonDocument<512> root;
        if (deserializeJson(root, buf) || !root.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"invalid json\"}");
            free(buf); return;
        }
        if (root.containsKey("ssid"))     nvs_config_set_string(NVS_CONFIG_WIFI_SSID, root["ssid"].as<String>().c_str());
        if (root.containsKey("wifiPass")) nvs_config_set_string(NVS_CONFIG_WIFI_PASS, root["wifiPass"].as<String>().c_str());
        if (root.containsKey("hostname")) {
            nvs_config_set_string(NVS_CONFIG_HOSTNAME, root["hostname"].as<String>().c_str());
            nvs_config_set_string(NVS_CONFIG_AP_SSID,  root["hostname"].as<String>().c_str());
            g_board.info.base.hostname                = root["hostname"].as<String>();
            g_board.info.connection.wifi.ap.info.ssid = root["hostname"].as<String>();
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    }
    free(buf);
}

// GET /api/setting/time -- timezone, time/date format.
void get_setting_time(AsyncWebServerRequest* request){
    StaticJsonDocument<256> root;
    root.clear();
    root["timeZone"]   = g_board.status.time.tz;
    root["timeFormat"] = g_board.status.time.format.time;
    root["dateFormat"] = g_board.status.time.format.date;
    String json_str;
    serializeJson(root, json_str);
    request->send(200, "application/json", json_str);
}

// PATCH /api/setting/time -- save timezone and date/time format to NVS.
void patch_setting_time(AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total){
    static const uint16_t BUF_SIZE = 256;
    if (total >= BUF_SIZE) { request->send(400, "application/json", "{\"error\":\"payload too large\"}"); return; }
    char *buf = (char*)heap_caps_malloc(BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { request->send(500, "application/json", "{\"error\":\"oom\"}"); return; }
    memset(buf, 0, BUF_SIZE);
    if (index + len < BUF_SIZE) memcpy(buf + index, data, len);
    if (index + len == total) {
        buf[total] = '\0';
        StaticJsonDocument<256> root;
        if (deserializeJson(root, buf) || !root.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"invalid json\"}");
            free(buf); return;
        }
        if (root.containsKey("timezone")) {
            nvs_config_set_string(NVS_CONFIG_TIMEZONE, root["timezone"].as<String>().c_str());
            g_board.status.time.tz = root["timezone"].as<String>();
        }
        if (root.containsKey("timeFormat")) {
            nvs_config_set_u8(NVS_CONFIG_TIME_FORMAT, root["timeFormat"].as<uint8_t>());
            g_board.status.time.format.time = root["timeFormat"].as<uint8_t>();
        }
        if (root.containsKey("dateFormat")) {
            nvs_config_set_string(NVS_CONFIG_DATE_FORMAT, root["dateFormat"].as<String>().c_str());
            g_board.status.time.format.date = root["dateFormat"].as<String>();
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    }
    free(buf);
}

// -- /api/setting/mining -- GET -----------------------------------------------
// Returns stratum config, current asic freq/vcore, and OC/VC dropdown options.
void get_setting_mining(AsyncWebServerRequest* request){
    const uint16_t json_size_max = 2048;
    StaticJsonDocument<json_size_max> root;
    root.clear();

    root["vcoreReq"] = g_board.info.spec.asic.req_vcore;
    root["freqReq"]  = g_board.info.spec.asic.req_frq;
    root["asic"]     = g_board.info.spec.asic.name;

    JsonObject stratumObj    = root.createNestedObject("stratum");
    JsonObject usedObj       = stratumObj.createNestedObject("used");
    usedObj["url"]  = g_board.info.connection.pool.use.ssl
        ? ("stratum+ssl://" + g_board.info.connection.pool.use.url + ":" + String(g_board.info.connection.pool.use.port))
        : ("stratum+tcp://" + g_board.info.connection.pool.use.url + ":" + String(g_board.info.connection.pool.use.port));
    usedObj["user"] = g_board.info.connection.stratum.use.user;
    usedObj["pwd"]  = g_board.info.connection.stratum.use.pwd;

    JsonObject primaryObj    = stratumObj.createNestedObject("primary");
    primaryObj["url"]  = g_board.info.connection.pool.primary.ssl
        ? ("stratum+ssl://" + g_board.info.connection.pool.primary.url + ":" + String(g_board.info.connection.pool.primary.port))
        : ("stratum+tcp://" + g_board.info.connection.pool.primary.url + ":" + String(g_board.info.connection.pool.primary.port));
    primaryObj["user"] = g_board.info.connection.stratum.primary.user;
    primaryObj["pwd"]  = g_board.info.connection.stratum.primary.pwd;

    JsonObject fallbackObj   = stratumObj.createNestedObject("fallback");
    fallbackObj["url"]  = g_board.info.connection.pool.fallback.ssl
        ? ("stratum+ssl://" + g_board.info.connection.pool.fallback.url + ":" + String(g_board.info.connection.pool.fallback.port))
        : ("stratum+tcp://" + g_board.info.connection.pool.fallback.url + ":" + String(g_board.info.connection.pool.fallback.port));
    fallbackObj["user"] = g_board.info.connection.stratum.fallback.user;
    fallbackObj["pwd"]  = g_board.info.connection.stratum.fallback.pwd;

    const std::vector<work_option_t>& oc_opts = g_board.info.spec.ui.setting_page.oc;
    const std::vector<work_option_t>& vc_opts = g_board.info.spec.ui.setting_page.vc;

    JsonObject overclock    = root.createNestedObject("overclock");
    JsonArray  oc_options   = overclock.createNestedArray("options");
    for (const auto& o : oc_opts) {
        JsonObject item = oc_options.createNestedObject();
        item["name"]  = o.name;
        item["value"] = o.value;
    }

    JsonObject vcore        = root.createNestedObject("vcore");
    JsonArray  vc_options   = vcore.createNestedArray("options");
    for (const auto& v : vc_opts) {
        JsonObject item = vc_options.createNestedObject();
        item["name"]  = v.name;
        item["value"] = v.value;
    }

    String json_str;
    serializeJson(root, json_str);
    request->send(200, "application/json", json_str);
}

// PATCH /api/setting/mining -- save stratum URLs/users/passwords and ASIC freq/vcore to NVS.
void patch_setting_mining(AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total){
    static const uint16_t BUF_SIZE = 1024;
    if (total >= BUF_SIZE) { request->send(400, "application/json", "{\"error\":\"payload too large\"}"); return; }
    char *buf = (char*)heap_caps_malloc(BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { request->send(500, "application/json", "{\"error\":\"oom\"}"); return; }
    memset(buf, 0, BUF_SIZE);
    if (index + len < BUF_SIZE) memcpy(buf + index, data, len);
    if (index + len == total) {
        buf[total] = '\0';
        StaticJsonDocument<1024> root;
        if (deserializeJson(root, buf) || !root.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"invalid json\"}");
            free(buf); return;
        }
        if (root.containsKey("stratum") && root["stratum"].is<JsonObject>()) {
            JsonObject stratum = root["stratum"].as<JsonObject>();
            if (stratum.containsKey("primary") && stratum["primary"].is<JsonObject>()) {
                JsonObject primary = stratum["primary"].as<JsonObject>();
                if (primary.containsKey("url"))  nvs_config_set_string(NVS_CONFIG_STRATUM_URL_PRIMARY,  primary["url"].as<String>().c_str());
                if (primary.containsKey("user")) nvs_config_set_string(NVS_CONFIG_STRATUM_USER_PRIMARY, primary["user"].as<String>().c_str());
                if (primary.containsKey("pwd"))  nvs_config_set_string(NVS_CONFIG_STRATUM_PASS_PRIMARY, primary["pwd"].as<String>().c_str());
            }
            if (stratum.containsKey("fallback") && stratum["fallback"].is<JsonObject>()) {
                JsonObject fallback = stratum["fallback"].as<JsonObject>();
                if (fallback.containsKey("url"))  nvs_config_set_string(NVS_CONFIG_STRATUM_URL_FALLBACK,  fallback["url"].as<String>().c_str());
                if (fallback.containsKey("user")) nvs_config_set_string(NVS_CONFIG_STRATUM_USER_FALLBACK, fallback["user"].as<String>().c_str());
                if (fallback.containsKey("pwd"))  nvs_config_set_string(NVS_CONFIG_STRATUM_PASS_FALLBACK, fallback["pwd"].as<String>().c_str());
            }
        }
        if (root.containsKey("asicVcoreReq")) {
            uint16_t req_mv = root["asicVcoreReq"].as<uint16_t>();
            g_board.info.spec.asic.req_vcore = req_mv;
            g_board.power->set_vcore_voltage(req_mv);
            nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, req_mv);
        }
        if (root.containsKey("asicFreqReq")) {
            uint16_t req_mhz = root["asicFreqReq"].as<uint16_t>();
            g_board.info.spec.asic.req_frq = req_mhz;
            nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, req_mhz);
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    }
    free(buf);
}

// -- /api/setting/market -- GET -----------------------------------------------
void get_setting_market(AsyncWebServerRequest* request){
    // Build response manually: settings fields + large pairs array (can be ~10 KB)
    String json;
    json.reserve(10800);
    json  = "{\"mainprice\":\"";
    json += g_board.info.base.coin_price;
    json += "\",\"coinWatchlist\":\"";
    json += g_board.info.base.coin_watchlist;
    json += "\",\"pairs\":[";
    if (g_board.market) {
        const char* buf = g_board.market->get_pairs_buffer();
        uint16_t    cnt = g_board.market->get_pairs_count();
        if (buf && cnt > 0) {
            const char* p = buf;
            for (uint16_t i = 0; i < cnt; i++) {
                const char* end = strchr(p, '\n');
                size_t slen = end ? (size_t)(end - p) : strlen(p);
                json += "\"";
                json.concat(p, slen);
                json += "\"";
                if (i + 1 < cnt) json += ",";
                p = end ? end + 1 : p + slen;
            }
        }
    }
    json += "]}";
    request->send(200, "application/json", json);
}

// PATCH /api/setting/market -- save main price coin and watchlist to NVS.
void patch_setting_market(AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total){
    static const uint16_t BUF_SIZE = 1024;
    if (total >= BUF_SIZE) { request->send(400, "application/json", "{\"error\":\"payload too large\"}"); return; }
    char *buf = (char*)heap_caps_malloc(BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { request->send(500, "application/json", "{\"error\":\"oom\"}"); return; }
    memset(buf, 0, BUF_SIZE);
    if (index + len < BUF_SIZE) memcpy(buf + index, data, len);
    if (index + len == total) {
        buf[total] = '\0';
        StaticJsonDocument<1024> root;
        if (deserializeJson(root, buf) || !root.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"invalid json\"}");
            free(buf); return;
        }
        if (root.containsKey("mainprice")) {
            nvs_config_set_string(NVS_CONFIG_PRICE_DISPLAY_COIN, root["mainprice"].as<String>().c_str());
            g_board.info.base.coin_price = root["mainprice"].as<String>();
            g_board.info.base.coin_price.toUpperCase();
        }
        if (root.containsKey("coinWatchlist")) {
            nvs_config_set_string(NVS_CONFIG_COIN_WATCHLIST, root["coinWatchlist"].as<String>().c_str());
            g_board.info.base.coin_watchlist = root["coinWatchlist"].as<String>();
        }
        // Trigger an immediate market refresh so the new coin settings take effect at once.
        if (g_board.market) g_board.market->request_refresh();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    }
    free(buf);
}

// GET /api/setting/preference -- screen flip/brightness/auto-roll, fan speed, LED indicator.
void get_setting_preference(AsyncWebServerRequest* request){

    auto get_fan_config = [&](uint8_t fan_id) -> fan_config_t* {
        for(auto &fan_cfg : g_board.info.spec.fans){
            if(fan_cfg.id == fan_id){
                return &fan_cfg;
            }
        }
        return nullptr;
    };

    StaticJsonDocument<1024> root;
    root.clear();
    root["screenFlip"]        = g_board.status.preference.screen.flip;
    root["ledIndicator"]      = g_board.status.preference.led.enable;
    root["screenAutoRoll"]    = g_board.status.preference.screen.auto_rolling;
    root["Brightness"]        = g_board.status.preference.screen.brightness;
    root["screensaverEnable"] = g_board.status.preference.screen.saver_enable ? 1 : 0;
    root["screensaverTimeout"]= (uint32_t)g_board.status.preference.screen.saver_timeout;
    root["hwModel"]           = g_board.info.spec.name;
    JsonArray fansArray    = root.createNestedArray("fans");
    for (auto & fan : g_board.status.fan.list) {
        JsonObject fanObj = fansArray.createNestedObject();
        auto cfg = get_fan_config(fan.id); // ensure the fan config exists for this fan ID; if not, defaults will be used
        if(!cfg) continue;
        fanObj["id"]        = fan.id;
        fanObj["speed"]     = fan.speed;
        fanObj["rpm"]       = fan.rpm;
        fanObj["auto"]      = cfg->auto_speed;
        fanObj["target"]    = cfg->target_temp;
    }
    String json_str;
    serializeJson(root, json_str);
    request->send(200, "application/json", json_str);
}

// PATCH /api/setting/preference -- save display/fan/LED preferences to NVS.
void patch_setting_preference(AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total){
    static const uint16_t BUF_SIZE = 1024;
    if (total >= BUF_SIZE) { request->send(400, "application/json", "{\"error\":\"payload too large\"}"); return; }
    char *buf = (char*)heap_caps_malloc(BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { request->send(500, "application/json", "{\"error\":\"oom\"}"); return; }
    memset(buf, 0, BUF_SIZE);
    if (index + len < BUF_SIZE) memcpy(buf + index, data, len);
    if (index + len == total) {
        buf[total] = '\0';
        StaticJsonDocument<BUF_SIZE> root;
        if (deserializeJson(root, buf) || !root.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"invalid json\"}");
            free(buf); return;
        }
        if (root.containsKey("Brightness")) {
            uint8_t brightness = (root["Brightness"].as<uint8_t>() <=1) ? 1 : ((root["Brightness"].as<uint8_t>() >= 100) ? 100 : root["Brightness"].as<uint8_t>());
            g_board.status.preference.screen.brightness = brightness;
            nvs_config_set_u8(NVS_CONFIG_SCREEN_BRIGHTNESS, brightness);
            xSemaphoreGive(g_board.status.brightness_update_xsem);
        }
        if (root.containsKey("ledIndicator")) {
            g_board.status.preference.led.enable = root["ledIndicator"].as<uint8_t>();
            g_board.status.preference.led.sleep  = false;
            nvs_config_set_u8(NVS_CONFIG_LED_INDICATOR, root["ledIndicator"].as<uint8_t>());
        }
        if (root.containsKey("screenAutoRoll")) {
            nvs_config_set_u8(NVS_CONFIG_AUTO_SCREEN, root["screenAutoRoll"].as<uint8_t>());
            g_board.status.preference.screen.auto_rolling = root["screenAutoRoll"].as<uint8_t>();
        }
        if (root.containsKey("screenFlip"))  {
            nvs_config_set_u8(NVS_CONFIG_FLIP_SCREEN, root["screenFlip"].as<uint8_t>());
        }
        if (root.containsKey("screensaverEnable"))  {
            nvs_config_set_u8(NVS_CONFIG_SCREEN_SAVER_ENABLE, root["screensaverEnable"].as<uint8_t>());
            g_board.status.preference.screen.saver_enable = root["screensaverEnable"].as<uint8_t>();
        }
        if (root.containsKey("screensaverTimeout"))  {
            nvs_config_set_u32(NVS_CONFIG_SCREEN_SAVER_TIMEOUT, root["screensaverTimeout"].as<uint32_t>());
            g_board.status.preference.screen.saver_timeout = root["screensaverTimeout"].as<uint32_t>();
        }
        // fans array: id=0 → ASIC fan, id=1 → Vcore fan
        if (root.containsKey("fans") && root["fans"].is<JsonArray>()) {
            for (JsonObject fan : root["fans"].as<JsonArray>()) {
                if (!fan.containsKey("id")) continue;
                uint8_t fan_id = fan["id"].as<uint8_t>();
                if (fan_id == 0) {
                    if (fan.containsKey("auto")) {
                        nvs_config_set_u16(NVS_CONFIG_AUTO_ASIC_FAN_SPEED, fan["auto"].as<uint16_t>());
                        g_board.info.spec.fans[0].auto_speed = fan["auto"].as<uint16_t>();
                    }
                    if (fan.containsKey("target")) {
                        String t = String(fan["target"].as<float>(), 1);
                        nvs_config_set_string(NVS_CONFIG_ASIC_TARGET_TEMP, t.c_str());
                        g_board.info.spec.fans[0].target_temp = fan["target"].as<float>();
                    }
                    if (fan.containsKey("speed")) {
                        nvs_config_set_u16(NVS_CONFIG_ASIC_FAN_SPEED, fan["speed"].as<uint16_t>());
                        g_board.status.fan.list[0].speed = fan["speed"].as<uint16_t>();
                    }
                } else if (fan_id == 1) {
                    if (fan.containsKey("auto")) {
                        nvs_config_set_u16(NVS_CONFIG_AUTO_VCORE_FAN_SPEED, fan["auto"].as<uint16_t>());
                        g_board.info.spec.fans[1].auto_speed = fan["auto"].as<uint16_t>();
                    }
                    if (fan.containsKey("target")) {
                        String t = String(fan["target"].as<float>(), 1);
                        nvs_config_set_string(NVS_CONFIG_VCORE_TARGET_TEMP, t.c_str());
                        g_board.info.spec.fans[1].target_temp = fan["target"].as<float>();
                    }
                    if (fan.containsKey("speed")) {
                        nvs_config_set_u16(NVS_CONFIG_VCORE_FAN_SPEED, fan["speed"].as<uint16_t>());
                        g_board.status.fan.list[1].speed = fan["speed"].as<uint16_t>();
                    }
                }
            }
        }

        for(const auto &kv : root.as<JsonObject>()) {
            LOG_W("Preference update: %s = %s", kv.key().c_str(), kv.value().as<String>().c_str());
        }

        request->send(200, "application/json", "{\"status\":\"ok\"}");
    }
    free(buf);
}

// GET /api/dashboard/hr/dist -- hashrate distribution bar config and sample counts.
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

// GET /api/dashboard/gauge/limits -- min/max ranges for dashboard power/heat/performance gauges.
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

// GET /api/dashboard/chart/history -- full miner status time-series (up to MAX_DATA_POINTS records).
// Uses PSRAM for both JSON document and serialization buffer to avoid
// fragmenting internal SRAM and to survive long uptimes on boards with high
// baseline PSRAM usage (e.g. NMQAxe++).
void get_status_history(AsyncWebServerRequest* request){
    LOG_D("Starting status history request processing...");
    
    // Maximum data points limit to prevent frontend overload
    const size_t MAX_DATA_POINTS = 2000;
    
    // Safely check history data size with mutex protection
    size_t history_size = 0;
    if (xSemaphoreTake(g_board.status.miner.status_history.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        history_size = g_board.status.miner.status_history.deque.size();
        xSemaphoreGive(g_board.status.miner.status_history.mutex);
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
    // ArduinoJson 6 internal: each VariantSlot = 16 bytes on ESP32 (32-bit).
    // Per data point: 14 elements × 16B + 1 nested array slot × 16B = 240 bytes.
    uint32_t base_overhead = 2048;
    uint32_t per_sample_size = 256;  // ArduinoJson internal memory per sample (240 + margin)
    uint32_t json_size_max = base_overhead + (estimated_samples * per_sample_size);
    
    // Add 25% safety margin
    json_size_max = json_size_max + (json_size_max / 4);
    
    // Ensure minimum size
    if (json_size_max < 32 * 1024) json_size_max = 32 * 1024;
    
    LOG_D("Creating PSRAM JSON document with %dKB for %d estimated samples", json_size_max/1024, estimated_samples);
    
    // Create JSON document in PSRAM (Fix 2)
    BasicJsonDocument<PsramJsonAllocator> root(json_size_max);
    
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
    if (xSemaphoreTake(g_board.status.miner.status_history.mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        actual_history_size = g_board.status.miner.status_history.deque.size();
        LOG_D("Starting sampling: %d total records, interval: %d, max points: %d", 
              actual_history_size, actual_sample_interval, MAX_DATA_POINTS);
        
        for (const auto& history : g_board.status.miner.status_history.deque) {
            if(idx % actual_sample_interval == 0) {
                // Stop if we've reached the maximum data points limit
                if (sampled_count >= (int)MAX_DATA_POINTS) {
                    LOG_D("Reached maximum data points limit (%d), stopping sampling", MAX_DATA_POINTS);
                    break;
                }
                
                JsonArray dataPoint = data.createNestedArray();
                
                // history_node_t fields are now native floats/ints — no String validation needed
                dataPoint.add(history.hashrate);
                dataPoint.add(history.asic_temp);
                dataPoint.add(history.vcore_temp);
                dataPoint.add(history.pbus);
                dataPoint.add(history.vbus);
                dataPoint.add(history.ibus);
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
        
        xSemaphoreGive(g_board.status.miner.status_history.mutex);
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
    
    // Serialize directly into a PSRAM buffer to avoid a large contiguous
    // Arduino String allocation on the (possibly fragmented) default heap. (Fix 4)
    size_t json_len = measureJson(root);
    char* json_buf = (char*)heap_caps_malloc(json_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!json_buf) {
        LOG_E("Failed to allocate %d bytes in PSRAM for JSON output", json_len + 1);
        request->send(500, "application/json", "{\"error\":\"PSRAM allocation failed for response\"}");
        return;
    }
    serializeJson(root, json_buf, json_len + 1);
    
    // Free the JSON document memory before sending to reduce peak usage
    root.clear();
    
    // Stream the PSRAM buffer to the client via a callback-based response
    // so that no secondary copy of the payload is needed.
    AsyncWebServerResponse *response = request->beginResponse(
        "application/json", json_len,
        [json_buf, json_len](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
            size_t remaining = json_len - index;
            size_t toWrite = (remaining < maxLen) ? remaining : maxLen;
            memcpy(buffer, json_buf + index, toWrite);
            if (index + toWrite >= json_len) {
                heap_caps_free(json_buf);  // free PSRAM buffer after last chunk
            }
            return toWrite;
        });
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    request->send(response);
    
    LOG_D("Response sent: %d bytes, history=%d, sampled=%d, interval=%d/%d, max_points=%d", 
          json_len, actual_history_size, sampled_count, actual_sample_interval, user_sample_interval, MAX_DATA_POINTS);
}

// GET /api/dashboard/chart/realtime -- single latest data point for live chart update.
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
    if (xSemaphoreTake(g_board.status.miner.status_history.mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        if (!g_board.status.miner.status_history.deque.empty()) {
            auto& history = g_board.status.miner.status_history.deque.back();
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
        xSemaphoreGive(g_board.status.miner.status_history.mutex);
    }
    String json_str;
    serializeJson(root, json_str);
    request->send(200, "application/json", json_str);

    LOG_D("Status realtime sent, history size: %d...", g_board.status.miner.status_history.deque.size());
}

// GET /api/dashboard/luck/history -- block-proximity history for luck/difficulty chart.
void get_lucky_history(AsyncWebServerRequest* request){
    LOG_D("Starting lucky history request processing...");
    
    // Safely check history data size with mutex protection
    size_t history_size = 0;
    if (xSemaphoreTake(g_board.status.miner.proximity_history.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        history_size = g_board.status.miner.proximity_history.deque.size();
        xSemaphoreGive(g_board.status.miner.proximity_history.mutex);
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
    if (xSemaphoreTake(g_board.status.miner.proximity_history.mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        LOG_D("Starting data collection: %d total records", history_size);
        
        for (const auto& history : g_board.status.miner.proximity_history.deque) {
            JsonArray dataPoint = data.createNestedArray();
            
            dataPoint.add(history.block_proximity);
            dataPoint.add(history.share_diff);
            dataPoint.add(history.net_diff);
            dataPoint.add(history.epoch);
            sampled_count++;
            // Yield every 100 samples to prevent watchdog timeout
            if (sampled_count % 100 == 0) taskYIELD();
        }
        
        xSemaphoreGive(g_board.status.miner.proximity_history.mutex);
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

// OPTIONS /api/theme -- CORS preflight (actual CORS headers set by wildcard handler).
void options_theme_handler(AsyncWebServerRequest* request){
    request->send(200, "application/json", "");
}

// GET /api/theme -- current color scheme name and accent color map.
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

// POST /api/theme -- persist colorScheme, theme name and accentColors to NVS.
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

// GET /api/log -- WebSocket log stream endpoint (logs echoed via WebSocket).
void echo_handler(AsyncWebServerRequest* request){
    LOG_I("Echo Request...");
}

// POST /api/system/restart -- trigger a graceful soft reboot via semaphore.
void post_restart(AsyncWebServerRequest * request){
    LOG_W("************** Restarting System because of API Request ***************");
    // Stamp the intent BEFORE giving the semaphore so the daemon thread sees it.
    String ip = request->client() ? request->client()->remoteIP().toString() : String("?");
    reboot_intent_set(REBOOT_INTENT_USER_WEB_REBOOT, "from %s", ip.c_str());
    // Send HTTP response before restarting.
    // NOTE: delay() must not be called in async_tcp context.
    //       daemon_thread_entry already waits ~1s before acting on reboot_xsem.
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "System will restart shortly.");
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    xSemaphoreGive(g_board.status.reboot_xsem);
}

// POST /api/system/clearhits -- reset block-hit counter to zero in RAM and NVS.
void post_reset_block_hits(AsyncWebServerRequest * request){
    g_board.status.miner.hits      = 0;
    xEventGroupClearBits(g_board.status.sys_evt, SYS_EVENT_MINER_BLOCK_HIT | SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED); 
    nvs_config_set_u16(NVS_CONFIG_BLOCK_HITS, 0);
    LOG_I("Miner stats reset: block hits cleared");
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// ── Reboot history endpoints ────────────────────────────────────────────────
//
// Records are produced by reboot_log_init() at the very top of setup() based on
// the previous run's RTC state + esp_reset_reason(). The web layer only reads.
//
// JSON shape (one record):
//   {
//     "idx": 142, "ts": 0, "uptime": 11520, "heapMin": 49152,
//     "reset": "panic", "intent": "none", "class": "crash",
//     "fw": "v3.0.11", "detail": "panic"
//   }
static void reboot_record_to_json(const RebootRecord& r, JsonObject obj) {
    obj["idx"]     = r.boot_index;
    obj["ts"]      = r.epoch_ts;
    obj["uptime"]  = r.uptime_last_sec;
    obj["heapMin"] = r.free_heap_min;
    obj["reset"]   = reboot_reset_reason_str(r.reset_reason);
    obj["intent"]  = reboot_intent_str(r.intent);
    obj["class"]   = reboot_class_str(r.cls);
    obj["fw"]      = (const char*)r.fw_version;
    obj["detail"]  = (const char*)r.detail;
}

// GET /api/reboot/last -- newest record only (for the dashboard banner).
void get_reboot_last(AsyncWebServerRequest * request) {
    RebootRecord r;
    DynamicJsonDocument doc(512);
    if (reboot_log_get_last(&r)) {
        reboot_record_to_json(r, doc.to<JsonObject>());
    } else {
        // No history yet; return a stub so the frontend can render a neutral banner.
        JsonObject obj = doc.to<JsonObject>();
        obj["class"]  = reboot_class_str(REBOOT_CLASS_COLD);
        obj["intent"] = reboot_intent_str(REBOOT_INTENT_NONE);
    }
    String body; serializeJson(doc, body);
    AsyncWebServerResponse* resp = request->beginResponse(200, "application/json", body);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    request->send(resp);
}

// GET /api/reboot/list -- up to REBOOT_LOG_RING_SIZE records, newest first.
void get_reboot_list(AsyncWebServerRequest * request) {
    RebootRecord recs[REBOOT_LOG_RING_SIZE];
    size_t n = reboot_log_get_recent(recs, REBOOT_LOG_RING_SIZE);
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.to<JsonArray>();
    for (size_t i = 0; i < n; ++i) {
        reboot_record_to_json(recs[i], arr.createNestedObject());
    }
    String body; serializeJson(doc, body);
    AsyncWebServerResponse* resp = request->beginResponse(200, "application/json", body);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    request->send(resp);
}

// DELETE /api/reboot/list -- wipe stored history (boot index is preserved).
void delete_reboot_list(AsyncWebServerRequest * request) {
    reboot_log_clear();
    AsyncWebServerResponse* resp = request->beginResponse(200, "application/json", "{\"status\":\"ok\"}");
    resp->addHeader("Access-Control-Allow-Origin", "*");
    request->send(resp);
}

// ── Coredump endpoints (Plan B) ─────────────────────────────────────────────
//
// On panic / WDT the IDF panic handler writes an ELF coredump to the dedicated
// "coredump" partition (64 KB). These three endpoints let the user inspect
// (info), retrieve (download) and clear (delete) it from the web UI.
//
// JSON shape from /api/coredump/info:
//   {
//     "present": true,
//     "size": 12480,
//     "task":  "miner",
//     "pc":    1075871436,
//     "bt":    [1075871436, 1075869312, ...],
//     "btCorrupted": false,
//     "appSha256": "abc123..."
//   }
// When no dump (or corrupted): {"present": false}.

// GET /api/coredump/info -- presence + summary metadata.
void get_coredump_info(AsyncWebServerRequest * request) {
    DynamicJsonDocument doc(1024);
    JsonObject obj = doc.to<JsonObject>();
    size_t sz = 0;
    bool crc_ok = false;
    bool present = coredump_present(&sz, &crc_ok);
    obj["present"] = present;
    if (!present) {
        String body; serializeJson(doc, body);
        AsyncWebServerResponse* resp = request->beginResponse(200, "application/json", body);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        request->send(resp);
        return;
    }
    obj["size"]   = (uint32_t)sz;
    obj["crcOk"]  = crc_ok;   // false → dump may be truncated / corrupted

    coredump_summary_lite_t s;
    if (coredump_get_summary(&s) && s.valid) {
        obj["task"]         = (const char*)s.task;
        obj["pc"]           = s.pc;
        obj["btCorrupted"]  = s.bt_corrupted;
        obj["appSha256"]    = (const char*)s.app_sha256;
        JsonArray bt = obj.createNestedArray("bt");
        for (uint8_t i = 0; i < s.bt_depth; ++i) bt.add(s.bt[i]);
    }
    String body; serializeJson(doc, body);
    AsyncWebServerResponse* resp = request->beginResponse(200, "application/json", body);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    request->send(resp);
}

// DELETE /api/coredump -- erase the partition. Use only after the user has
// downloaded what they need; this is irreversible.
void delete_coredump(AsyncWebServerRequest * request) {
    bool ok = coredump_erase();
    AsyncWebServerResponse* resp = request->beginResponse(
        ok ? 200 : 500, "application/json",
        ok ? "{\"status\":\"ok\"}" : "{\"status\":\"error\"}");
    resp->addHeader("Access-Control-Allow-Origin", "*");
    request->send(resp);
}

// GET /api/update/progress -- returns current OTA/upload progress from g_board.status.ota.
// Used by the frontend to poll real device-side write progress for all upload types
// (firmware.bin, spiffs.bin, screensaver .gif).
void get_ota_progress(AsyncWebServerRequest* request) {
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"running\":%s,\"progress\":%d,\"filename\":\"%s\"}",
        g_board.status.ota.running ? "true" : "false",
        g_board.status.ota.progress,
        g_board.status.ota.filename.c_str());
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", buf);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}

// Unified upload handler for all file types:
//   *.gif  → SPIFFS file write (screensaver)  — uses g_board.status.ota for progress
//   *.bin  → OTA flash write (firmware/spiffs) — uses g_board.status.ota for progress
//
// Both paths update g_board.status.ota.{running, progress, filename} so the frontend
// can poll GET /api/update/progress to get accurate device-side write progress.
//
// POST /api/update/screensaver -- GIF screensaver (multipart field: "screensaver")
// POST /api/update/{firmware,spiffs} and legacy aliases -- firmware/SPIFFS OTA
void file_upload_handler(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
    // ── Shared statics (only one upload runs at a time) ──────────────────────
    // GIF path
    static File     gif_file;
    // OTA bin path: accumulate TCP segments into larger writes (2 flash sectors each)
    static const size_t OTA_WRITE_BUFFER_SIZE = 8192;
    static uint8_t  ota_buf[OTA_WRITE_BUFFER_SIZE];
    static size_t   ota_buf_len = 0;
    // Shared
    static int      lastPercentage = -1;
    static bool     is_gif = false;

    uint64_t flen = request->contentLength();

    if (index == 0) {
        // Detect file type by extension
        String lname = filename;
        lname.toLowerCase();
        is_gif = lname.endsWith(".gif");

        if (is_gif) {
            // ── GIF / screensaver init ─────────────────────────────────────
            String gif_name;
            if (g_board.info.spec.name == BOARD_NMAXE_NAME || g_board.info.spec.name == BOARD_NMAXE_GAMMA_NAME) {
                gif_name = "screen_saver_240x135.gif";
            } else {
                gif_name = "screen_saver_320x240.gif";
            }
            LOG_I("GIF upload started: %s -> /%s  total=%llu bytes", filename.c_str(), gif_name.c_str(), (unsigned long long)flen);
            gif_file = SPIFFS.open(("/" + gif_name).c_str(), "w");
            if (!gif_file) {
                LOG_E("GIF upload: failed to open /%s for writing", gif_name.c_str());
                request->send(500, "text/plain", "Failed to open file for writing.");
                return;
            }
            g_board.status.ota.running          = true;
            g_board.status.ota.progress         = 0;
            g_board.status.ota.filename         = filename;
            g_board.status.ota.last_progress_ms = millis();
            lastPercentage    = -1;
        } else {
            // ── Firmware / SPIFFS OTA init ────────────────────────────────
            LOG_I("OTA Update Started, File name: %s, Index: %d, contentLength: %llu, len: %d, Final: %d", filename.c_str(), index, flen, len, final);
            size_t bin_size   = UPDATE_SIZE_UNKNOWN;
            int    update_type = U_FLASH;

            if (filename == "firmware.bin") {
                bin_size    = UPDATE_SIZE_UNKNOWN;
                update_type = U_FLASH;
            } else if (filename == "spiffs.bin") {
                // Must use UPDATE_SIZE_UNKNOWN here. SPIFFS.totalBytes() returns the usable
                // filesystem capacity (after metadata overhead), which is smaller than the
                // actual partition size (0x380000 = 3.5MB). The generated spiffs.bin is sized
                // to the full partition, so using totalBytes() as the limit causes "Not Enough
                // Space" at ~91%. UPDATE_SIZE_UNKNOWN lets the Update library read the true
                // partition size from the partition table.
                bin_size    = UPDATE_SIZE_UNKNOWN;
                update_type = U_SPIFFS;
                // Mark SPIFFS as being updated. Cleared only on successful completion.
                // If the device reboots before that, file_system_init() will detect the
                // flag and force recovery mode — even if SPIFFS partially mounted.
                nvs_config_set_u8(NVS_CONFIG_SPIFFS_UPDATING, 1);
            }

            if (!Update.begin(bin_size, update_type)) {
                LOG_E("OTA Update error: %s", Update.errorString());
                String err_msg = String("OTA begin failed: ") + Update.errorString();
                request->send(500, "text/plain", err_msg);
                return;
            }
            g_board.status.ota.running          = true;
            g_board.status.ota.progress         = 0;
            g_board.status.ota.filename         = filename;
            g_board.status.ota.last_progress_ms = millis();
            ota_buf_len    = 0;
            lastPercentage = -1;
        }
    }

    if (is_gif) {
        // ── GIF write path ────────────────────────────────────────────────
        if (gif_file) {
            if (gif_file.write(data, len) != len) {
                LOG_E("GIF upload: write error at offset %u", (unsigned)index);
                gif_file.close();
                g_board.status.ota.running = false;
                request->send(500, "text/plain", "Write error.");
                return;
            }
            if (flen > 0) {
                int pct = (int)((index + len) * 100ULL / flen);
                if (pct != lastPercentage) {
                    g_board.status.ota.progress         = pct;
                    g_board.status.ota.last_progress_ms = millis();
                    LOG_I("GIF upload: %d%%  (%u / %llu bytes)", pct, (unsigned)(index + len), (unsigned long long)flen);
                    lastPercentage = pct;
                }
                delay(5); // Feed the watchdog
            }
        }
        if (final) {
            if (gif_file) gif_file.close();
            g_board.status.ota.running  = false;
            g_board.status.ota.progress = 100;
            LOG_I("GIF upload complete: %u bytes saved as %s", (unsigned)(index + len), filename.c_str());
            AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", "{\"status\":\"ok\"}");
            resp->addHeader("Access-Control-Allow-Origin", "*");
            request->send(resp);

            LOG_I("GIF upload finished, new screensaver will be used on next trigger without reboot");
        }
    } else {
        // ── OTA bin write path ────────────────────────────────────────────
        // Accumulate incoming data into ota_buf, flush when buffer is full or this is the last chunk.
        // OTA_WRITE_BUFFER_SIZE = 8192 bytes = 2 flash sectors per write call.
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
                    String err_msg = String("OTA write failed: ") + Update.errorString();
                    request->send(500, "text/plain", err_msg);
                    ota_buf_len = 0;
                    return;
                }
                ota_buf_len = 0;

                // vTaskDelay(1): block async_tcp for 1ms so IDLE task can reset the task watchdog.
                // taskYIELD() is insufficient  - it only yields to equal/higher-priority tasks,
                // and IDLE (priority 0) would never run.
                vTaskDelay(pdMS_TO_TICKS(1));

                int progress = (int)((index + offset) * 100.0 / flen);
                if (progress != lastPercentage) {
                    g_board.status.ota.progress         = progress;
                    g_board.status.ota.last_progress_ms = millis();
                    LOG_I("%s ota: %d%%", filename.c_str(), progress);
                    lastPercentage = progress;
                }
            }
        }

        if (final) {
            if (Update.end(true)) {
                // g_board.status.ota.running  = false;
                g_board.status.ota.progress = 100;
                LOG_I("%s ota: %d%%", filename.c_str(), g_board.status.ota.progress);
                // SPIFFS update completed successfully — clear the "in-progress" guard flag
                // so the next boot proceeds to normal mode instead of recovery mode.
                if (filename == "spiffs.bin") {
                    nvs_config_set_u8(NVS_CONFIG_SPIFFS_UPDATING, 0);
                }
                AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"status\":\"ok\"}");
                response->addHeader("Access-Control-Allow-Origin", "*");
                request->send(response);

                LOG_W("*************** Update Success: %u bytes, rebooting *************** ", index + len);
                // NOTE: delay() in async_tcp context blocks the TCP stack.
                //       daemon_thread_entry waits ~1s before restarting, giving time for the HTTP response to be sent.
                reboot_intent_set(REBOOT_INTENT_OTA_FINISHED, "uploaded %s (%u bytes)",
                                  filename.c_str(), (unsigned)(index + len));
                xSemaphoreGive(g_board.status.reboot_xsem);
            } else {
                LOG_E("OTA Update error: %s", Update.errorString());
                String err_msg = String("OTA end failed: ") + Update.errorString();
                request->send(500, "text/plain", err_msg);
            }
        }
    }
}

// WebSocket event dispatcher. Runs inside async_tcp task -- never call LOG macros here
// (they invoke webSocket.textAll() which would re-enter the callback).
void webSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    // NOTE: Do NOT use LOG_W/LOG_E etc. here  - those macros call webSocket.textAll(),
    //       which is re-entrant inside a WebSocket event callback. Use Serial.printf directly.
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("> [WS] client #%u connected from %s\r\n",
                          client->id(), client->remoteIP().toString().c_str());
            // A WebSocket connection is the most reliable signal that a user has opened the web UI.
            // Wake screensaver immediately so the page renders with a live display.
            // Safe to call here: g_board is a global and these are atomic bit ops.
            g_board.status.ui.last_active_ms = millis();
            xEventGroupClearBits(g_board.status.sys_evt, SYS_EVENT_SCREEN_SAVER_TRIGGERED);
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("> [WS] client #%u disconnected\r\n", client->id());
            break;
        case WS_EVT_DATA:
            // Echo text back (mirrors previous behaviour)
            if (len > 0 && data != nullptr) {
                client->text(data, len);
            }
            break;
        case WS_EVT_PONG:
            Serial.printf("> [WS] client #%u pong\r\n", client->id());
            break;
        case WS_EVT_ERROR:
            Serial.printf("> [WS] client #%u error\r\n", client->id());
            break;
    }
}
