
#include <Arduino.h>
#include <Update.h>
#include <SPIFFS.h>
#include "ArduinoJson.h"
#include "utils/logger/logger.h"
#include "http_server.h"
#include "web_ctx.h"
#include "app/system_events.h"
#include "ui/ui_manager.h"
#include "nvs/nvs_config.h"
#include "utils/helper.h"
#include "utils/reboot_log/reboot_log.h"
#include "utils/reboot_log/coredump.h"
#include "board/board.h"
#include "version.h"
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_mac.h>
#include <nvs.h>
#include <memory>
#include <math.h>

AsyncWebServer  webServer(80);
// AsyncWebSocket runs on the same port 80 at path /ws, no separate port needed.
AsyncWebSocket  webSocket("/ws");

// Module-global webserver context, set once at the start of webserver_thread_entry
// before any route is registered. The free-function request handlers read it.
WebCtx* g_web = nullptr;

// Highest safe auto-fan target for a fan: its over-temperature trip point
// (fan id 0 → ASIC limit, id 1 → Vcore/power limit) minus a safety margin.
static float fan_max_target_temp(uint8_t fan_id) {
    float limit = (fan_id == 1) ? g_web->spec->pwr.temp_limit.high
                                : g_web->spec->asic.temp_limit.high;
    return limit - FAN_TARGET_TEMP_MARGIN_C;
}

// Log a web config change to the serial console. Every config-modifying
// PATCH/POST handler calls this right after a successful JSON parse so the
// operator can see exactly what the web UI sent.
//
// Each top-level field is logged on its own line (key = value) instead of
// dumping the raw body, because the logger truncates a single line at 512
// bytes — a long stratum URL would otherwise cut off the trailing fields
// (e.g. asicVcoreReq / asicFreqReq).
//
// The client address is part of the line on purpose. A fan target was found
// changed on a test board with nobody claiming the edit, and there was no way to
// tell whether it came from the web UI, another session on the LAN or a script:
// the firmware only logged the new value. Every caller here already has the
// request, so the origin costs nothing to record.
static void log_web_config_change(const char* endpoint, const JsonObject& root,
                                  AsyncWebServerRequest* request = nullptr) {
    String ip;
    if (request != nullptr && request->client() != nullptr) {
        ip = request->client()->remoteIP().toString();
    }
    const char* from = (ip.length() > 0) ? ip.c_str() : "unknown";
    for (const auto& kv : root) {
        String val = kv.value().as<String>();
        LOG_W("[web] config change %s from %s: %s = %s", endpoint, from, kv.key().c_str(), val.c_str());
    }
}

struct OtaLastResultSnapshot {
    bool valid;
    bool success;
    bool reboot_pending;
    uint16_t http_status;
    uint32_t bytes;
    uint32_t ts_ms;
    char target[16];
    char filename[40];
    char detail[128];
};

static OtaLastResultSnapshot g_ota_last_result = {
    false, false, false, 0, 0, 0, "", "", ""
};

static const char* ota_target_from_filename(const String& filename) {
    if (filename == "firmware.bin") return "firmware";
    if (filename == "spiffs.bin")   return "spiffs";
    String lname = filename;
    lname.toLowerCase();
    if (lname.endsWith(".gif")) return "screensaver";
    return "unknown";
}

static void ota_last_result_set(bool success,
                                bool reboot_pending,
                                uint16_t http_status,
                                uint32_t bytes,
                                const String& filename,
                                const char* detail) {
    g_ota_last_result.valid = true;
    g_ota_last_result.success = success;
    g_ota_last_result.reboot_pending = reboot_pending;
    g_ota_last_result.http_status = http_status;
    g_ota_last_result.bytes = bytes;
    g_ota_last_result.ts_ms = millis();
    snprintf(g_ota_last_result.target, sizeof(g_ota_last_result.target), "%s", ota_target_from_filename(filename));
    snprintf(g_ota_last_result.filename, sizeof(g_ota_last_result.filename), "%s", filename.c_str());
    snprintf(g_ota_last_result.detail, sizeof(g_ota_last_result.detail), "%s", detail ? detail : "");
}

static void ota_begin_exclusive_ui_session() {
    if (!g_web || !g_web->sys_evt) return;
    xEventGroupClearBits(
        g_web->sys_evt,
        SYS_EVENT_FIND_NEIGHBOR_TRIGGERED |
        SYS_EVENT_SCREEN_SAVER_TRIGGERED
    );
}

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

static bool miner_state_is_paused_like(MinerRuntimeState state) {
    return state == MINER_RUNTIME_PAUSING ||
           state == MINER_RUNTIME_PAUSED ||
           state == MINER_RUNTIME_RESUMING ||
           state == MINER_RUNTIME_ERROR;
}

// Name of the hardware protection that is currently latched, or "none".
//
// This is deliberately independent of MinerRuntimeState: `paused` only describes a
// USER pause, so a unit that shut the rail down on overcurrent still reported
// paused=false next to 0.8 W of input power — the contradictory pair from the
// Nexus field report. Protection state comes from the sys_evt bits that
// power_loop sets when it takes the safety action, so the API can tell
// "protecting the hardware" apart from "mining" and "user-paused".
static const char* active_protection_name(void) {
    if (g_web->sys_evt == nullptr) return "none";
    EventBits_t bits = xEventGroupGetBits(g_web->sys_evt);
    if (bits & SYS_EVENT_POWER_OC_FAULT) return "overcurrent";
    if (bits & SYS_EVENT_POWER_OT_FAULT) return "overtemperature";
    return "none";
}

static void send_mining_state_response(AsyncWebServerRequest *request, uint16_t code = 200) {
    StaticJsonDocument<320> root;
    MinerRuntimeState state = g_web->status->runtime_state;
    bool paused = g_web->status->user_paused || miner_state_is_paused_like(state);

    root["status"] = (code >= 200 && code < 300) ? "ok" : "error";
    root["state"] = miner_runtime_state_to_string(state);
    root["paused"] = paused;
    root["reason"] = paused ? "user" : "";
    root["protection"] = active_protection_name();
    root["vcoreEnabled"] = !(state == MINER_RUNTIME_PAUSING || state == MINER_RUNTIME_PAUSED || state == MINER_RUNTIME_ERROR);

    String json_str;
    serializeJson(root, json_str);
    AsyncWebServerResponse *response = request->beginResponse(code, "application/json", json_str);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
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
    const uint16_t json_size_max = 1536;
    StaticJsonDocument<json_size_max> root;
    root.clear();

    // Power & electrical
    JsonObject powerObj = root.createNestedObject("power");
    powerObj["power"]   = (g_web->pwr->ibus / 1000.0f) * (g_web->pwr->vbus / 1000.0f);
    powerObj["vbus"]    = g_web->pwr->vbus;
    powerObj["ibus"]    = g_web->pwr->ibus;

    // Temperatures
    JsonObject tempObj  = root.createNestedObject("temps");
    tempObj["vcore"]    = g_web->temp->vcore;
    tempObj["asic"]     = g_web->temp->asic;

    // ASIC status
    JsonObject asicObj      = root.createNestedObject("asic");
    asicObj["count"]        = g_web->miner->get_asic_count();
    asicObj["model"]        = g_web->spec->asic.name;
    asicObj["vcoreReq"]     = g_web->spec->asic.req_vcore;
    asicObj["vcoreReal"]    = g_web->pwr->vcore;
    asicObj["freqReq"]      = g_web->spec->asic.req_frq;
    asicObj["smallCoreCnt"] = g_web->miner->get_asic_small_cores();

    // Mining stats
    JsonObject minerObj         = root.createNestedObject("miner");
    MinerRuntimeState miner_state = g_web->status->runtime_state;
    bool miner_paused = g_web->status->user_paused || miner_state_is_paused_like(miner_state);
    minerObj["state"]           = miner_runtime_state_to_string(miner_state);
    minerObj["paused"]          = miner_paused;
    minerObj["pauseReason"]     = miner_paused ? "user" : "";
    // Hardware protection latch, independent of the user-pause flags above.
    // "none" | "overcurrent" | "overtemperature". Lets an external monitor tell
    // "shut down by protection" apart from "still mining" without inferring it
    // from the power reading.
    minerObj["protection"]      = active_protection_name();
    minerObj["hashRate"]        = g_web->status->hashrate._3m / 1000 / 1000 / 1000;
    minerObj["bestDiffEver"]    = formatNumber(g_web->status->diff.best_ever, 4);
    minerObj["bestDiffSession"] = formatNumber(g_web->status->diff.best_session, 4);
    minerObj["networkDiff"]     = formatNumber(g_web->status->diff.network, 4);
    minerObj["poolDiff"]        = formatNumber(g_web->status->diff.pool, 4);
    minerObj["lastDiff"]        = formatNumber(g_web->status->diff.last, 4);
    minerObj["blkhits"]         = g_web->status->hits;
    minerObj["freeHeap"]        = ESP.getFreeHeap();
    minerObj["minFreeHeap"]     = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    minerObj["sAccepted"]       = g_web->status->share_accepted;
    minerObj["sRejected"]       = g_web->status->share_rejected;
    minerObj["uptimeSeconds"]   = g_web->status->uptime_session;
    minerObj["uptimeEver"]      = g_web->status->uptime_ever;
    minerObj["bmMode"]          = (*g_web->bm_mode);

    // Board identity
    JsonObject identityObj    = root.createNestedObject("identity");
    identityObj["fwVersion"]   = g_web->fw_version;
    identityObj["hwModel"]     = g_web->spec->name;
    identityObj["displayName"] = g_web->spec->display_name;
    identityObj["hostName"]    = g_web->wifi_cfg->hostname;
    identityObj["ssid"]       = g_web->wifi_cfg->sta_ssid;
    identityObj["rssi"]       = g_web->wifi->rssi;
    // Factory MAC of the station interface, uppercase colon-separated hex
    // ("FC:9E:53:4A:8B:43") - the form printed on the module label and used by
    // router DHCP client lists, so it can be matched by eye. Read straight from
    // eFuse rather than from the WiFi driver, so the field is present even if
    // the radio is down. Used by the dashboard to tell two otherwise identical
    // miners apart when their displayName/hostname were both left at the board
    // default.
    {
        uint8_t mac[6] = {0};
        if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
            char mac_str[18] = {0};
            snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            identityObj["mac"] = mac_str;
        } else {
            identityObj["mac"] = "";
        }
    }
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
    stratumObj["url"]  = g_web->conn->pool.use.ssl
        ? ("stratum+ssl://" + g_web->conn->pool.use.url + ":" + String(g_web->conn->pool.use.port))
        : ("stratum+tcp://" + g_web->conn->pool.use.url + ":" + String(g_web->conn->pool.use.port));
    stratumObj["user"] = g_web->conn->stratum.use.user;
    stratumObj["pwd"]  = g_web->conn->stratum.use.pwd;

    // Fan status
    JsonArray fansArray = root.createNestedArray("fans");
    for (auto & fan : (*g_web->fan_status)) {
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
    StaticJsonDocument<384> root;
    root.clear();
    root["hostName"] = g_web->wifi_cfg->hostname;
    root["mdnsName"] = g_web->wifi_cfg->mdns_name;
    root["ssid"]     = g_web->wifi_cfg->sta_ssid;
    root["status"]   = (g_web->wifi->status == WL_CONNECTED) ? "connected" : "disconnected";
    root["ip"]       = g_web->wifi->ip.toString();
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
        log_web_config_change("/api/setting/network", root.as<JsonObject>(), request);
        if (root.containsKey("ssid"))     nvs_config_set_string(NVS_CONFIG_WIFI_SSID, root["ssid"].as<String>().c_str());
        if (root.containsKey("wifiPass")) nvs_config_set_string(NVS_CONFIG_WIFI_PASS, root["wifiPass"].as<String>().c_str());
        if (root.containsKey("hostname")) {
            nvs_config_set_string(NVS_CONFIG_HOSTNAME, root["hostname"].as<String>().c_str());
            nvs_config_set_string(NVS_CONFIG_AP_SSID,  root["hostname"].as<String>().c_str());
            g_web->wifi_cfg->hostname                = root["hostname"].as<String>();
            g_web->wifi_cfg->ap_ssid = root["hostname"].as<String>();
        }
        if (root.containsKey("mdnsName")) {
            nvs_config_set_string(NVS_CONFIG_MDNS_NAME, root["mdnsName"].as<String>().c_str());
            g_web->wifi_cfg->mdns_name = root["mdnsName"].as<String>();
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    }
    free(buf);
}

// GET /api/setting/time -- timezone, time/date format.
void get_setting_time(AsyncWebServerRequest* request){
    StaticJsonDocument<256> root;
    root.clear();
    root["timeZone"]   = (*g_web->tz);
    root["timeFormat"] = g_web->time->format.time;
    root["dateFormat"] = g_web->time->format.date;
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
        log_web_config_change("/api/setting/time", root.as<JsonObject>(), request);
        if (root.containsKey("timezone")) {
            const String new_tz = root["timezone"].as<String>();
            const bool   tz_changed = (new_tz != *g_web->tz);
            nvs_config_set_string(NVS_CONFIG_TIMEZONE, new_tz.c_str());
            (*g_web->tz) = new_tz;
            // timeFormat/dateFormat are read live by the UI render loop, but the
            // TZ env (setenv/tzset) is only refreshed on NTP sync (hourly). Notify
            // the monitor thread to re-apply it now so the offset takes effect
            // within ~1s instead of after the next NTP sync / reboot.
            if (tz_changed && g_web->sys_evt) {
                xEventGroupSetBits(g_web->sys_evt, SYS_EVENT_TZ_CHANGED);
                LOG_I("Timezone changed -> monitor TZ re-apply requested");
            }
        }
        if (root.containsKey("timeFormat")) {
            uint8_t tf = root["timeFormat"].as<uint8_t>();
            tf = (tf == 12) ? 12 : 24;
            nvs_config_set_u8(NVS_CONFIG_TIME_FORMAT, tf);
            g_web->time->format.time = tf;
        }
        if (root.containsKey("dateFormat")) {
            nvs_config_set_string(NVS_CONFIG_DATE_FORMAT, root["dateFormat"].as<String>().c_str());
            g_web->time->format.date = root["dateFormat"].as<String>();
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    }
    free(buf);
}

// -- /api/setting/mining -- GET -----------------------------------------------
// Returns stratum config, current asic freq/vcore, and OC/VC dropdown options.
void get_setting_mining(AsyncWebServerRequest* request){
    const uint16_t json_size_max = 1024*4;
    StaticJsonDocument<json_size_max> root;
    root.clear();

    root["vcoreReq"] = g_web->spec->asic.req_vcore;
    root["freqReq"]  = g_web->spec->asic.req_frq;
    root["asic"]     = g_web->spec->asic.name;

    JsonObject stratumObj    = root.createNestedObject("stratum");
    JsonObject usedObj       = stratumObj.createNestedObject("used");
    usedObj["url"]  = g_web->conn->pool.use.ssl
        ? ("stratum+ssl://" + g_web->conn->pool.use.url + ":" + String(g_web->conn->pool.use.port))
        : ("stratum+tcp://" + g_web->conn->pool.use.url + ":" + String(g_web->conn->pool.use.port));
    usedObj["user"] = g_web->conn->stratum.use.user;
    usedObj["pwd"]  = g_web->conn->stratum.use.pwd;

    JsonObject primaryObj    = stratumObj.createNestedObject("primary");
    primaryObj["url"]  = g_web->conn->pool.primary.ssl
        ? ("stratum+ssl://" + g_web->conn->pool.primary.url + ":" + String(g_web->conn->pool.primary.port))
        : ("stratum+tcp://" + g_web->conn->pool.primary.url + ":" + String(g_web->conn->pool.primary.port));
    primaryObj["user"] = g_web->conn->stratum.primary.user;
    primaryObj["pwd"]  = g_web->conn->stratum.primary.pwd;

    JsonObject fallbackObj   = stratumObj.createNestedObject("fallback");
    fallbackObj["url"]  = g_web->conn->pool.fallback.ssl
        ? ("stratum+ssl://" + g_web->conn->pool.fallback.url + ":" + String(g_web->conn->pool.fallback.port))
        : ("stratum+tcp://" + g_web->conn->pool.fallback.url + ":" + String(g_web->conn->pool.fallback.port));
    fallbackObj["user"] = g_web->conn->stratum.fallback.user;
    fallbackObj["pwd"]  = g_web->conn->stratum.fallback.pwd;

    const std::vector<work_option_t>& oc_opts = g_web->spec->ui.setting_page.oc;
    const std::vector<work_option_t>& vc_opts = g_web->spec->ui.setting_page.vc;

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
        log_web_config_change("/api/setting/mining", root.as<JsonObject>(), request);
        // The operating point belongs to the sweep while a benchmark runs.
        //
        // Accepting this would be worse than refusing: the sweep re-applies its
        // own point on the very next round, so the user would see "Applied" and
        // then silently lose the setting, with the PLL bounced twice in a row on
        // the way. patch_mining_state already refuses pause/resume for the same
        // reason; this is the matching gate for the freq/vcore half.
        //
        // Pool credentials are deliberately NOT blocked: shares must keep flowing
        // during a sweep and changing the pool does not touch the ASIC.
        if ((*g_web->bm_mode) != 0 &&
            (root.containsKey("asicFreqReq") || root.containsKey("asicVcoreReq"))) {
            request->send(409, "application/json",
                "{\"error\":\"asic frequency/voltage cannot be changed while a benchmark is running - stop the benchmark first\"}");
            free(buf); return;
        }
        bool pool_changed = false;
        if (root.containsKey("stratum") && root["stratum"].is<JsonObject>()) {
            JsonObject stratum = root["stratum"].as<JsonObject>();
            // Validate URLs before storing so a malformed value never reaches NVS.
            auto check_url = [&](const char* key, const char* val) -> bool {
                pool_info_t tmp;
                if (!parse_pool_url(String(val), tmp)) {
                    LOG_W("Rejected invalid pool URL for %s: [%s]", key, val);
                    return false;
                }
                return true;
            };
            bool valid = true;
            const String old_url_pri = nvs_config_get_string_value(NVS_CONFIG_STRATUM_URL_PRIMARY,  PRIMARY_POOL_URL);
            const String old_url_fb  = nvs_config_get_string_value(NVS_CONFIG_STRATUM_URL_FALLBACK, FALLBACK_POOL_URL);
            if (stratum.containsKey("primary") && stratum["primary"].is<JsonObject>()) {
                JsonObject primary = stratum["primary"].as<JsonObject>();
                if (primary.containsKey("url")) {
                    const String url = primary["url"].as<String>();
                    if (!check_url("stratum.primary.url", url.c_str())) valid = false;
                    else if (url != old_url_pri) pool_changed = true;
                }
                if (primary.containsKey("user") && primary["user"].as<String>() != nvs_config_get_string_value(NVS_CONFIG_STRATUM_USER_PRIMARY, "")) pool_changed = true;
                if (primary.containsKey("pwd")  && primary["pwd"].as<String>()  != nvs_config_get_string_value(NVS_CONFIG_STRATUM_PASS_PRIMARY, "")) pool_changed = true;
            }
            if (stratum.containsKey("fallback") && stratum["fallback"].is<JsonObject>()) {
                JsonObject fallback = stratum["fallback"].as<JsonObject>();
                if (fallback.containsKey("url")) {
                    const String url = fallback["url"].as<String>();
                    if (!check_url("stratum.fallback.url", url.c_str())) valid = false;
                    else if (url != old_url_fb) pool_changed = true;
                }
                if (fallback.containsKey("user") && fallback["user"].as<String>() != nvs_config_get_string_value(NVS_CONFIG_STRATUM_USER_FALLBACK, "")) pool_changed = true;
                if (fallback.containsKey("pwd")  && fallback["pwd"].as<String>()  != nvs_config_get_string_value(NVS_CONFIG_STRATUM_PASS_FALLBACK, "")) pool_changed = true;
            }
            if (!valid) {
                request->send(400, "application/json", "{\"error\":\"invalid pool url\"}");
                free(buf); return;
            }
            // Report the REAL storage result. A failed nvs_set_str() used to be
            // swallowed, so the UI said "Saved" while flash kept the old pool and
            // the next boot restored it.
            esp_err_t   nvs_err     = ESP_OK;
            const char* nvs_err_key = "";
            auto save_pool_stratum = [&]() -> esp_err_t {
                nvs_err     = ESP_OK;
                nvs_err_key = "";
                auto put = [&](const char* key, const char* val) {
                    esp_err_t e = nvs_config_set_string(key, val);
                    if (e != ESP_OK) {
                        if (nvs_err == ESP_OK) { nvs_err = e; nvs_err_key = key; }
                        LOG_E("Pool config NOT saved: NVS write failed for '%s': %s",
                              key, esp_err_to_name(e));
                    }
                };
                if (stratum.containsKey("primary") && stratum["primary"].is<JsonObject>()) {
                    JsonObject primary = stratum["primary"].as<JsonObject>();
                    if (primary.containsKey("url"))  put(NVS_CONFIG_STRATUM_URL_PRIMARY,  primary["url"].as<String>().c_str());
                    if (primary.containsKey("user")) put(NVS_CONFIG_STRATUM_USER_PRIMARY, primary["user"].as<String>().c_str());
                    if (primary.containsKey("pwd"))  put(NVS_CONFIG_STRATUM_PASS_PRIMARY, primary["pwd"].as<String>().c_str());
                }
                if (stratum.containsKey("fallback") && stratum["fallback"].is<JsonObject>()) {
                    JsonObject fallback = stratum["fallback"].as<JsonObject>();
                    if (fallback.containsKey("url"))  put(NVS_CONFIG_STRATUM_URL_FALLBACK,  fallback["url"].as<String>().c_str());
                    if (fallback.containsKey("user")) put(NVS_CONFIG_STRATUM_USER_FALLBACK, fallback["user"].as<String>().c_str());
                    if (fallback.containsKey("pwd"))  put(NVS_CONFIG_STRATUM_PASS_FALLBACK, fallback["pwd"].as<String>().c_str());
                }
                return nvs_err;
            };
            if (save_pool_stratum() == ESP_ERR_NVS_NOT_ENOUGH_SPACE) {
                // The pool is the one setting a miner cannot live without, so before
                // giving up, reclaim benchmark result slots that no ring index points
                // at (invisible in the UI, still holding NVS entries - measured at
                // ~65% of the 24 KB partition on a long-lived board). Reachable rows
                // are never touched. Then retry once.
                const uint16_t freed = bm_result_sweep_orphans();
                if (freed) {
                    LOG_W("NVS full while saving pool config - reclaimed %u orphan benchmark slot(s), retrying",
                          (unsigned)freed);
                    save_pool_stratum();
                }
            }
            if (nvs_err != ESP_OK) {
                char msg[320];
                snprintf(msg, sizeof(msg),
                         "{\"error\":\"nvs_write_failed\",\"code\":%d,\"detail\":\"%s\",\"key\":\"%s\","
                         "\"hint\":\"NVS is full or fragmented - clear benchmark results / top-10 or factory reset, then retry\"}",
                         (int)nvs_err, esp_err_to_name(nvs_err), nvs_err_key);
                request->send(500, "application/json", msg);
                free(buf); return;
            }
            // Hot-reload: notify the stratum thread (it re-reads NVS and rebuilds
            // the pool connection itself; the web thread never touches ConnInfo).
            if (pool_changed && g_web->sys_evt) {
                xEventGroupSetBits(g_web->sys_evt, SYS_EVENT_POOL_CONFIG_CHANGED);
                LOG_I("Pool config changed -> stratum hot-reload requested");
            }
        }
        // Track whether the operating point actually moved: only then does it make
        // sense to discard latched status bits.
        bool op_point_changed = false;
        // Resolve both requested values (and their clamps) BEFORE touching
        // hardware, so the pair can be applied in a direction-safe order:
        // when the frequency goes DOWN the PLL must settle first, otherwise the
        // rail drops while the ASIC still hashes at the old higher frequency
        // (under-volted high-frequency run — same hazard as the benchmark
        // start path). When the frequency goes UP, the rail moves first.
        bool    vcore_req = root.containsKey("asicVcoreReq");
        bool    freq_req  = root.containsKey("asicFreqReq");
        uint16_t req_mv   = 0;
        uint16_t req_mhz  = 0;
        if (vcore_req) {
            req_mv = root["asicVcoreReq"].as<uint16_t>();
            // Clamp to the user-facing range (vc list max); asic.max_vcore is regulator
            // headroom for the power loop's line-loss PID, not a user-settable target.
            const uint16_t vc_min = g_web->spec->asic.min_vcore;
            const uint16_t vc_max = g_web->spec->user_vcore_max();
            if (vc_min > 0 && req_mv < vc_min) req_mv = vc_min;
            if (vc_max > 0 && req_mv > vc_max) req_mv = vc_max;
        }
        if (freq_req) {
            req_mhz = root["asicFreqReq"].as<uint16_t>();
            // Clamp to the selectable OC list range (first..last entry).
            const auto& oc_opts = g_web->spec->ui.setting_page.oc;
            if (!oc_opts.empty()) {
                if (req_mhz < oc_opts.front().value) req_mhz = oc_opts.front().value;
                if (req_mhz > oc_opts.back().value)  req_mhz = oc_opts.back().value;
            }
        }

        auto apply_vcore_req = [&]() {
            if (!vcore_req) return;
            if (req_mv != g_web->spec->asic.req_vcore) op_point_changed = true;
            g_web->spec->asic.req_vcore = req_mv;
            g_web->power->set_vcore_voltage(req_mv);
            nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, req_mv);
        };
        auto apply_freq_req = [&](bool await_pll) {
            if (!freq_req) return;
            bool ok;
            if (await_pll && g_web->miner != nullptr) {
                ok = g_web->miner->request_and_await_asic_frequency(req_mhz);
            } else {
                ok = g_web->miner != nullptr && g_web->miner->request_asic_frequency(req_mhz);
            }
            if (!ok) {
                LOG_W("ASIC frequency hot-switch %s failed: %uMHz", await_pll ? "down-switch" : "request", req_mhz);
            }
            if (req_mhz != g_web->spec->asic.req_frq) op_point_changed = true;
            g_web->spec->asic.req_frq = req_mhz;
            nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, req_mhz);
        };

        const uint16_t cur_freq_hw = g_web->miner ? g_web->miner->get_asic_frequency_current() : 0;
        if (vcore_req && freq_req && cur_freq_hw != 0 && req_mhz < cur_freq_hw) {
            // Down-shift: PLL first (awaited), rail second.
            apply_freq_req(true);
            apply_vcore_req();
        } else {
            apply_vcore_req();
            apply_freq_req(false);
        }
        // Discard latched STATUS_IOUT / STATUS_TEMPERATURE bits after the operating
        // point moves, so a WARN that fired at the old (hotter/faster) pair stops
        // being reported as if it were current. Symptom this fixes: switching away
        // from Turbo left power_loop logging "Overcurrent WARNING" every 3 s forever
        // at 26 A (ECO) / 43 A (Normal) — the warning bit is sticky and nothing ever
        // cleared it outside the miner init/reinit paths.
        //
        // GUARD: CLEAR_FAULTS clears ALL latched bits, including a genuine OC_FAULT
        // or OT_FAULT. Clearing those while the condition is still present would
        // silently disarm the protection, so this only runs when nothing is
        // actually faulting. power_loop owns the fault handling; here we only drop
        // stale warning history.
        if (op_point_changed && g_web->power != nullptr &&
            !g_web->power->is_oc_fault() && !g_web->power->is_ot_fault()) {
            g_web->power->clear_faults();
            LOG_I("Operating point changed -> cleared latched power status bits");
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
    json += (*g_web->coin_price);
    json += "\",\"coinWatchlist\":\"";
    json += (*g_web->coin_watchlist);
    json += "\",\"pairs\":[";
    if (g_web->market) {
        const char* buf = g_web->market->get_pairs_buffer();
        uint16_t    cnt = g_web->market->get_pairs_count();
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
        log_web_config_change("/api/setting/market", root.as<JsonObject>(), request);
        if (root.containsKey("mainprice")) {
            nvs_config_set_string(NVS_CONFIG_PRICE_DISPLAY_COIN, root["mainprice"].as<String>().c_str());
            (*g_web->coin_price) = root["mainprice"].as<String>();
            (*g_web->coin_price).toUpperCase();
        }
        if (root.containsKey("coinWatchlist")) {
            nvs_config_set_string(NVS_CONFIG_COIN_WATCHLIST, root["coinWatchlist"].as<String>().c_str());
            (*g_web->coin_watchlist) = root["coinWatchlist"].as<String>();
        }
        // Trigger an immediate market refresh so the new coin settings take effect at once.
        if (g_web->market) g_web->market->request_refresh();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    }
    free(buf);
}

// GET /api/setting/preference -- screen flip/brightness/auto-roll, fan speed, LED indicator.
void get_setting_preference(AsyncWebServerRequest* request){

    auto get_fan_config = [&](uint8_t fan_id) -> fan_config_t* {
        for(auto &fan_cfg : g_web->spec->fans){
            if(fan_cfg.id == fan_id){
                return &fan_cfg;
            }
        }
        return nullptr;
    };

    StaticJsonDocument<1024> root;
    root.clear();
    root["screenFlip"]        = g_web->pref->screen.flip;
    root["ledIndicator"]      = g_web->pref->led.enable;
    root["screenAutoRoll"]    = g_web->pref->screen.auto_rolling;
    root["Brightness"]        = g_web->pref->screen.brightness;
    root["screensaverEnable"] = g_web->pref->screen.saver_enable ? 1 : 0;
    root["screensaverTimeout"]= (uint32_t)g_web->pref->screen.saver_timeout;
    root["screensaverMode"]   = (uint8_t)g_web->pref->screen.saver_mode;
    root["hwModel"]           = g_web->spec->name;
    root["displayName"]       = g_web->spec->display_name;
    JsonArray fansArray    = root.createNestedArray("fans");
    for (auto & fan : (*g_web->fan_status)) {
        JsonObject fanObj = fansArray.createNestedObject();
        auto cfg = get_fan_config(fan.id); // ensure the fan config exists for this fan ID; if not, defaults will be used
        if(!cfg) continue;
        fanObj["id"]        = fan.id;
        fanObj["speed"]     = fan.speed;
        fanObj["rpm"]       = fan.rpm;
        fanObj["auto"]      = cfg->auto_speed;
        fanObj["target"]    = cfg->target_temp;
        fanObj["maxTarget"] = fan_max_target_temp(fan.id);
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
        log_web_config_change("/api/setting/preference", root.as<JsonObject>(), request);
        if (root.containsKey("Brightness")) {
            uint8_t brightness = (root["Brightness"].as<uint8_t>() <=1) ? 1 : ((root["Brightness"].as<uint8_t>() >= 100) ? 100 : root["Brightness"].as<uint8_t>());
            g_web->pref->screen.brightness = brightness;
            nvs_config_set_u8(NVS_CONFIG_SCREEN_BRIGHTNESS, brightness);
            xSemaphoreGive(g_web->brightness_update_xsem);
        }
        if (root.containsKey("ledIndicator")) {
            g_web->pref->led.enable = root["ledIndicator"].as<uint8_t>();
            g_web->pref->led.sleep  = false;
            nvs_config_set_u8(NVS_CONFIG_LED_INDICATOR, root["ledIndicator"].as<uint8_t>());
        }
        if (root.containsKey("screenAutoRoll")) {
            nvs_config_set_u8(NVS_CONFIG_AUTO_SCREEN, root["screenAutoRoll"].as<uint8_t>());
            g_web->pref->screen.auto_rolling = root["screenAutoRoll"].as<uint8_t>();
        }
        if (root.containsKey("screenFlip"))  {
            nvs_config_set_u8(NVS_CONFIG_FLIP_SCREEN, root["screenFlip"].as<uint8_t>());
        }
        if (root.containsKey("screensaverEnable"))  {
            nvs_config_set_u8(NVS_CONFIG_SCREEN_SAVER_ENABLE, root["screensaverEnable"].as<uint8_t>());
            g_web->pref->screen.saver_enable = root["screensaverEnable"].as<uint8_t>();
        }
        if (root.containsKey("screensaverTimeout"))  {
            nvs_config_set_u32(NVS_CONFIG_SCREEN_SAVER_TIMEOUT, root["screensaverTimeout"].as<uint32_t>());
            g_web->pref->screen.saver_timeout = root["screensaverTimeout"].as<uint32_t>();
        }
        if (root.containsKey("screensaverMode"))  {
            nvs_config_set_u8(NVS_CONFIG_SCREEN_SAVER_MODE, root["screensaverMode"].as<uint8_t>());
            g_web->pref->screen.saver_mode = root["screensaverMode"].as<uint8_t>();
        }
        // fans array: id=0 → ASIC fan, id=1 → Vcore fan
        if (root.containsKey("fans") && root["fans"].is<JsonArray>()) {
            // Whether the COOLING actually changed, which is the only thing that can
            // invalidate a soak verdict.
            //
            // This used to be "did the payload mention a fan field", and the web form
            // always submits the complete fans array (auto + target + speed), so every
            // save looked like a cooling change. Editing the screen brightness, the
            // screensaver or the LED therefore wiped every soak result and left ECO /
            // Turbo greyed out with "re-run benchmark" - for a user who never touched
            // the fans.
            //
            // Compare against the STORED policy instead, and only on the field that
            // actually drives the duty:
            //   auto ON  -> target temperature decides; the manual duty is ignored by
            //               the PID, so sending a different speed is not a change
            //   auto OFF -> the manual duty decides; the target is ignored
            bool policy_changed = false;
            // Boards with no controllable fan run a fixed duty; nothing here can alter
            // the cooling a soak was measured under.
            const bool fan_ctl = g_web->spec != nullptr && g_web->spec->fan_controllable;

            auto fan_policy_differs = [](bool auto_old, bool auto_new,
                                         float target_old, float target_new,
                                         uint16_t speed_old, uint16_t speed_new) -> bool {
                if (auto_old != auto_new) return true;
                if (auto_new) return fabsf(target_new - target_old) > 0.05f;
                return speed_old != speed_new;
            };

            for (JsonObject fan : root["fans"].as<JsonArray>()) {
                if (!fan.containsKey("id")) continue;
                uint8_t fan_id = fan["id"].as<uint8_t>();
                if (fan_id == 0) {
                    // Snapshot the stored policy BEFORE overwriting it.
                    const bool     auto_old   = g_web->spec->fans[0].auto_speed;
                    const float    target_old = g_web->spec->fans[0].target_temp;
                    const uint16_t speed_old  = nvs_config_get_u16(NVS_CONFIG_ASIC_FAN_SPEED,
                                                                   (*g_web->fan_status)[0].speed);
                    const bool     auto_new   = fan.containsKey("auto")
                                                  ? (fan["auto"].as<uint16_t>() != 0) : auto_old;
                    float          target_new = target_old;
                    if (fan.containsKey("target")) {
                        target_new = fan_clamp_target_temp(fan["target"].as<float>(),
                                                           g_web->spec->asic.temp_limit.high);
                    }
                    const uint16_t speed_new = fan.containsKey("speed")
                                                 ? fan["speed"].as<uint16_t>() : speed_old;
                    if (fan_ctl && fan_policy_differs(auto_old, auto_new, target_old, target_new,
                                                      speed_old, speed_new)) {
                        policy_changed = true;
                    }

                    if (fan.containsKey("auto")) {
                        nvs_config_set_u16(NVS_CONFIG_AUTO_ASIC_FAN_SPEED, fan["auto"].as<uint16_t>());
                        g_web->spec->fans[0].auto_speed = fan["auto"].as<uint16_t>();
                    }
                    if (fan.containsKey("target")) {
                        String t = String(target_new, 1);
                        nvs_config_set_string(NVS_CONFIG_ASIC_TARGET_TEMP, t.c_str());
                        g_web->spec->fans[0].target_temp = target_new;
                    }
                    if (fan.containsKey("speed")) {
                        nvs_config_set_u16(NVS_CONFIG_ASIC_FAN_SPEED, fan["speed"].as<uint16_t>());
                        (*g_web->fan_status)[0].speed = fan["speed"].as<uint16_t>();
                    }
                } else if (fan_id == 1) {
                    // Single-fan boards have no slot 1. Reject rather than index past
                    // the vector end - a client that sends id=1 anyway must not be able
                    // to fault the request handler.
                    if (g_web->spec->fans.size() < 2 || g_web->fan_status->size() < 2) continue;
                    const bool     auto_old   = g_web->spec->fans[1].auto_speed;
                    const float    target_old = g_web->spec->fans[1].target_temp;
                    const uint16_t speed_old  = nvs_config_get_u16(NVS_CONFIG_VCORE_FAN_SPEED,
                                                                   (*g_web->fan_status)[1].speed);
                    const bool     auto_new   = fan.containsKey("auto")
                                                  ? (fan["auto"].as<uint16_t>() != 0) : auto_old;
                    float          target_new = target_old;
                    if (fan.containsKey("target")) {
                        target_new = fan_clamp_target_temp(fan["target"].as<float>(),
                                                           g_web->spec->pwr.temp_limit.high);
                    }
                    const uint16_t speed_new = fan.containsKey("speed")
                                                 ? fan["speed"].as<uint16_t>() : speed_old;
                    if (fan_ctl && fan_policy_differs(auto_old, auto_new, target_old, target_new,
                                                      speed_old, speed_new)) {
                        policy_changed = true;
                    }

                    if (fan.containsKey("auto")) {
                        nvs_config_set_u16(NVS_CONFIG_AUTO_VCORE_FAN_SPEED, fan["auto"].as<uint16_t>());
                        g_web->spec->fans[1].auto_speed = fan["auto"].as<uint16_t>();
                    }
                    if (fan.containsKey("target")) {
                        String t = String(target_new, 1);
                        nvs_config_set_string(NVS_CONFIG_VCORE_TARGET_TEMP, t.c_str());
                        g_web->spec->fans[1].target_temp = target_new;
                    }
                    if (fan.containsKey("speed")) {
                        nvs_config_set_u16(NVS_CONFIG_VCORE_FAN_SPEED, fan["speed"].as<uint16_t>());
                        (*g_web->fan_status)[1].speed = fan["speed"].as<uint16_t>();
                    }
                }
            }
            // Soak verdicts describe the OLD cooling, so a real change invalidates them.
            // Unchanged settings keep them: they are still valid evidence.
            if (policy_changed) {
                LOG_W("Fan policy changed -> invalidating soak verification results");
                bm_result_clear_soak();
            }
        }

        request->send(200, "application/json", "{\"status\":\"ok\"}");
    }
    free(buf);
}

// GET /api/dashboard/topdiffs -- lifetime Top-10 best share difficulties,
// stored in NVS and kept sorted descending (index 0 = best). Replaces the old
// /api/dashboard/hr/dist hashrate-distribution endpoint (removed once HCN gave
// accurate hashrate, making the distribution histogram obsolete).
void get_top_diffs(AsyncWebServerRequest* request){
    // Pool ("host:port") + stratum user (wallet) snapshots per entry can push
    // the payload past a static buffer, so allocate the doc from PSRAM.
    // Worst case ~10*(47+95) chars + JSON overhead < 2 KB; 4 KB keeps margin.
    BasicJsonDocument<PsramJsonAllocator> root(4096);

    root.clear();
    JsonArray top = root.createNestedArray("top");
    for (int i = 0; i < MINER_TOP_DIFF_COUNT; i++) {
        top.add(formatNumber(g_web->status->best_top_diffs[i], 4));
    }
    // Achievement epoch (UTC seconds) per entry, index-aligned with "top";
    // 0 = unknown (pre-timestamp upgrade or pre-NTP share).
    JsonArray ts = root.createNestedArray("ts");
    for (int i = 0; i < MINER_TOP_DIFF_COUNT; i++) {
        ts.add(g_web->status->best_top_diff_ts[i]);
    }
    // Connection snapshot per entry, index-aligned with "top": pool the share
    // was mined on and stratum user (wallet[.worker]); "" = unknown
    // (pre-snapshot firmware upgrade).
    JsonArray pool = root.createNestedArray("pool");
    JsonArray user = root.createNestedArray("user");
    for (int i = 0; i < MINER_TOP_DIFF_COUNT; i++) {
        pool.add(g_web->status->best_top_diff_pool[i]);
        user.add(g_web->status->best_top_diff_user[i]);
    }
    // Pool-reported network difficulty at achievement time, formatted like
    // "top" (e.g. "90.12T"); "0" = unknown (pre-support upgrade). Comparing
    // it against "top" shows whether the share was a block-proximity hit.
    JsonArray net = root.createNestedArray("net");
    for (int i = 0; i < MINER_TOP_DIFF_COUNT; i++) {
        net.add(formatNumber(g_web->status->best_top_diff_net[i], 4));
    }

    String json_str;
    serializeJson(root, json_str);
    request->send(200, "application/json", json_str);
}

// DELETE /api/dashboard/topdiffs -- wipe the lifetime Top-10 share-difficulty
// history (RAM + NVS). Also clears NVS_CONFIG_BEST_EVER: on boot the Top-10
// list is seeded from bestever for firmware upgrades, so leaving it set would
// resurrect the old #1 entry after the next reboot. best_session is left
// untouched (session-scoped, resets on reboot anyway).
void delete_top_diffs(AsyncWebServerRequest* request){
    memset(g_web->status->best_top_diffs, 0, sizeof(g_web->status->best_top_diffs));
    memset(g_web->status->best_top_diff_ts, 0, sizeof(g_web->status->best_top_diff_ts));
    memset(g_web->status->best_top_diff_net, 0, sizeof(g_web->status->best_top_diff_net));
    memset(g_web->status->best_top_diff_pool, 0, sizeof(g_web->status->best_top_diff_pool));
    memset(g_web->status->best_top_diff_user, 0, sizeof(g_web->status->best_top_diff_user));
    g_web->status->diff.best_ever = 0.0;
    nvs_config_set_string(NVS_CONFIG_TOP_DIFFS, "");
    nvs_config_set_string(NVS_CONFIG_TOP_DIFF_TS, "");
    nvs_config_set_string(NVS_CONFIG_TOP_DIFF_NET, "");
    nvs_config_set_string(NVS_CONFIG_TOP_DIFF_SNAP, "");
    nvs_config_set_string(NVS_CONFIG_BEST_EVER, "0");
    LOG_I("Miner stats reset: lifetime Top-10 best difficulties cleared");
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// GET /api/dashboard/gauge/limits -- min/max ranges for dashboard power/heat/performance gauges.
void get_gauge_limits(AsyncWebServerRequest* request){
    const uint16_t json_size_max  =  1024;
    StaticJsonDocument<json_size_max> root = StaticJsonDocument<json_size_max>();

    root.clear();
    
    // Power limits
    JsonObject power = root.createNestedObject("power");
    JsonObject power_vbus = power.createNestedObject("vbus");
    power_vbus["min"] = g_web->spec->ui.dashboard_page.power.vbus.min;
    power_vbus["max"] = g_web->spec->ui.dashboard_page.power.vbus.max;
    
    JsonObject power_ibus = power.createNestedObject("ibus");
    power_ibus["min"] = g_web->spec->ui.dashboard_page.power.ibus.min;
    power_ibus["max"] = g_web->spec->ui.dashboard_page.power.ibus.max;
    
    JsonObject power_power = power.createNestedObject("power");
    power_power["min"] = g_web->spec->ui.dashboard_page.power.power.min;
    power_power["max"] = g_web->spec->ui.dashboard_page.power.power.max;
    
    // Heat limits
    JsonObject heat = root.createNestedObject("heat");
    JsonObject heat_asic = heat.createNestedObject("asic");
    heat_asic["min"] = g_web->spec->ui.dashboard_page.heat.asic.min;
    heat_asic["max"] = g_web->spec->ui.dashboard_page.heat.asic.max;
    
    JsonObject heat_vcore = heat.createNestedObject("vcore");
    heat_vcore["min"] = g_web->spec->ui.dashboard_page.heat.vcore.min;
    heat_vcore["max"] = g_web->spec->ui.dashboard_page.heat.vcore.max;
    
    JsonObject heat_fan = heat.createNestedObject("fan");
    heat_fan["min"] = g_web->spec->ui.dashboard_page.heat.fan.min;
    heat_fan["max"] = g_web->spec->ui.dashboard_page.heat.fan.max;
    
    // Performance limits
    JsonObject performance = root.createNestedObject("performance");
    JsonObject perf_asic_freq = performance.createNestedObject("asic_freq_req");
    perf_asic_freq["min"] = g_web->spec->ui.dashboard_page.performance.asic_freq_req.min;
    perf_asic_freq["max"] = g_web->spec->ui.dashboard_page.performance.asic_freq_req.max;
    
    JsonObject perf_vcore_req = performance.createNestedObject("vcore_req");
    perf_vcore_req["min"] = g_web->spec->ui.dashboard_page.performance.vcore_req.min;
    perf_vcore_req["max"] = g_web->spec->ui.dashboard_page.performance.vcore_req.max;
    
    JsonObject perf_vcore_measure = performance.createNestedObject("vcore_measure");
    perf_vcore_measure["min"] = g_web->spec->ui.dashboard_page.performance.vcore_measure.min;
    perf_vcore_measure["max"] = g_web->spec->ui.dashboard_page.performance.vcore_measure.max;

    String json_str;
    serializeJsonPretty(root, json_str);
    request->send(200, "application/json", json_str);
}

static bool append_jsonf(char* buffer, size_t capacity, size_t* used, const char* format, ...) {
    if (*used >= capacity) return false;

    va_list args;
    va_start(args, format);
    int written = vsnprintf(buffer + *used, capacity - *used, format, args);
    va_end(args);

    if (written < 0 || static_cast<size_t>(written) >= capacity - *used) return false;
    *used += static_cast<size_t>(written);
    return true;
}

// Format a float for JSON, writing the bare token "null" when the value is not
// finite. %.7g prints "nan"/"inf", neither of which is legal JSON, so a single
// failed sensor read would make the entire response unparseable on the client.
// This preserves the behaviour ArduinoJson had before this handler was rewritten
// to serialize directly.
static void json_float_or_null(char* out, size_t out_size, float value) {
    if (isfinite(value)) {
        snprintf(out, out_size, "%.7g", value);
    } else {
        snprintf(out, out_size, "null");
    }
}

// GET /api/dashboard/chart/history -- full miner status time-series (up to MAX_DATA_POINTS records).
// Snapshot fixed-size records under the history lock, then serialize directly
// into PSRAM. Avoid building a large ArduinoJson DOM in the async_tcp task:
// the DOM's slot chain was the confirmed crash site after long uptime.
void get_status_history(AsyncWebServerRequest* request){
    LOG_D("Starting status history request processing...");

    const size_t MAX_DATA_POINTS = 2000;

    int user_sample_interval = 10;
    if(request->hasParam("interval")) {
        user_sample_interval = request->getParam("interval")->value().toInt();
        if(user_sample_interval < 1) user_sample_interval = 1;
        if(user_sample_interval > 100) user_sample_interval = 100;
    }

    history_node_t* samples = static_cast<history_node_t*>(
        heap_caps_malloc(MAX_DATA_POINTS * sizeof(history_node_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!samples) {
        LOG_E("Failed to allocate history snapshot in PSRAM");
        request->send(500, "application/json", "{\"error\":\"PSRAM allocation failed for history snapshot\"}");
        return;
    }

    size_t sampled_count = 0;
    size_t actual_history_size = 0;
    int actual_sample_interval = user_sample_interval;

    if (xSemaphoreTake(g_web->status->status_history.mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        actual_history_size = g_web->status->status_history.deque.size();
        if (actual_history_size == 0) {
            xSemaphoreGive(g_web->status->status_history.mutex);
            heap_caps_free(samples);
            request->send(200, "application/json", "{\"statistics\":[],\"size\":0,\"sampledSize\":0}");
            return;
        }

        size_t estimated_samples = (actual_history_size + actual_sample_interval - 1) / actual_sample_interval;
        if (estimated_samples > MAX_DATA_POINTS) {
            actual_sample_interval = (actual_history_size + MAX_DATA_POINTS - 1) / MAX_DATA_POINTS;
        }

        size_t idx = 0;
        for (const auto& history : g_web->status->status_history.deque) {
            if ((idx % actual_sample_interval) == 0 && sampled_count < MAX_DATA_POINTS) {
                samples[sampled_count++] = history;
            }
            idx++;
        }
        xSemaphoreGive(g_web->status->status_history.mutex);
    } else {
        heap_caps_free(samples);
        LOG_E("Failed to acquire history mutex for data collection");
        request->send(500, "application/json", "{\"error\":\"Failed to access history data\"}");
        return;
    }

    const size_t json_capacity = 1024 + sampled_count * 320;
    char* json_buf = static_cast<char*>(
        heap_caps_malloc(json_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!json_buf) {
        heap_caps_free(samples);
        LOG_E("Failed to allocate %d bytes in PSRAM for JSON output", json_capacity);
        request->send(500, "application/json", "{\"error\":\"PSRAM allocation failed for response\"}");
        return;
    }

    size_t used = 0;
    const uint64_t timestamp_ms = (*g_web->utc) * 1000ULL;
    bool ok = append_jsonf(json_buf, json_capacity, &used,
                           "{\"timestamp\":%llu,\"labels\":[\"hashRate\",\"shareRate\",\"asicTemp\",\"vcoreTemp\",\"Pbus\",\"Vbus\",\"Ibus\",\"Vcore\",\"fanspeed\",\"fanrpm\",\"wifiRSSI\",\"freeHeap\",\"freePsram\",\"latency\",\"epoch\",\"fanCount\",\"fan1speed\",\"fan1rpm\"],\"statistics\":[",
                           static_cast<unsigned long long>(timestamp_ms));
    for (size_t i = 0; ok && i < sampled_count; i++) {
        const history_node_t& h = samples[i];
        // A temperature read can fail (the sensor HAL returns NAN on an I2C
        // error), and that NAN is stored in the history node. ArduinoJson wrote
        // such values as `null`; a plain "%.7g" would emit the literal `nan`,
        // which is not valid JSON and made the whole payload unparseable - one
        // bad row out of 474 was enough to blank the chart. Format through this
        // helper so non-finite values keep the original `null` contract.
        char f_hashrate[16], f_share[16], f_asic[16], f_vcore_t[16];
        char f_pbus[16], f_vbus[16], f_ibus[16];
        json_float_or_null(f_hashrate, sizeof(f_hashrate), h.hashrate);
        json_float_or_null(f_share,    sizeof(f_share),    h.share_rate);
        json_float_or_null(f_asic,     sizeof(f_asic),     h.asic_temp);
        json_float_or_null(f_vcore_t,  sizeof(f_vcore_t),  h.vcore_temp);
        json_float_or_null(f_pbus,     sizeof(f_pbus),     h.pbus);
        json_float_or_null(f_vbus,     sizeof(f_vbus),     h.vbus);
        json_float_or_null(f_ibus,     sizeof(f_ibus),     h.ibus);

        ok = append_jsonf(json_buf, json_capacity, &used,
                          "%s[%s,%s,%s,%s,%s,%s,%s,%u,%u,%u,%d,%u,%u,%u,%llu,%u,%u,%u]",
                          i == 0 ? "" : ",", f_hashrate, f_share, f_asic, f_vcore_t,
                          f_pbus, f_vbus, f_ibus, static_cast<unsigned>(h.vcore),
                          static_cast<unsigned>(h.fan_speed[0]), static_cast<unsigned>(h.fan_rpm[0]),
                          static_cast<int>(h.wifi_rssi), static_cast<unsigned>(h.free_ram),
                          static_cast<unsigned>(h.free_psram), static_cast<unsigned>(h.latency),
                          static_cast<unsigned long long>(h.epoch), static_cast<unsigned>(h.fan_count),
                          static_cast<unsigned>(h.fan_speed[1]), static_cast<unsigned>(h.fan_rpm[1]));
    }
    heap_caps_free(samples);

    ok = ok && append_jsonf(json_buf, json_capacity, &used,
                            "],\"size\":%u,\"sampledSize\":%u,\"sampleInterval\":%d,\"userRequestedInterval\":%d,\"maxDataPoints\":%u}",
                            static_cast<unsigned>(actual_history_size), static_cast<unsigned>(sampled_count),
                            actual_sample_interval, user_sample_interval, static_cast<unsigned>(MAX_DATA_POINTS));
    if (!ok) {
        heap_caps_free(json_buf);
        LOG_E("History JSON exceeded %d-byte PSRAM buffer", json_capacity);
        request->send(500, "application/json", "{\"error\":\"History response serialization overflow\"}");
        return;
    }

    const size_t json_len = used;
    std::shared_ptr<char> response_buf(json_buf, [](char* ptr) {
        heap_caps_free(ptr);
    });
    AsyncWebServerResponse *response = request->beginResponse(
        "application/json", json_len,
        [response_buf, json_len](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
            size_t remaining = json_len - index;
            size_t toWrite = (remaining < maxLen) ? remaining : maxLen;
            memcpy(buffer, response_buf.get() + index, toWrite);
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
    uint32_t json_size_max = 1536; // in bytes — 18 labels + one 18-column row
    
    // Use local document instead of static to prevent memory leaks
    BasicJsonDocument<PsramJsonAllocator> root(json_size_max);

    uint64_t ms = (*g_web->utc)*1000ULL;
    root["timestamp"] = ms;
    JsonArray labels = root.createNestedArray("labels");
    labels.add("hashRate");
    labels.add("shareRate");
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
    // Same fixed 18-column schema as /chart/history — see get_status_history().
    labels.add("fanCount");
    labels.add("fan1speed");
    labels.add("fan1rpm");
    
    JsonArray data = root.createNestedArray("statistics");
    
    // Protect status_history access with mutex
    if (xSemaphoreTake(g_web->status->status_history.mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        if (!g_web->status->status_history.deque.empty()) {
            auto& history = g_web->status->status_history.deque.back();
            JsonArray dataPoint = data.createNestedArray();
            dataPoint.add(history.hashrate);           // hashRate (GH/s)
            dataPoint.add(history.share_rate);         // shareRate (nonces/s)
            dataPoint.add(history.asic_temp);          // asic_temp (°C)
            dataPoint.add(history.vcore_temp);         // vcore_temp (°C)
            dataPoint.add(history.pbus);               // power (W)
            dataPoint.add(history.vbus);               // voltage (V)
            dataPoint.add(history.ibus);               // current (A)
            dataPoint.add(history.vcore);              // coreVoltageActual (mV)
            dataPoint.add(history.fan_speed[0]);       // fanspeed (%) — ASIC fan
            dataPoint.add(history.fan_rpm[0]);         // fanrpm (RPM)   — ASIC fan
            dataPoint.add(history.wifi_rssi);          // wifiRSSI (dBm)
            dataPoint.add(history.free_ram);           // freeHeap (KB)
            dataPoint.add(history.free_psram);         // freePsram (KB)
            dataPoint.add(history.latency);            // latency (ms)
            dataPoint.add(history.epoch);              // timestamp (ms)
            dataPoint.add(history.fan_count);          // fanCount (1-2)
            dataPoint.add(history.fan_speed[1]);       // fan1speed (%) — Vcore fan
            dataPoint.add(history.fan_rpm[1]);         // fan1rpm (RPM)   — Vcore fan
        }
        xSemaphoreGive(g_web->status->status_history.mutex);
    }
    String json_str;
    serializeJson(root, json_str);
    request->send(200, "application/json", json_str);

    LOG_D("Status realtime sent, history size: %d...", g_web->status->status_history.deque.size());
}

// GET /api/dashboard/luck/history -- block-proximity history for luck/difficulty chart.
void get_lucky_history(AsyncWebServerRequest* request){
    LOG_D("Starting lucky history request processing...");
    
    // Safely check history data size with mutex protection
    size_t history_size = 0;
    if (xSemaphoreTake(g_web->status->proximity_history.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        history_size = g_web->status->proximity_history.deque.size();
        xSemaphoreGive(g_web->status->proximity_history.mutex);
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
    
    LOG_D("Creating PSRAM lucky JSON document with %dKB for %d samples", json_size_max/1024, history_size);
    
    // Create JSON document in PSRAM
    BasicJsonDocument<PsramJsonAllocator> root(json_size_max);
    
    // Build JSON structure
    uint64_t ms = (*g_web->utc) * 1000ULL;
    root["timestamp"] = ms;
    JsonArray labels = root.createNestedArray("labels");
    labels.add("proximity");
    labels.add("share_diff");
    labels.add("net_diff");
    labels.add("epoch");
    
    JsonArray data = root.createNestedArray("statistics");
    
    size_t sampled_count = 0;
    
    // Acquire mutex for history traversal
    if (xSemaphoreTake(g_web->status->proximity_history.mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        LOG_D("Starting data collection: %d total records", history_size);
        
        for (const auto& history : g_web->status->proximity_history.deque) {
            JsonArray dataPoint = data.createNestedArray();
            
            dataPoint.add(history.block_proximity);
            dataPoint.add(history.share_diff);
            dataPoint.add(history.net_diff);
            dataPoint.add(history.epoch);
            sampled_count++;
            // Yield every 100 samples to prevent watchdog timeout
            if (sampled_count % 100 == 0) taskYIELD();
        }
        
        xSemaphoreGive(g_web->status->proximity_history.mutex);
        LOG_D("Data collection completed: %d samples from %d total records", sampled_count, history_size);
    } else {
        LOG_E("Failed to acquire history mutex for data collection");
        request->send(500, "application/json", "{\"error\":\"Failed to access history data\"}");
        return;
    }
    
    // Add metadata
    root["size"] = sampled_count;
    
    // Serialize directly into a PSRAM buffer to avoid a large internal-heap String.
    size_t json_size = measureJson(root);
    if (json_size == 0) {
        LOG_E("JSON serialization failed");
        request->send(500, "application/json", "{\"error\":\"JSON serialization failed\"}");
        return;
    }
    char* json_buf = (char*)heap_caps_malloc(json_size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!json_buf) {
        LOG_E("Failed to allocate %d bytes in PSRAM for lucky JSON output", json_size + 1);
        request->send(500, "application/json", "{\"error\":\"PSRAM allocation failed for response\"}");
        return;
    }
    serializeJson(root, json_buf, json_size + 1);
    root.clear();
    
    // Send response
    AsyncWebServerResponse *response = request->beginResponse(
        "application/json", json_size,
        [json_buf, json_size](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
            size_t remaining = json_size - index;
            size_t toWrite = (remaining < maxLen) ? remaining : maxLen;
            memcpy(buffer, json_buf + index, toWrite);
            if (index + toWrite >= json_size) {
                heap_caps_free(json_buf);
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
    
    BasicJsonDocument<PsramJsonAllocator> root(1024), colors_json(512);
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

    BasicJsonDocument<PsramJsonAllocator> root(1024*2);
    DeserializationError error = deserializeJson(root, data);
    if(error){
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }
    if(!root.is<JsonObject>()){
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }
    log_web_config_change("/api/theme", root.as<JsonObject>(), request);

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

// ── Benchmark API endpoints ──────────────────────────────────────────────────
//
// GET  /api/benchmark         — config + status + results
// PATCH /api/benchmark        — update config parameters (does NOT start)
// POST /api/benchmark/start   — write params, set bm_mode=1 (sweep starts in place, no reboot)
// POST /api/benchmark/stop    — set bm_mode=0 (sweep aborts, Normal point restored, no reboot)
// DELETE /api/benchmark/results — clear stored results

// GET /api/benchmark
void get_benchmark(AsyncWebServerRequest* request){
    char *result_json = bm_result_read_all();
    // Board-specific default sweep range.
    // Freq : iterate (same pattern as get_setting_mining) to avoid new front()/back() instantiations.
    // Vcore: read from asic struct integers directly — no vector access needed.
    uint16_t oc_min_def = 400, oc_max_def = 625;
    { bool first = true;
      for (const auto& o : g_web->spec->ui.setting_page.oc) {
          if (first) { oc_min_def = o.value; first = false; }
          oc_max_def = o.value;
      } }
    uint16_t vc_min_def = g_web->spec->asic.min_vcore ? g_web->spec->asic.min_vcore : 1000;
    // User-facing ceiling (vc list max), not asic.max_vcore (regulator headroom).
    uint16_t vc_max_def = g_web->spec->user_vcore_max() ? g_web->spec->user_vcore_max() : 1300;
    uint16_t bm_fstep_def = g_web->spec->asic.bm_freq_step ? g_web->spec->asic.bm_freq_step : 25;
    uint16_t bm_vstep_def = g_web->spec->asic.bm_vcore_step ? g_web->spec->asic.bm_vcore_step : 25;
    AsyncResponseStream *resp = request->beginResponseStream("application/json");
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->print("{");
    resp->printf("\"mode\":%d,",         nvs_config_get_u8 (NVS_CONFIG_BM_MODE,        0));
    resp->printf("\"freqMin\":%d,",      nvs_config_get_u16(NVS_CONFIG_BM_FREQ_MIN,    oc_min_def));
    resp->printf("\"freqMax\":%d,",      nvs_config_get_u16(NVS_CONFIG_BM_FREQ_MAX,    oc_max_def));
    resp->printf("\"freqStep\":%d,",     nvs_config_get_u16(NVS_CONFIG_BM_FREQ_STEP,   bm_fstep_def));
    resp->printf("\"vcoreMin\":%d,",     nvs_config_get_u16(NVS_CONFIG_BM_VCORE_MIN,   vc_min_def));
    resp->printf("\"vcoreMax\":%d,",     nvs_config_get_u16(NVS_CONFIG_BM_VCORE_MAX,   vc_max_def));
    resp->printf("\"vcoreStep\":%d,",    nvs_config_get_u16(NVS_CONFIG_BM_VCORE_STEP,  bm_vstep_def));
    resp->printf("\"sampleIntv\":%d,",   nvs_config_get_u8 (NVS_CONFIG_BM_SAMPLE_INTV, 2));
    resp->printf("\"bmTime\":%d,",       nvs_config_get_u16(NVS_CONFIG_BM_TIME,        120));
    resp->printf("\"stabTime\":%d,",     nvs_config_get_u16(NVS_CONFIG_BM_STAB_TIME,   30));
    resp->printf("\"curFreq\":%d,",      nvs_config_get_u16(NVS_CONFIG_BM_CUR_FREQ,    0));
    resp->printf("\"curVcore\":%d,",     nvs_config_get_u16(NVS_CONFIG_BM_CUR_VCORE,   0));
    resp->printf("\"startTs\":%lu,",     (unsigned long)nvs_config_get_u32(NVS_CONFIG_BM_START_TS,  0));
    resp->printf("\"totalSec\":%lu,",    (unsigned long)nvs_config_get_u32(NVS_CONFIG_BM_TOTAL_SEC, 0));
    resp->printf("\"results\":%s",       result_json);
    resp->print("}");
    free(result_json);
    request->send(resp);
}

// PATCH /api/benchmark
void patch_benchmark(AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total){
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
        log_web_config_change("/api/benchmark", root.as<JsonObject>(), request);
        if (root.containsKey("freqMin"))    nvs_config_set_u16(NVS_CONFIG_BM_FREQ_MIN,    root["freqMin"].as<uint16_t>());
        if (root.containsKey("freqMax"))    nvs_config_set_u16(NVS_CONFIG_BM_FREQ_MAX,    root["freqMax"].as<uint16_t>());
        if (root.containsKey("freqStep"))   nvs_config_set_u16(NVS_CONFIG_BM_FREQ_STEP,   root["freqStep"].as<uint16_t>());
        if (root.containsKey("vcoreMin"))   nvs_config_set_u16(NVS_CONFIG_BM_VCORE_MIN,   root["vcoreMin"].as<uint16_t>());
        if (root.containsKey("vcoreMax"))   nvs_config_set_u16(NVS_CONFIG_BM_VCORE_MAX,   root["vcoreMax"].as<uint16_t>());
        if (root.containsKey("vcoreStep"))  nvs_config_set_u16(NVS_CONFIG_BM_VCORE_STEP,  root["vcoreStep"].as<uint16_t>());
        if (root.containsKey("sampleIntv")) nvs_config_set_u8 (NVS_CONFIG_BM_SAMPLE_INTV, root["sampleIntv"].as<uint8_t>());
        if (root.containsKey("bmTime"))     nvs_config_set_u16(NVS_CONFIG_BM_TIME,         root["bmTime"].as<uint16_t>());
        if (root.containsKey("stabTime"))   nvs_config_set_u16(NVS_CONFIG_BM_STAB_TIME,   root["stabTime"].as<uint16_t>());
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    }
    free(buf);
}

// POST /api/benchmark/start
void post_benchmark_start(AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total){
    static const uint16_t BUF_SIZE = 512;
    if (total >= BUF_SIZE) { request->send(400, "application/json", "{\"error\":\"payload too large\"}"); return; }
    char *buf = (char*)heap_caps_malloc(BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { request->send(500, "application/json", "{\"error\":\"oom\"}"); return; }
    memset(buf, 0, BUF_SIZE);
    if (index + len < BUF_SIZE) memcpy(buf + index, data, len);
    if (index + len == total) {
        buf[total] = '\0';
        // An OTA is writing the flash partition and will reboot the device, so a
        // sweep started now would be killed mid-round and could race the flash
        // erase. Refuse instead of queueing: there is nothing to resume into.
        if (g_web->ota != nullptr && g_web->ota->running) {
            request->send(409, "application/json",
                "{\"error\":\"an OTA update is in progress - benchmark cannot start until it finishes\"}");
            free(buf); return;
        }
        // Optional: accept params in the start body to update them before starting.
        // Also supports {"resume":true} to continue from the last stopped position.
        bool do_resume = false;
        if (total > 0) {
            StaticJsonDocument<512> root;
            if (!deserializeJson(root, buf) && root.is<JsonObject>()) {
                if (root.containsKey("resume"))     do_resume = root["resume"].as<bool>();
                if (root.containsKey("freqMin"))    nvs_config_set_u16(NVS_CONFIG_BM_FREQ_MIN,    root["freqMin"].as<uint16_t>());
                if (root.containsKey("freqMax"))    nvs_config_set_u16(NVS_CONFIG_BM_FREQ_MAX,    root["freqMax"].as<uint16_t>());
                if (root.containsKey("freqStep"))   nvs_config_set_u16(NVS_CONFIG_BM_FREQ_STEP,   root["freqStep"].as<uint16_t>());
                if (root.containsKey("vcoreMin"))   nvs_config_set_u16(NVS_CONFIG_BM_VCORE_MIN,   root["vcoreMin"].as<uint16_t>());
                if (root.containsKey("vcoreMax"))   nvs_config_set_u16(NVS_CONFIG_BM_VCORE_MAX,   root["vcoreMax"].as<uint16_t>());
                if (root.containsKey("vcoreStep"))  nvs_config_set_u16(NVS_CONFIG_BM_VCORE_STEP,  root["vcoreStep"].as<uint16_t>());
                if (root.containsKey("sampleIntv")) nvs_config_set_u8 (NVS_CONFIG_BM_SAMPLE_INTV, root["sampleIntv"].as<uint8_t>());
                if (root.containsKey("bmTime"))     nvs_config_set_u16(NVS_CONFIG_BM_TIME,         root["bmTime"].as<uint16_t>());
                if (root.containsKey("stabTime"))   nvs_config_set_u16(NVS_CONFIG_BM_STAB_TIME,   root["stabTime"].as<uint16_t>());
            }
        }
        if (!do_resume) {
            // Fresh start: reset sweep state to first round
            uint16_t freq_min  = nvs_config_get_u16(NVS_CONFIG_BM_FREQ_MIN,  400);
            uint16_t vcore_min = nvs_config_get_u16(NVS_CONFIG_BM_VCORE_MIN, 1000);
            nvs_config_set_u16(NVS_CONFIG_BM_CUR_FREQ,  freq_min);
            nvs_config_set_u16(NVS_CONFIG_BM_CUR_VCORE, vcore_min);
            nvs_config_set_u32(NVS_CONFIG_BM_START_TS,  (uint32_t)time(nullptr));
            nvs_config_set_u32(NVS_CONFIG_BM_TOTAL_SEC, 0);
            LOG_W("[BM] Benchmark fresh start via API: freq_min=%d vcore_min=%d", freq_min, vcore_min);
        } else {
            // Resume: keep cur_freq / cur_vcore / start_ts from the paused run
            LOG_W("[BM] Benchmark resume via API: cur_freq=%d cur_vcore=%d",
                  nvs_config_get_u16(NVS_CONFIG_BM_CUR_FREQ, 0),
                  nvs_config_get_u16(NVS_CONFIG_BM_CUR_VCORE, 0));
        }
        nvs_config_set_u8(NVS_CONFIG_BM_MODE, 1);
        *g_web->bm_mode = 1;   // live flag: the resident benchmark thread picks it up within ~1s

        LOG_W("[BM] Benchmark mode=1 set (resume=%d)", (int)do_resume);
        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"status\":\"ok\",\"message\":\"benchmark starting\"}");
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
    }
    free(buf);
}

// POST /api/benchmark/stop
void post_benchmark_stop(AsyncWebServerRequest* request){
    nvs_config_set_u8 (NVS_CONFIG_BM_MODE, 0);
    *g_web->bm_mode = 0;   // benchmark thread abandons the round and restores the Normal point
    nvs_config_set_u32(NVS_CONFIG_BM_TOTAL_SEC, 0);  // discard partial run time
    LOG_W("[BM] Benchmark stopped via API");
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"status\":\"ok\",\"message\":\"benchmark stopped\"}");
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}

// DELETE /api/benchmark/results[?freq=NNN]
//
// Without a parameter: clears ALL results and resets the sweep configuration so
// the defaults revert to the board values (the historical behaviour).
//
// With ?freq=NNN: removes just that frequency's row. Results are keyed by
// frequency alone, so one row is exactly one entry — the sweep config, the
// progress checkpoint and every other stored point are left untouched. This is
// the "delete this row" button in the results table.
void delete_benchmark_results(AsyncWebServerRequest* request){
    if (request->hasParam("freq")) {
        const uint16_t freq = (uint16_t)request->getParam("freq")->value().toInt();
        if (freq == 0) {
            request->send(400, "application/json", "{\"error\":\"invalid freq\"}");
            return;
        }
        esp_err_t err = bm_result_delete_freq(freq);
        if (err == ESP_ERR_NOT_FOUND) {
            request->send(404, "application/json", "{\"error\":\"no result for that frequency\"}");
            return;
        }
        if (err != ESP_OK) {
            request->send(500, "application/json", "{\"error\":\"nvs delete failed\"}");
            return;
        }
        LOG_W("[BM] Single result deleted via API: freq=%u", freq);
        AsyncWebServerResponse *response = request->beginResponse(200, "application/json",
            "{\"status\":\"ok\",\"message\":\"result deleted\"}");
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
        return;
    }

    // Report erasures that failed instead of a blind "ok". Deleting a key needs a
    // free NVS entry (the tombstone), so a nearly full partition can refuse - that
    // is exactly how 66 result rows survived a "Clear Results" and kept the pool
    // config unwritable.
    const uint16_t stuck = bm_result_clear();
    nvs_config_delete_key(NVS_CONFIG_BM_FREQ_MIN);
    nvs_config_delete_key(NVS_CONFIG_BM_FREQ_MAX);
    nvs_config_delete_key(NVS_CONFIG_BM_FREQ_STEP);
    nvs_config_delete_key(NVS_CONFIG_BM_VCORE_MIN);
    nvs_config_delete_key(NVS_CONFIG_BM_VCORE_MAX);
    nvs_config_delete_key(NVS_CONFIG_BM_VCORE_STEP);
    nvs_config_delete_key(NVS_CONFIG_BM_SAMPLE_INTV);
    nvs_config_delete_key(NVS_CONFIG_BM_TIME);
    nvs_config_delete_key(NVS_CONFIG_BM_STAB_TIME);
    nvs_config_delete_key(NVS_CONFIG_BM_CUR_FREQ);
    nvs_config_delete_key(NVS_CONFIG_BM_CUR_VCORE);
    nvs_config_delete_key(NVS_CONFIG_BM_START_TS);
    nvs_config_delete_key(NVS_CONFIG_BM_TOTAL_SEC);
    if (stuck) {
        char msg[192];
        snprintf(msg, sizeof(msg),
                 "{\"error\":\"nvs_delete_failed\",\"detail\":\"ESP_ERR_NVS_NOT_ENOUGH_SPACE\",\"rows_left\":%u,"
                 "\"hint\":\"deleting needs a free NVS entry - free space first, then retry\"}",
                 (unsigned)stuck);
        request->send(500, "application/json", msg);
        return;
    }
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"status\":\"ok\"}");
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}

// POST /api/benchmark/apply
// Saves a specific freq+vcore as the Normal-mode ASIC settings and hot-switches
// to them immediately (no reboot) — in Normal mode this mirrors
// PATCH /api/setting/mining; in Benchmark mode the sweep thread sees bm_mode==0,
// abandons the current round, and its restore-to-Normal converges to the same
// point just written here.
// Response: {"status":"ok","reboot":false}
void post_benchmark_apply(AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total){
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
        uint16_t freq  = root["freq"]  | (uint16_t)0;
        uint16_t vcore = root["vcore"] | (uint16_t)0;
        if (freq == 0 || vcore == 0) {
            request->send(400, "application/json", "{\"error\":\"missing freq or vcore\"}");
            free(buf); return;
        }
        // Clamp vcore to board safety limits (user-facing: vc list max; the regulator
        // headroom in asic.max_vcore is reserved for the power loop's line-loss PID).
        uint16_t vcore_min = g_web->spec->asic.min_vcore;
        uint16_t vcore_max = g_web->spec->user_vcore_max();
        if (vcore_min > 0 && vcore < vcore_min) vcore = vcore_min;
        if (vcore_max > 0 && vcore > vcore_max) vcore = vcore_max;
        // Clamp freq to the selectable OC list range (first..last entry).
        const auto& oc_opts = g_web->spec->ui.setting_page.oc;
        if (!oc_opts.empty()) {
            if (freq < oc_opts.front().value) freq = oc_opts.front().value;
            if (freq > oc_opts.back().value)  freq = oc_opts.back().value;
        }
        // Persist to NVS BEFORE clearing the mode flag: the sweep thread's
        // abort path restores from these keys, so it converges to this point.
        nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ,    freq);
        nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, vcore);
        nvs_config_set_u8 (NVS_CONFIG_BM_MODE, 0);
        const bool was_bm = ((*g_web->bm_mode) != 0);
        *g_web->bm_mode = 0;
        // Direction-dependent hot-switch order (same rule as bm_apply_point):
        // the ASIC must never hash at a frequency the rail cannot support.
        //   freq down -> PLL down first (awaited), THEN lower the rail.
        //   freq up   -> raise the rail first so it supports the new frequency.
        const uint16_t cur_freq_hw = g_web->miner ? g_web->miner->get_asic_frequency_current() : 0;
        if (cur_freq_hw != 0 && freq < cur_freq_hw) {
            g_web->spec->asic.req_frq = freq;
            if (g_web->miner != nullptr &&
                !g_web->miner->request_and_await_asic_frequency(freq)) {
                LOG_W("[BM] Apply (hot): freq down-switch did not settle before Vcore move: %uMHz", freq);
            }
            g_web->spec->asic.req_vcore = vcore;
            if (g_web->power  != nullptr) g_web->power->set_vcore_voltage(vcore);
        } else {
            g_web->spec->asic.req_vcore = vcore;
            if (g_web->power  != nullptr) g_web->power->set_vcore_voltage(vcore);
            g_web->spec->asic.req_frq = freq;
            if (g_web->miner  != nullptr && !g_web->miner->request_asic_frequency(freq)) {
                LOG_W("[BM] Apply (hot): freq hot-switch request failed to queue: %uMHz", freq);
            }
        }
        LOG_W("[BM] Apply via API (hot): freq=%dMHz vcore=%dmV — active immediately%s",
              freq, vcore, was_bm ? ", BM sweep aborted" : "");
        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"status\":\"ok\",\"reboot\":false}");
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
    }
    free(buf);
}

// POST /api/benchmark/reset
// Stops any active benchmark run and resets the sweep state (cur_freq /
// cur_vcore) back to the configured minimums. An active sweep aborts in place
// (the thread restores the Normal-mode point) — no reboot.
void post_benchmark_reset(AsyncWebServerRequest* request){
    bool was_running = (nvs_config_get_u8(NVS_CONFIG_BM_MODE, 0) == 1);

    // Reset progress state and config keys so defaults revert to board values.
    // NOTE: results are intentionally preserved — use DELETE /api/benchmark/results to clear them.
    nvs_config_set_u8    (NVS_CONFIG_BM_MODE,    0);
    *g_web->bm_mode = 0;
    nvs_config_delete_key(NVS_CONFIG_BM_FREQ_MIN);
    nvs_config_delete_key(NVS_CONFIG_BM_FREQ_MAX);
    nvs_config_delete_key(NVS_CONFIG_BM_FREQ_STEP);
    nvs_config_delete_key(NVS_CONFIG_BM_VCORE_MIN);
    nvs_config_delete_key(NVS_CONFIG_BM_VCORE_MAX);
    nvs_config_delete_key(NVS_CONFIG_BM_VCORE_STEP);
    nvs_config_delete_key(NVS_CONFIG_BM_SAMPLE_INTV);
    nvs_config_delete_key(NVS_CONFIG_BM_TIME);
    nvs_config_delete_key(NVS_CONFIG_BM_STAB_TIME);
    nvs_config_delete_key(NVS_CONFIG_BM_CUR_FREQ);
    nvs_config_delete_key(NVS_CONFIG_BM_CUR_VCORE);
    nvs_config_delete_key(NVS_CONFIG_BM_START_TS);
    nvs_config_delete_key(NVS_CONFIG_BM_TOTAL_SEC);

    LOG_W("[BM] Benchmark reset via API (was_running=%d)", (int)was_running);

    AsyncWebServerResponse *response = request->beginResponse(200, "application/json",
        "{\"status\":\"ok\",\"message\":\"benchmark reset\"}");
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
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
    xSemaphoreGive(g_web->reboot_xsem);
}

// POST /api/system/clearhits -- reset block-hit counter to zero in RAM and NVS.
void post_reset_block_hits(AsyncWebServerRequest * request){
    g_web->status->hits      = 0;
    xEventGroupClearBits(g_web->sys_evt, SYS_EVENT_MINER_BLOCK_HIT | SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED); 
    nvs_config_set_u16(NVS_CONFIG_BLOCK_HITS, 0);
    LOG_I("Miner stats reset: block hits cleared");
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// PATCH /api/mining/state -- user-controlled runtime pause/resume.
void patch_mining_state(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    static const uint16_t BUF_SIZE = 128;
    if (total >= BUF_SIZE) { request->send(400, "application/json", "{\"error\":\"payload too large\"}"); return; }
    char *buf = (char*)heap_caps_malloc(BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { request->send(500, "application/json", "{\"error\":\"oom\"}"); return; }
    memset(buf, 0, BUF_SIZE);
    if (index + len < BUF_SIZE) memcpy(buf + index, data, len);
    if (index + len == total) {
        buf[total] = '\0';
        StaticJsonDocument<128> root;
        if (deserializeJson(root, buf) || !root.is<JsonObject>() || !root.containsKey("paused")) {
            request->send(400, "application/json", "{\"error\":\"invalid json\"}");
            free(buf); return;
        }

        if ((*g_web->bm_mode) != 0) {
            request->send(409, "application/json", "{\"error\":\"mining state control is disabled in benchmark mode\"}");
            free(buf); return;
        }
        if (g_web->ota->running) {
            request->send(409, "application/json", "{\"error\":\"mining state control is disabled during OTA\"}");
            free(buf); return;
        }
        if (g_web->miner == nullptr || g_web->power == nullptr) {
            request->send(503, "application/json", "{\"error\":\"miner is not ready\"}");
            free(buf); return;
        }

        bool pause_requested = root["paused"].as<bool>();
        MinerRuntimeState state = g_web->status->runtime_state;
        if (pause_requested) {
            if (state != MINER_RUNTIME_PAUSING && state != MINER_RUNTIME_PAUSED) {
                g_web->status->user_paused = true;
                g_web->status->runtime_state = MINER_RUNTIME_PAUSING;
                g_web->status->pause_started_ms = millis();
                g_web->status->resume_grace_until_ms = 0;
                LOG_W("Mining pause requested by API");
            }
        } else {
            // Resume also releases a latched hardware protection. The power
            // supervisor holds while SYS_EVENT_POWER_OC/OT_FAULT is set and re-arms
            // as soon as it clears, so an API resume does the same thing the
            // on-screen "Dismiss" button does. Without this an API-only client could
            // request resume, get 200, and watch nothing happen.
            bool cleared_protection = false;
            if (g_web->sys_evt &&
                (xEventGroupGetBits(g_web->sys_evt) &
                 (SYS_EVENT_POWER_OC_FAULT | SYS_EVENT_POWER_OT_FAULT))) {
                xEventGroupClearBits(g_web->sys_evt,
                                     SYS_EVENT_POWER_OC_FAULT | SYS_EVENT_POWER_OT_FAULT);
                cleared_protection = true;
                LOG_W("API resume: cleared latched hardware protection, supervisor re-arming");
            }
            // `cleared_protection` MUST force the resume on its own: a protection
            // shutdown leaves runtime_state at RUNNING (it is not a user pause), so
            // the state check below would be false and mining would stay at 0 TH/s
            // with Vcore off until the monitor's HCN watchdog happened to fire.
            if (cleared_protection ||
                state != MINER_RUNTIME_RUNNING || g_web->status->user_paused) {
                g_web->status->runtime_state = MINER_RUNTIME_RESUMING;
                g_web->status->pause_started_ms = 0;
                LOG_W("Mining resume requested by API");
            }
        }

        if (g_web->status->control_xsem != NULL) xSemaphoreGive(g_web->status->control_xsem);
        if (g_web->stratum != nullptr) xSemaphoreGive(g_web->stratum->new_job_xsem);
        send_mining_state_response(request);
    }
    free(buf);
}

// ── Reboot history endpoints ────────────────────────────────────────────────
//
// Records are produced by reboot_log_init() at the very top of setup() based on
// the previous run's RTC state + esp_reset_reason(). The web layer only reads.
//
// JSON shape (one record):
//   {
//     "idx": 142, "ts": 0, "uptime": 11520,
//     "heapMin": 7941,          // total min free heap (PSRAM-dominated, legacy)
//     "heapMinInternal": 43560, // min free on-chip SRAM  <- the scarce one
//     "heapMinPsram": 8069000,  // min free PSRAM
//     "reset": "panic", "intent": "none", "class": "crash",
//     "fw": "v3.0.11", "detail": "panic"
//   }
static void reboot_record_to_json(const RebootRecord& r, JsonObject obj) {
    obj["idx"]     = r.boot_index;
    obj["ts"]      = r.epoch_ts;
    obj["uptime"]  = r.uptime_last_sec;
    // heapMin is the total figure and, because PSRAM joins the heap, it reads as
    // an 8 MB-scale number that says nothing about on-chip SRAM. Keep it for
    // existing clients and expose the two real regions next to it.
    obj["heapMin"]         = r.free_heap_min;
    obj["heapMinInternal"] = r.heap_min_internal;
    obj["heapMinPsram"]    = r.heap_min_psram;
    obj["reset"]   = reboot_reset_reason_str(r.reset_reason);
    obj["intent"]  = reboot_intent_str(r.intent);
    obj["class"]   = reboot_class_str(r.cls);
    obj["fw"]      = (const char*)r.fw_version;
    obj["detail"]  = (const char*)r.detail;
}

// GET /api/reboot/last -- newest record only (for the dashboard banner).
void get_reboot_last(AsyncWebServerRequest * request) {
    RebootRecord r;
    BasicJsonDocument<PsramJsonAllocator> doc(768);
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
    // 480 B per record covers all 11 fields with margin (was 4096 for 9 fields
    // and would overflow once heapMinInternal/heapMinPsram were added).
    BasicJsonDocument<PsramJsonAllocator> doc(REBOOT_LOG_RING_SIZE * 480 + 256);
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

// ── GET /api/system/nvs -- read-only NVS inventory (diagnostics) ────────────
//
// Why this exists: writes to the "main" namespace can fail with
// ESP_ERR_NVS_NOT_ENOUGH_SPACE while the obvious data (benchmark rows, top-10)
// looks small. NVS is an entry (32 B) based log-structured store with a 4 KB
// page granularity: a value must fit inside ONE page, pages can only be
// reclaimed by moving every live entry out first, so a handful of large values
// plus long rewrite history can exhaust the partition long before its nominal
// 24 KB is "full" of useful data. Guessing from the outside is not possible -
// this endpoint reports the real numbers:
//   * partition: used/free entry counts from nvs_get_stats()
//   * entries[]: every key with its namespace and payload size in bytes
//   * namespaces[]: key count + bytes per namespace
//   * largest[]: the 10 biggest values (the ones that block page reclaim)
void get_nvs_stats(AsyncWebServerRequest * request) {
    AsyncResponseStream* resp = request->beginResponseStream("application/json");
    resp->addHeader("Access-Control-Allow-Origin", "*");

    nvs_stats_t st;
    memset(&st, 0, sizeof(st));
    const bool stats_ok = (nvs_get_stats("nvs", &st) == ESP_OK);
    // Size straight from the partition table. This is the field that exposes a
    // board flashed with an OLD partition table: an OTA updates the app only,
    // never the partition table, so a device can run the same firmware yet have
    // far less NVS than partitions.csv promises.
    const esp_partition_t* nvs_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
    resp->printf("{\"partition\":{\"ok\":%s,\"size_bytes\":%u,\"total_entries\":%u,\"used_entries\":%u,"
                 "\"free_entries\":%u,\"namespaces\":%u}",
                 stats_ok ? "true" : "false",
                 (unsigned)(nvs_part ? nvs_part->size : 0),
                 (unsigned)st.total_entries, (unsigned)st.used_entries,
                 (unsigned)st.free_entries, (unsigned)st.namespace_count);

    // Aggregates and top-10 kept in BSS: the web task stack is small and this
    // handler holds ~200 iterations of flash reads.
    struct NsAgg { char name[20]; uint16_t keys; uint32_t bytes; };
    struct Big   { char ns[20]; char key[24]; uint32_t bytes; };
    static NsAgg ns_agg[8];
    static Big   big[10];
    uint8_t ns_n = 0, big_n = 0;

    nvs_handle_t h = 0;
    char h_ns[20] = "";

    resp->print(",\"entries\":[");
    bool first = true;
    for (nvs_iterator_t it = nvs_entry_find("nvs", nullptr, NVS_TYPE_ANY); it != nullptr; ) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);

        // Reuse one read handle per namespace instead of opening per key.
        if (strcmp(h_ns, info.namespace_name) != 0) {
            if (h) { nvs_close(h); h = 0; }
            if (nvs_open_from_partition("nvs", info.namespace_name, NVS_READONLY, &h) != ESP_OK) h = 0;
            strncpy(h_ns, info.namespace_name, sizeof(h_ns) - 1);
            h_ns[sizeof(h_ns) - 1] = '\0';
        }

        const char* tname = "?";
        uint32_t    bytes = 0;
        switch (info.type) {
            case NVS_TYPE_U8:   tname = "u8";   bytes = 1; break;
            case NVS_TYPE_I8:   tname = "i8";   bytes = 1; break;
            case NVS_TYPE_U16:  tname = "u16";  bytes = 2; break;
            case NVS_TYPE_I16:  tname = "i16";  bytes = 2; break;
            case NVS_TYPE_U32:  tname = "u32";  bytes = 4; break;
            case NVS_TYPE_I32:  tname = "i32";  bytes = 4; break;
            case NVS_TYPE_U64:  tname = "u64";  bytes = 8; break;
            case NVS_TYPE_I64:  tname = "i64";  bytes = 8; break;
            case NVS_TYPE_STR: {
                tname = "str";
                size_t len = 0;
                if (h && nvs_get_str(h, info.key, nullptr, &len) == ESP_OK) bytes = (uint32_t)len;
                break;
            }
            case NVS_TYPE_BLOB: {
                tname = "blob";
                size_t len = 0;
                if (h && nvs_get_blob(h, info.key, nullptr, &len) == ESP_OK) bytes = (uint32_t)len;
                break;
            }
            default: break;
        }

        if (!first) resp->print(",");
        first = false;
        resp->printf("{\"ns\":\"%s\",\"key\":\"%s\",\"type\":\"%s\",\"bytes\":%u}",
                     info.namespace_name, info.key, tname, (unsigned)bytes);

        // per-namespace aggregate
        uint8_t i = 0;
        for (; i < ns_n; i++) if (strcmp(ns_agg[i].name, info.namespace_name) == 0) break;
        if (i == ns_n && ns_n < (uint8_t)(sizeof(ns_agg) / sizeof(ns_agg[0]))) {
            strncpy(ns_agg[ns_n].name, info.namespace_name, sizeof(ns_agg[ns_n].name) - 1);
            ns_agg[ns_n].keys = 0; ns_agg[ns_n].bytes = 0;
            i = ns_n++;
        }
        if (i < ns_n) { ns_agg[i].keys++; ns_agg[i].bytes += bytes; }

        // keep the 10 largest values, descending (simple insertion into a
        // fixed-size sorted array: nothing bigger than big[0], nothing smaller
        // than big[big_n-1])
        if (bytes > 0) {
            uint8_t pos = big_n;
            while (pos > 0 && big[pos - 1].bytes < bytes) pos--;
            if (pos < 10) {
                const uint8_t last = (big_n < 10) ? big_n : 9;
                for (uint8_t k = last; k > pos; k--) big[k] = big[k - 1];
                strncpy(big[pos].ns, info.namespace_name, sizeof(big[pos].ns) - 1);
                big[pos].ns[sizeof(big[pos].ns) - 1] = '\0';
                strncpy(big[pos].key, info.key, sizeof(big[pos].key) - 1);
                big[pos].key[sizeof(big[pos].key) - 1] = '\0';
                big[pos].bytes = bytes;
                if (big_n < 10) big_n++;
            }
        }

        it = nvs_entry_next(it);
    }
    if (h) nvs_close(h);
    resp->print("]");

    resp->print(",\"namespaces\":[");
    for (uint8_t i = 0; i < ns_n; i++) {
        if (i) resp->print(",");
        resp->printf("{\"ns\":\"%s\",\"keys\":%u,\"bytes\":%u}",
                     ns_agg[i].name, (unsigned)ns_agg[i].keys, (unsigned)ns_agg[i].bytes);
    }
    resp->print("]");

    resp->print(",\"largest\":[");
    for (uint8_t i = 0; i < big_n; i++) {
        if (i) resp->print(",");
        resp->printf("{\"ns\":\"%s\",\"key\":\"%s\",\"bytes\":%u}",
                     big[i].ns, big[i].key, (unsigned)big[i].bytes);
    }
    resp->print("],\"entry_bytes\":32,\"page_bytes\":4096}");

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
//
// Every field is split into two unambiguous groups:
//   crash*  / task / pc / bt  -> describes the image that CRASHED
//   running*                  -> describes the image running right now
// The crashing image is identified by the dump's own ELF SHA plus a persisted
// crash-identity record (see coredump.h); it is never inferred from the
// currently running firmware. `belongsToRunningImage == false` means the dump
// is a leftover from an earlier image (typically an OTA that did not clear it).
void get_coredump_info(AsyncWebServerRequest * request) {
    BasicJsonDocument<PsramJsonAllocator> doc(2048);
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

    // ── Running image (context only, never the crash identity) ─────────────
    char running_sha[COREDUMP_SHA_SHORT_LEN] = {0};
    {
        const esp_app_desc_t* desc = esp_ota_get_app_description();
        if (desc) {
            for (int i = 0; i < 8; ++i)
                snprintf(running_sha + i * 2, 3, "%02x", desc->app_elf_sha256[i]);
        }
    }
    obj["runningFwVersion"] = BOARD_CURRENT_FW_VERSION;
    obj["runningAppSha256"] = running_sha;

    // ── Crash-time facts taken straight from the dump ──────────────────────
    coredump_summary_lite_t s;
    if (coredump_get_summary(&s) && s.valid) {
        obj["task"]         = (const char*)s.task;
        obj["pc"]           = s.pc;
        obj["btCorrupted"]  = s.bt_corrupted;
        obj["appSha256"]    = (const char*)s.app_sha256;  // full 64-hex, from the dump
        JsonArray bt = obj.createNestedArray("bt");
        for (uint8_t i = 0; i < s.bt_depth; ++i) bt.add(s.bt[i]);
    }

    // ── Crash-time identity from persisted metadata ────────────────────────
    coredump_crash_identity_t id;
    if (coredump_get_crash_identity(&id, running_sha)) {
        obj["crashIdentityKnown"]    = id.valid;
        obj["crashAppSha256"]        = id.app_sha256;            // 16 hex, compare with runningAppSha256
        obj["crashFwVersion"]        = id.fw_version;            // "" => not recoverable
        obj["crashUptimeSec"]        = id.uptime_sec;
        obj["crashMinFreeHeap"]      = id.min_free_heap;
        obj["crashEpoch"]            = id.epoch_ts;
        obj["belongsToRunningImage"] = id.belongs_to_running_image;
    }
    String body; serializeJson(doc, body);
    AsyncWebServerResponse* resp = request->beginResponse(200, "application/json", body);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    request->send(resp);
}

// GET /api/coredump/download -- stream the raw ELF coredump image.
//
// /api/coredump/info only exposes the summary, and when the stack walker gives
// up (btCorrupted=true) that summary carries a single frame, which is not
// enough to name the caller. The ELF core still holds every task stack plus the
// exception frame, so a real post-mortem (gdb + the matching firmware.elf) is
// only possible with the raw image. Streamed straight from flash: no RAM buffer
// large enough to matter, and it also works when the dump is >50 KB.
void get_coredump_download(AsyncWebServerRequest * request) {
    size_t total = 0;
    if (!coredump_present(&total, nullptr) || total == 0) {
        request->send(404, "application/json", "{\"error\":\"no coredump\"}");
        return;
    }
    // The stored image starts with the IDF header; serve the ELF core itself so
    // the file can be handed to gdb directly.
    const size_t base = coredump_elf_offset();
    const size_t sz   = (base < total) ? (total - base) : total;
    AsyncWebServerResponse *response = request->beginResponse(
        "application/octet-stream", sz,
        [sz, base](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
            if (index >= sz) return 0;
            size_t n = (sz - index < maxLen) ? (sz - index) : maxLen;
            if (!coredump_read(base + index, buffer, n)) return 0;
            return n;
        });
    response->addHeader("Content-Disposition", "attachment; filename=\"coredump.elf\"");
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    LOG_I("[coredump] download started (%u bytes)", (unsigned)sz);
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

// GET /api/update/progress -- returns current OTA/upload progress from g_web->ota->
// Used by the frontend to poll real device-side write progress for all upload types
// (firmware.bin, spiffs.bin, screensaver .gif).
void get_ota_progress(AsyncWebServerRequest* request) {
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"running\":%s,\"progress\":%d,\"filename\":\"%s\"}",
        g_web->ota->running ? "true" : "false",
        g_web->ota->progress,
        g_web->ota->filename.c_str());
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", buf);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}

// GET /api/update/last-result -- snapshot of the latest OTA/upload outcome.
void get_ota_last_result(AsyncWebServerRequest* request) {
    BasicJsonDocument<PsramJsonAllocator> doc(768);
    JsonObject obj = doc.to<JsonObject>();
    obj["valid"] = g_ota_last_result.valid;
    obj["success"] = g_ota_last_result.success;
    obj["rebootPending"] = g_ota_last_result.reboot_pending;
    obj["httpStatus"] = g_ota_last_result.http_status;
    obj["bytes"] = g_ota_last_result.bytes;
    obj["tsMs"] = g_ota_last_result.ts_ms;
    obj["target"] = g_ota_last_result.target;
    obj["filename"] = g_ota_last_result.filename;
    obj["detail"] = g_ota_last_result.detail;
    obj["running"] = g_web->ota->running;
    obj["progress"] = g_web->ota->progress;

    String body;
    serializeJson(doc, body);

    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", body);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}

// Unified upload handler for all file types:
//   *.gif  → SPIFFS file write (screensaver)  — uses g_board.status.ota for progress
//   *.bin  → OTA flash write (firmware/spiffs) — uses g_board.status.ota for progress
//
// Both paths update g_web->ota->{running, progress, filename} so the frontend
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
        ota_last_result_set(false, false, 0, 0, filename, "upload_started");

        // Detect file type by extension
        String lname = filename;
        lname.toLowerCase();
        is_gif = lname.endsWith(".gif");

        if (is_gif) {
            // ── GIF / screensaver init ─────────────────────────────────────
            // Reject oversized files before writing anything to SPIFFS.
            // 400 KB is a safe upper bound: larger GIFs won't fit in PSRAM
            // alongside the rest of the heap and degrade decode performance.
            static const uint64_t GIF_MAX_BYTES = 400ULL * 1024;
            if (flen > GIF_MAX_BYTES) {
                LOG_E("GIF upload rejected: file too large (%llu bytes, max %llu)", (unsigned long long)flen, (unsigned long long)GIF_MAX_BYTES);
                ota_last_result_set(false, false, 400, (uint32_t)flen, filename, "GIF file too large (max 400 KB)");
                request->send(400, "text/plain", "GIF file too large (max 400 KB).");
                is_gif = false;
                return;
            }
            // Validate GIF magic bytes in the first chunk (must start with GIF87a or GIF89a).
            if (len < 6 || !(memcmp(data, "GIF87a", 6) == 0 || memcmp(data, "GIF89a", 6) == 0)) {
                LOG_E("GIF upload rejected: invalid magic bytes");
                ota_last_result_set(false, false, 400, (uint32_t)len, filename, "Not a valid GIF file");
                request->send(400, "text/plain", "Not a valid GIF file.");
                is_gif = false;
                return;
            }
            String gif_name;
#if defined(BOARD_NMAXE) || defined(BOARD_NMAXE_GAMMA)
            gif_name = "screen_saver_240x135.gif";
#elif defined(BOARD_NMQAXE_PP) || defined(BOARD_NMQAXE_PP_REV61) || defined(BOARD_NMQAXE_PP_REV81)  || defined(BOARD_NMQAXE_PP_NEXUS)
            gif_name = "screen_saver_320x240.gif";
#else
            #error "No board model defined. Add -D BOARD_<model> in platformio.ini"
#endif
            LOG_I("GIF upload started: %s -> /%s  total=%llu bytes", filename.c_str(), gif_name.c_str(), (unsigned long long)flen);
            gif_file = SPIFFS.open(("/" + gif_name).c_str(), "w");
            if (!gif_file) {
                LOG_E("GIF upload: failed to open /%s for writing", gif_name.c_str());
                ota_last_result_set(false, false, 500, 0, filename, "Failed to open file for writing");
                request->send(500, "text/plain", "Failed to open file for writing.");
                return;
            }
            ota_begin_exclusive_ui_session();
            g_web->ota->running          = true;
            g_web->ota->progress         = 0;
            g_web->ota->filename         = filename;
            g_web->ota->last_progress_ms = millis();
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
                ota_last_result_set(false, false, 500, 0, filename, err_msg.c_str());
                request->send(500, "text/plain", err_msg);
                return;
            }
            ota_begin_exclusive_ui_session();
            g_web->ota->running          = true;
            g_web->ota->progress         = 0;
            g_web->ota->filename         = filename;
            g_web->ota->last_progress_ms = millis();
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
                g_web->ota->running = false;
                ota_last_result_set(false, false, 500, (uint32_t)(index + len), filename, "Write error");
                request->send(500, "text/plain", "Write error.");
                return;
            }
            if (flen > 0) {
                int pct = (int)((index + len) * 100ULL / flen);
                if (pct != lastPercentage) {
                    g_web->ota->progress         = pct;
                    g_web->ota->last_progress_ms = millis();
                    LOG_I("GIF upload: %d%%  (%u / %llu bytes)", pct, (unsigned)(index + len), (unsigned long long)flen);
                    lastPercentage = pct;
                }
                delay(5); // Feed the watchdog
            }
        }
        if (final) {
            if (gif_file) gif_file.close();
            g_web->ota->running  = false;
            g_web->ota->progress = 100;
            LOG_I("GIF upload complete: %u bytes saved as %s", (unsigned)(index + len), filename.c_str());
            AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", "{\"status\":\"ok\"}");
            resp->addHeader("Access-Control-Allow-Origin", "*");
            request->send(resp);
            ota_last_result_set(true, false, 200, (uint32_t)(index + len), filename, "upload_success");

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
                    // Release the OTA flag: without this the flag stays set until the
                    // 60s stall watchdog reboots the device, and anything waiting on
                    // ota_running (benchmark reboot, daemon checks) is blocked needlessly.
                    g_web->ota->running = false;
                    ota_last_result_set(false, false, 500, (uint32_t)(index + offset), filename, err_msg.c_str());
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
                    static uint32_t ota_speed_start_ms = 0;
                    static size_t   ota_speed_start_bytes = 0;
                    uint32_t now_ms = millis();
                    g_web->ota->progress         = progress;
                    g_web->ota->last_progress_ms = now_ms;
                    size_t total_bytes = index + offset;
                    if (ota_speed_start_ms > 0 && now_ms > ota_speed_start_ms) {
                        float kBps = (float)(total_bytes - ota_speed_start_bytes) / 1024.0f
                                   / ((now_ms - ota_speed_start_ms) / 1000.0f);
                        LOG_I("%s ota: %d%%, %.1f kB/s", filename.c_str(), progress, kBps);
                    } else {
                        ota_speed_start_ms    = now_ms;
                        ota_speed_start_bytes = total_bytes;
                        LOG_I("%s ota: %d%%", filename.c_str(), progress);
                    }
                    lastPercentage = progress;
                }
            }
        }

        if (final) {
            if (Update.end(true)) {
                // g_web->ota->running  = false;
                g_web->ota->progress = 100;
                LOG_I("%s ota: %d%%", filename.c_str(), g_web->ota->progress);
                // SPIFFS update completed successfully — clear the "in-progress" guard flag
                // so the next boot proceeds to normal mode instead of recovery mode.
                if (filename == "spiffs.bin") {
                    nvs_config_set_u8(NVS_CONFIG_SPIFFS_UPDATING, 0);
                }
                AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"status\":\"ok\"}");
                response->addHeader("Access-Control-Allow-Origin", "*");
                request->send(response);
                ota_last_result_set(true, true, 200, (uint32_t)(index + len), filename, "upload_success_reboot_pending");

                LOG_W("*************** Update Success: %.1f KB, rebooting *************** ", (index + len) / 1024.0);
                // NOTE: delay() in async_tcp context blocks the TCP stack.
                //       daemon_thread_entry waits ~1s before restarting, giving time for the HTTP response to be sent.
                reboot_intent_set(REBOOT_INTENT_OTA_FINISHED, "uploaded %s, %.1f KB",
                                  filename.c_str(), (index + len) / 1024.0);
                xSemaphoreGive(g_web->reboot_xsem);
            } else {
                LOG_E("OTA Update error: %s", Update.errorString());
                String err_msg = String("OTA end failed: ") + Update.errorString();
                // Same as the write-failure path above: a failed end() must release
                // ota_running so waiters can proceed instead of tripping the stall watchdog.
                g_web->ota->running = false;
                ota_last_result_set(false, false, 500, (uint32_t)(index + len), filename, err_msg.c_str());
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
            // A full send queue must drop the message, not kill the connection.
            //
            // Every LOG_* line is broadcast with webSocket.textAll() from
            // whichever task logged it (dbg::log_emit), so a browser that stops
            // draining (background tab, slow Wi-Fi, a page stuck loading) fills
            // the 32-message queue quickly. The library default then closes the
            // client from inside the very loop that walks
            // AsyncWebSocket::_clients, and the page reconnects immediately - a
            // reconnect storm at the exact moment the list is being modified.
            // Dropping a log line costs nothing; dropping the client costs a
            // reconnect (and, before ESPAsyncWebServer 3.10.3, a use-after-free
            // on that list, which surfaces as a panic inside ROM strlen).
            client->setCloseClientOnQueueFull(false);
            Serial.printf("> [WS] client #%u connected from %s\r\n",
                          client->id(), client->remoteIP().toString().c_str());
            // A WebSocket connection is the most reliable signal that a user has opened the web UI.
            // Wake screensaver immediately so the page renders with a live display.
            // Defer the LVGL inactivity reset onto the UI thread.
            UIManager::instance().wake_activity();
            xEventGroupClearBits(g_web->sys_evt, SYS_EVENT_SCREEN_SAVER_TRIGGERED);
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
