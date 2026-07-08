#include "web/web_service.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <Update.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <stdio.h>
#include <string.h>

#include "app/firmware_identity.h"
#include "config/nvs_keys.h"
#include "drivers/storage/storage.h"
#include "utils/logger/logger.h"

namespace nm::web {
namespace {

AsyncWebServer web_server(80);
AsyncWebSocket web_socket("/ws");

WebService* g_service = nullptr;

enum class OtaTarget : uint8_t {
    None,
    Firmware,
    Spiffs,
    Screensaver,
};

struct OtaProgress {
    bool running = false;
    bool error = false;
    uint8_t progress = 0;
    uint32_t bytes = 0;
    uint32_t last_progress_ms = 0;
    char filename[48] = {};
};

struct OtaLastResult {
    bool valid = false;
    bool success = false;
    bool reboot_pending = false;
    uint16_t http_status = 0;
    uint32_t bytes = 0;
    uint32_t ts_ms = 0;
    char target[16] = {};
    char filename[48] = {};
    char detail[128] = {};
};

OtaProgress g_ota_progress;
OtaLastResult g_ota_last_result;

constexpr size_t kOtaWriteBufferSize = 8192;
constexpr uint32_t kOtaStallTimeoutMs = 60 * 1000UL;
constexpr uint32_t kOtaRebootDelayMs = 1200;
constexpr uint64_t kScreensaverMaxBytes = 400ULL * 1024ULL;

struct OtaUploadState {
    bool abort = false;
    OtaTarget target = OtaTarget::None;
    AsyncWebServerRequest* request = nullptr;
    uint8_t buffer[kOtaWriteBufferSize] = {};
    size_t buffer_len = 0;
    int last_logged_percent = -1;
    File screensaver_file;
    char screensaver_path[40] = {};
};

struct RestartSchedule {
    bool pending = false;
    uint32_t due_ms = 0;
    char reason[64] = {};
};

OtaUploadState g_ota_upload;
RestartSchedule g_restart_schedule;
TaskHandle_t g_web_maintenance_task = nullptr;

const char* asic_name(bsp::AsicFamily family) {
    switch (family) {
        case bsp::AsicFamily::BM1366: return "BM1366";
        case bsp::AsicFamily::BM1370: return "BM1370";
        case bsp::AsicFamily::BM1373: return "BM1373";
        default: return "UNKNOWN";
    }
}

const char* mining_phase_name(state::MiningPhase phase) {
    switch (phase) {
        case state::MiningPhase::Disabled: return "disabled";
        case state::MiningPhase::Running: return "running";
        case state::MiningPhase::Fault: return "error";
        case state::MiningPhase::ReadyConfirm: return "ready";
        default: return "initializing";
    }
}

float hashrate_ghs(const bsp::Board& board, const state::RuntimeState& runtime) {
    const uint32_t now = millis();
    if (runtime.mining.last_asic_nonce_ms != 0 && now - runtime.mining.last_asic_nonce_ms < 180000u) {
        const double pool = runtime.mining.diff_pool > 0.0 ? runtime.mining.diff_pool : board.mining_profile().initial_difficulty;
        return static_cast<float>((pool * runtime.mining.asic_nonce_counter * 4.294967296) / max(1u, now / 1000u));
    }
    return runtime.mining.phase == state::MiningPhase::Running
        ? static_cast<float>(board.mining_profile().expected_hashrate_ghs)
        : 0.0f;
}

String format_diff(double value) {
    if (value <= 0.0) {
        return "0";
    }
    if (value >= 1000000000.0) {
        return String(value / 1000000000.0, 3) + "G";
    }
    if (value >= 1000000.0) {
        return String(value / 1000000.0, 3) + "M";
    }
    if (value >= 1000.0) {
        return String(value / 1000.0, 3) + "K";
    }
    return String(value, 3);
}

void add_cors(AsyncWebServerResponse* response) {
    if (response == nullptr) {
        return;
    }
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
}

void send_json(AsyncWebServerRequest* request, JsonDocument& doc, int code = 200) {
    String body;
    serializeJson(doc, body);
    AsyncWebServerResponse* response = request->beginResponse(code, "application/json", body);
    add_cors(response);
    request->send(response);
}

void send_text(AsyncWebServerRequest* request, int code, const char* text) {
    AsyncWebServerResponse* response = request->beginResponse(code, "text/plain", text != nullptr ? text : "");
    add_cors(response);
    request->send(response);
}

bool ota_is_running() {
    return g_ota_progress.running;
}

void send_ota_busy(AsyncWebServerRequest* request) {
    StaticJsonDocument<128> root;
    root["status"] = "busy";
    root["detail"] = "ota running";
    send_json(request, root, 503);
}

bool spiffs_update_flag() {
    drivers::storage::Storage storage(NVS_CONFIG_NAMESPACE, false);
    return storage.get_bool(NVS_CONFIG_SPIFFS_UPDATING, false);
}

void set_spiffs_update_flag(bool updating) {
    drivers::storage::Storage storage(NVS_CONFIG_NAMESPACE, true);
    if (!storage.valid()) {
        return;
    }
    if (storage.set_bool(NVS_CONFIG_SPIFFS_UPDATING, updating)) {
        storage.commit();
    }
}

const char* target_name(OtaTarget target) {
    switch (target) {
        case OtaTarget::Firmware: return "firmware";
        case OtaTarget::Spiffs: return "spiffs";
        case OtaTarget::Screensaver: return "screensaver";
        default: return "unknown";
    }
}

void schedule_restart(const char* reason, uint32_t delay_ms = kOtaRebootDelayMs) {
    g_restart_schedule.pending = true;
    g_restart_schedule.due_ms = millis() + delay_ms;
    snprintf(g_restart_schedule.reason,
             sizeof(g_restart_schedule.reason),
             "%s",
             reason != nullptr ? reason : "scheduled");
    LOG_W("[web] restart scheduled reason=%s delay=%ums",
          g_restart_schedule.reason,
          static_cast<unsigned>(delay_ms));
}

bool parse_body(uint8_t* data, size_t len, JsonDocument& doc) {
    if (data == nullptr || len == 0) {
        return false;
    }
    return deserializeJson(doc, data, len) == DeserializationError::Ok && doc.is<JsonObject>();
}

void lock_state() {
    if (g_service != nullptr && g_service->_state_mutex != nullptr) {
        xSemaphoreTake(g_service->_state_mutex, portMAX_DELAY);
    }
}

void unlock_state() {
    if (g_service != nullptr && g_service->_state_mutex != nullptr) {
        xSemaphoreGive(g_service->_state_mutex);
    }
}

void save_config_locked() {
    if (g_service != nullptr && g_service->_config_store != nullptr && g_service->_config != nullptr) {
        g_service->_config_store->save(*g_service->_config);
    }
}

String content_type_for(const String& path) {
    if (path.endsWith(".html")) return "text/html";
    if (path.endsWith(".css")) return "text/css";
    if (path.endsWith(".js")) return "application/javascript";
    if (path.endsWith(".png")) return "image/png";
    if (path.endsWith(".ico")) return "image/x-icon";
    if (path.endsWith(".svg")) return "image/svg+xml";
    if (path.endsWith(".json")) return "application/json";
    return "text/plain";
}

bool file_system_init() {
    if (spiffs_update_flag()) {
        LOG_E("[web] previous SPIFFS OTA was interrupted; forcing recovery mode");
        return false;
    }

    if (!SPIFFS.begin(false, "", 5, nullptr)) {
        LOG_E("[web] SPIFFS mount failed");
        return false;
    }

    static const char* const required[] = {
        "/index.html.gz",
        "/runtime.js.gz",
        "/main.js.gz",
        "/styles.css.gz",
    };
    for (const char* path : required) {
        if (!SPIFFS.exists(path)) {
            LOG_E("[web] SPIFFS missing %s", path);
            return false;
        }
    }

    LOG_I("[web] SPIFFS ready total=%uKB used=%uKB",
          static_cast<unsigned>(SPIFFS.totalBytes() / 1024),
          static_cast<unsigned>(SPIFFS.usedBytes() / 1024));
    return true;
}

void serve_static(AsyncWebServerRequest* request) {
    if (ota_is_running()) {
        send_ota_busy(request);
        return;
    }

    String plain_path = request->url().endsWith("/") ? "/index.html" : request->url();
    const String content_type = content_type_for(plain_path);
    const String gz_path = plain_path + ".gz";
    File file = SPIFFS.open(gz_path, "r");
    if (!file) {
        request->redirect("/");
        return;
    }

    AsyncWebServerResponse* response = request->beginResponse(
        content_type,
        file.size(),
        [file](uint8_t* buffer, size_t max_len, size_t) mutable -> size_t {
            const size_t read = file.read(buffer, max_len);
            if (read == 0) {
                file.close();
            }
            return read;
        });
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
    response->addHeader("Content-Encoding", "gzip");
    add_cors(response);
    request->send(response);
}

void serve_recovery(AsyncWebServerRequest* request) {
    static const char html[] =
        "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>NMAxe Recovery</title></head><body><h2>NMAxe Recovery</h2>"
        "<p>SPIFFS web assets are missing. Upload spiffs.bin from AxeOS update page or flash full image.</p>"
        "</body></html>";
    AsyncWebServerResponse* response = request->beginResponse(200, "text/html", html);
    add_cors(response);
    request->send(response);
}

void add_chart_labels(JsonArray labels) {
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
}

void add_chart_point(JsonArray point, const bsp::Board& board, const state::RuntimeState& runtime) {
    const float hr = hashrate_ghs(board, runtime);
    point.add(hr);
    point.add(runtime.thermal.asic_c);
    point.add(runtime.thermal.vcore_c);
    point.add(static_cast<float>(runtime.power.power_mw) / 1000.0f);
    point.add(static_cast<float>(runtime.power.vbus_mv) / 1000.0f);
    point.add(static_cast<float>(runtime.power.ibus_ma) / 1000.0f);
    point.add(static_cast<float>(runtime.power.vcore_mv) / 1000.0f);
    point.add(runtime.fan_count > 0 ? runtime.fans[0].speed_percent : 0);
    point.add(runtime.fan_count > 0 ? runtime.fans[0].rpm : 0);
    point.add(runtime.network.rssi_dbm);
    point.add(ESP.getFreeHeap() / 1024);
    point.add(ESP.getFreePsram() / 1024);
    point.add(runtime.stratum.last_share_latency_ms);
    point.add(millis());
}

void handle_system_info(AsyncWebServerRequest* request) {
    if (g_service == nullptr) {
        send_text(request, 503, "web service unavailable");
        return;
    }

    StaticJsonDocument<4096> root;
    lock_state();
    const auto& board = *g_service->_board;
    const auto& cfg = *g_service->_config;
    const auto& rt = *g_service->_runtime;

    const float power_w = static_cast<float>(rt.power.power_mw) / 1000.0f;
    const float hr = hashrate_ghs(board, rt);
    const uint32_t uptime_s = millis() / 1000u;
    const uint16_t fan_rpm = rt.fan_count > 0 ? rt.fans[0].rpm : 0;
    const uint8_t fan_speed = rt.fan_count > 0 ? rt.fans[0].speed_percent : 0;

    JsonObject power = root.createNestedObject("power");
    power["power"] = power_w;
    power["vbus"] = rt.power.vbus_mv;
    power["ibus"] = rt.power.ibus_ma;

    JsonObject temps = root.createNestedObject("temps");
    temps["vcore"] = rt.thermal.vcore_c;
    temps["mcu"] = temperatureRead();
    temps["asic"] = rt.thermal.asic_c;

    JsonObject asic = root.createNestedObject("asic");
    asic["count"] = rt.mining.detected_asic_count != 0 ? rt.mining.detected_asic_count : board.mining_profile().asic_count;
    asic["model"] = asic_name(board.mining_profile().asic_family);
    asic["vcoreReq"] = cfg.mining.target_vcore_mv;
    asic["vcoreReal"] = rt.power.vcore_mv;
    asic["freqReq"] = cfg.mining.target_freq_mhz;
    asic["smallCoreCnt"] = 0;

    JsonObject miner = root.createNestedObject("miner");
    miner["state"] = mining_phase_name(rt.mining.phase);
    miner["paused"] = rt.mining.phase == state::MiningPhase::Disabled;
    miner["pauseReason"] = "";
    miner["bmMode"] = cfg.benchmark.mode;
    miner["hashRate"] = hr;
    miner["bestDiffEver"] = format_diff(rt.mining.diff_best_session);
    miner["bestDiffSession"] = format_diff(rt.mining.diff_best_session);
    miner["networkDiff"] = format_diff(rt.mining.diff_network);
    miner["poolDiff"] = format_diff(rt.mining.diff_pool);
    miner["lastDiff"] = format_diff(rt.mining.diff_last);
    miner["blkhits"] = 0;
    miner["freeHeap"] = ESP.getFreeHeap();
    miner["minFreeHeap"] = ESP.getMinFreeHeap();
    miner["sAccepted"] = rt.stratum.share_accepted;
    miner["sRejected"] = rt.stratum.share_rejected;
    miner["uptimeSeconds"] = uptime_s;
    miner["uptimeEver"] = uptime_s;

    JsonObject identity = root.createNestedObject("identity");
    identity["fwVersion"] = app::kFirmwareVersion;
    identity["hwModel"] = board.traits().board_name;
    identity["displayName"] = board.traits().display_name;
    identity["hostName"] = cfg.network.hostname;
    identity["ssid"] = rt.network.ssid;
    identity["rssi"] = rt.network.rssi_dbm;

    JsonArray fans = root.createNestedArray("fans");
    for (uint8_t i = 0; i < rt.fan_count; ++i) {
        JsonObject fan = fans.createNestedObject();
        fan["id"] = i;
        fan["speed"] = rt.fans[i].speed_percent;
        fan["rpm"] = rt.fans[i].rpm;
    }

    JsonObject stratum = root.createNestedObject("stratum");
    stratum["url"] = cfg.stratum.primary.url;
    stratum["user"] = cfg.stratum.primary.user;
    stratum["pwd"] = cfg.stratum.primary.password;
    JsonObject primary = stratum.createNestedObject("primary");
    primary["url"] = cfg.stratum.primary.url;
    primary["user"] = cfg.stratum.primary.user;
    primary["pwd"] = cfg.stratum.primary.password;
    JsonObject fallback = stratum.createNestedObject("fallback");
    fallback["url"] = cfg.stratum.fallback.url;
    fallback["user"] = cfg.stratum.fallback.user;
    fallback["pwd"] = cfg.stratum.fallback.password;

    root["coinPriceDisplay"] = cfg.market.display_coin;
    root["mainprice"] = cfg.market.display_coin;
    root["timeZone"] = cfg.time.timezone;
    root["timeFormat"] = cfg.time.hour_format;
    root["dateFormat"] = cfg.time.date_format;
    root["screenFlip"] = cfg.screen.flip ? 1 : 0;
    root["screenAutoRoll"] = cfg.screen.auto_cycle_pages ? 1 : 0;
    root["Brightness"] = cfg.screen.brightness_percent;
    root["asicTargetTemp"] = String(cfg.cooling.asic.target_temp_c, 1);
    root["fanAutoSpeed"] = cfg.cooling.asic.auto_control ? 1 : 0;
    root["ledIndicator"] = cfg.led.indicator_enabled ? 1 : 0;

    root["usedUrl"] = cfg.stratum.primary.url;
    root["usedUser"] = cfg.stratum.primary.user;
    root["primaryUrl"] = cfg.stratum.primary.url;
    root["primaryUser"] = cfg.stratum.primary.user;
    root["primaryPassword"] = cfg.stratum.primary.password;
    root["fallBackUrl"] = cfg.stratum.fallback.url;
    root["fallBackUser"] = cfg.stratum.fallback.user;
    root["fallBackPassword"] = cfg.stratum.fallback.password;
    root["stratumURLUSED"] = cfg.stratum.primary.url;
    root["stratumURL1"] = cfg.stratum.primary.url;
    root["stratumURL2"] = cfg.stratum.fallback.url;
    root["stratumUserUSED"] = cfg.stratum.primary.user;
    root["stratumUser1"] = cfg.stratum.primary.user;
    root["stratumPassword1"] = cfg.stratum.primary.password;
    root["stratumUser2"] = cfg.stratum.fallback.user;
    root["stratumPassword2"] = cfg.stratum.fallback.password;
    root["coin"] = cfg.market.display_coin;
    root["brightness"] = cfg.screen.brightness_percent;
    root["version"] = app::kFirmwareVersion;
    root["boardVersion"] = board.traits().board_revision;
    root["ASICModel"] = asic_name(board.mining_profile().asic_family);
    root["bestDiff"] = format_diff(rt.mining.diff_best_session);
    root["bestSessionDiff"] = format_diff(rt.mining.diff_best_session);
    root["smallCoreCount"] = 0;
    root["flipscreen"] = cfg.screen.flip ? 1 : 0;
    root["invertscreen"] = board.display_profile().color_invert ? 1 : 0;
    root["ledindicator"] = cfg.led.indicator_enabled ? 1 : 0;
    root["autofanspeed"] = cfg.cooling.asic.auto_control ? 1 : 0;
    root["targetAsicTemp"] = String(cfg.cooling.asic.target_temp_c, 1);
    root["autoscreen"] = cfg.screen.auto_cycle_pages ? 1 : 0;
    root["fanspeed"] = fan_speed;
    root["fanrpm"] = fan_rpm;
    root["boardtemp2"] = rt.thermal.vcore_c;
    unlock_state();

    send_json(request, root);
}

void handle_setting_network(AsyncWebServerRequest* request) {
    StaticJsonDocument<512> root;
    lock_state();
    const auto& cfg = *g_service->_config;
    const auto& rt = *g_service->_runtime;
    root["hostName"] = cfg.network.hostname;
    root["ssid"] = cfg.network.sta_ssid;
    root["password"] = cfg.network.sta_password;
    root["apSsid"] = cfg.network.ap_ssid;
    root["status"] = rt.network.sta_connected ? "connected" : (rt.network.ap_ready ? "ap" : "disconnected");
    root["ip"] = rt.network.ip;
    unlock_state();
    send_json(request, root);
}

void patch_setting_network(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
    StaticJsonDocument<1024> body;
    if (!parse_body(data, len, body)) {
        send_text(request, 400, "invalid json");
        return;
    }
    lock_state();
    auto& cfg = *g_service->_config;
    cfg.network.hostname = body["hostName"] | body["hostname"] | cfg.network.hostname;
    cfg.network.sta_ssid = body["ssid"] | body["staSsid"] | cfg.network.sta_ssid;
    cfg.network.sta_password = body["password"] | body["staPassword"] | cfg.network.sta_password;
    cfg.network.ap_ssid = body["apSsid"] | cfg.network.ap_ssid;
    save_config_locked();
    unlock_state();
    StaticJsonDocument<128> root;
    root["status"] = "ok";
    send_json(request, root);
}

void handle_setting_time(AsyncWebServerRequest* request) {
    StaticJsonDocument<256> root;
    lock_state();
    const auto& cfg = *g_service->_config;
    root["timeZone"] = cfg.time.timezone;
    root["timeFormat"] = cfg.time.hour_format;
    root["dateFormat"] = cfg.time.date_format;
    unlock_state();
    send_json(request, root);
}

void patch_setting_time(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
    StaticJsonDocument<512> body;
    if (!parse_body(data, len, body)) {
        send_text(request, 400, "invalid json");
        return;
    }
    lock_state();
    auto& cfg = *g_service->_config;
    cfg.time.timezone = body["timeZone"] | cfg.time.timezone;
    cfg.time.hour_format = (body["timeFormat"] | cfg.time.hour_format) == 12 ? 12 : 24;
    cfg.time.date_format = body["dateFormat"] | cfg.time.date_format;
    save_config_locked();
    unlock_state();
    StaticJsonDocument<128> root;
    root["status"] = "ok";
    send_json(request, root);
}

void add_options(JsonArray options, uint16_t min, uint16_t max, uint16_t step, const char* suffix) {
    if (step == 0) {
        step = 25;
    }
    for (uint16_t value = min; value <= max; value = static_cast<uint16_t>(value + step)) {
        JsonObject option = options.createNestedObject();
        option["name"] = String(value) + suffix;
        option["value"] = value;
        if (max - value < step) {
            break;
        }
    }
}

void handle_setting_mining(AsyncWebServerRequest* request) {
    StaticJsonDocument<2048> root;
    lock_state();
    const auto& board = *g_service->_board;
    const auto& cfg = *g_service->_config;
    root["vcoreReq"] = cfg.mining.target_vcore_mv;
    root["freqReq"] = cfg.mining.target_freq_mhz;
    root["asic"] = asic_name(board.mining_profile().asic_family);

    JsonObject stratum = root.createNestedObject("stratum");
    JsonObject used = stratum.createNestedObject("used");
    used["url"] = cfg.stratum.primary.url;
    used["user"] = cfg.stratum.primary.user;
    used["pwd"] = cfg.stratum.primary.password;
    JsonObject primary = stratum.createNestedObject("primary");
    primary["url"] = cfg.stratum.primary.url;
    primary["user"] = cfg.stratum.primary.user;
    primary["pwd"] = cfg.stratum.primary.password;
    JsonObject fallback = stratum.createNestedObject("fallback");
    fallback["url"] = cfg.stratum.fallback.url;
    fallback["user"] = cfg.stratum.fallback.user;
    fallback["pwd"] = cfg.stratum.fallback.password;

    JsonArray oc = root["overclock"].createNestedArray("options");
    add_options(oc, cfg.benchmark.freq_min_mhz, cfg.benchmark.freq_max_mhz, cfg.benchmark.freq_step_mhz, " MHz");
    JsonArray vc = root["vcore"].createNestedArray("options");
    add_options(vc, board.policies().min_vcore_mv, board.policies().max_vcore_mv, 25, " mV");
    unlock_state();
    send_json(request, root);
}

void patch_setting_mining(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
    StaticJsonDocument<2048> body;
    if (!parse_body(data, len, body)) {
        send_text(request, 400, "invalid json");
        return;
    }
    lock_state();
    auto& cfg = *g_service->_config;
    cfg.mining.target_freq_mhz = body["freqReq"] | body["frequency"] | cfg.mining.target_freq_mhz;
    cfg.mining.target_vcore_mv = body["vcoreReq"] | body["coreVoltage"] | cfg.mining.target_vcore_mv;
    if (body["stratum"].is<JsonObject>()) {
        JsonObject s = body["stratum"];
        if (s["primary"].is<JsonObject>()) {
            cfg.stratum.primary.url = s["primary"]["url"] | cfg.stratum.primary.url;
            cfg.stratum.primary.user = s["primary"]["user"] | cfg.stratum.primary.user;
            cfg.stratum.primary.password = s["primary"]["pwd"] | cfg.stratum.primary.password;
        }
        if (s["fallback"].is<JsonObject>()) {
            cfg.stratum.fallback.url = s["fallback"]["url"] | cfg.stratum.fallback.url;
            cfg.stratum.fallback.user = s["fallback"]["user"] | cfg.stratum.fallback.user;
            cfg.stratum.fallback.password = s["fallback"]["pwd"] | cfg.stratum.fallback.password;
        }
    }
    cfg.stratum.primary.url = body["primaryUrl"] | body["stratumURL1"] | cfg.stratum.primary.url;
    cfg.stratum.primary.user = body["primaryUser"] | body["stratumUser1"] | cfg.stratum.primary.user;
    cfg.stratum.primary.password = body["primaryPassword"] | body["stratumPassword1"] | cfg.stratum.primary.password;
    cfg.stratum.fallback.url = body["fallBackUrl"] | body["stratumURL2"] | cfg.stratum.fallback.url;
    cfg.stratum.fallback.user = body["fallBackUser"] | body["stratumUser2"] | cfg.stratum.fallback.user;
    cfg.stratum.fallback.password = body["fallBackPassword"] | body["stratumPassword2"] | cfg.stratum.fallback.password;
    save_config_locked();
    unlock_state();
    StaticJsonDocument<128> root;
    root["status"] = "ok";
    send_json(request, root);
}

void handle_setting_market(AsyncWebServerRequest* request) {
    StaticJsonDocument<1024> root;
    lock_state();
    const auto& cfg = *g_service->_config;
    root["mainprice"] = cfg.market.display_coin;
    root["coinWatchlist"] = cfg.market.watchlist;
    JsonArray pairs = root.createNestedArray("pairs");
    const char* defaults[] = {"BTCUSDT", "ETHUSDT", "BNBUSDT", "SOLUSDT", "XRPUSDT", "DOGEUSDT", "LTCUSDT", "BCHUSDT", "XECUSDT"};
    for (const char* pair : defaults) {
        pairs.add(pair);
    }
    unlock_state();
    send_json(request, root);
}

void patch_setting_market(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
    StaticJsonDocument<1024> body;
    if (!parse_body(data, len, body)) {
        send_text(request, 400, "invalid json");
        return;
    }
    lock_state();
    auto& cfg = *g_service->_config;
    cfg.market.display_coin = body["mainprice"] | body["coinPriceDisplay"] | cfg.market.display_coin;
    cfg.market.watchlist = body["coinWatchlist"] | body["coinWatchlistDisplay"] | cfg.market.watchlist;
    save_config_locked();
    unlock_state();
    StaticJsonDocument<128> root;
    root["status"] = "ok";
    send_json(request, root);
}

void handle_setting_preference(AsyncWebServerRequest* request) {
    StaticJsonDocument<2048> root;
    lock_state();
    const auto& board = *g_service->_board;
    const auto& cfg = *g_service->_config;
    const auto& rt = *g_service->_runtime;
    root["screenFlip"] = cfg.screen.flip ? 1 : 0;
    root["ledIndicator"] = cfg.led.indicator_enabled ? 1 : 0;
    root["fanAutoSpeed"] = cfg.cooling.asic.auto_control ? 1 : 0;
    root["screenAutoRoll"] = cfg.screen.auto_cycle_pages ? 1 : 0;
    root["asicTargetTemp"] = String(cfg.cooling.asic.target_temp_c, 1);
    root["Brightness"] = cfg.screen.brightness_percent;
    root["screensaverEnable"] = cfg.screen.screensaver_enabled ? 1 : 0;
    root["screensaverTimeout"] = cfg.screen.screensaver_timeout_s;
    root["screensaverMode"] = cfg.screen.screensaver_mode;
    root["hwModel"] = board.traits().board_name;
    root["displayName"] = board.traits().display_name;
    JsonArray fans = root.createNestedArray("fans");
    for (uint8_t i = 0; i < max<uint8_t>(rt.fan_count, 1); ++i) {
        JsonObject fan = fans.createNestedObject();
        fan["id"] = i;
        fan["speed"] = i < rt.fan_count ? rt.fans[i].speed_percent : cfg.cooling.asic.manual_speed_percent;
        fan["rpm"] = i < rt.fan_count ? rt.fans[i].rpm : 0;
        fan["auto"] = cfg.cooling.asic.auto_control ? 1 : 0;
        fan["target"] = cfg.cooling.asic.target_temp_c;
        fan["tempMin"] = 20;
        fan["tempMax"] = 90;
    }
    unlock_state();
    send_json(request, root);
}

void patch_setting_preference(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
    StaticJsonDocument<1024> body;
    if (!parse_body(data, len, body)) {
        send_text(request, 400, "invalid json");
        return;
    }
    lock_state();
    auto& cfg = *g_service->_config;
    cfg.screen.flip = (body["screenFlip"] | body["flipscreen"] | (cfg.screen.flip ? 1 : 0)) != 0;
    cfg.led.indicator_enabled = (body["ledIndicator"] | body["ledindicator"] | (cfg.led.indicator_enabled ? 1 : 0)) != 0;
    cfg.screen.auto_cycle_pages = (body["screenAutoRoll"] | body["autoscreen"] | (cfg.screen.auto_cycle_pages ? 1 : 0)) != 0;
    cfg.screen.brightness_percent = body["Brightness"] | body["brightness"] | cfg.screen.brightness_percent;
    cfg.screen.screensaver_enabled = (body["screensaverEnable"] | (cfg.screen.screensaver_enabled ? 1 : 0)) != 0;
    cfg.screen.screensaver_timeout_s = body["screensaverTimeout"] | cfg.screen.screensaver_timeout_s;
    cfg.screen.screensaver_mode = body["screensaverMode"] | cfg.screen.screensaver_mode;
    cfg.cooling.asic.auto_control = (body["fanAutoSpeed"] | body["autofanspeed"] | (cfg.cooling.asic.auto_control ? 1 : 0)) != 0;
    cfg.cooling.asic.target_temp_c = atof(String(body["asicTargetTemp"] | body["targetAsicTemp"] | String(cfg.cooling.asic.target_temp_c, 1)).c_str());
    cfg.cooling.asic.manual_speed_percent = body["fanspeed"] | cfg.cooling.asic.manual_speed_percent;
    if (g_service->_board->drivers().display != nullptr) {
        g_service->_board->drivers().display->set_flip(cfg.screen.flip);
        g_service->_board->drivers().display->set_brightness_percent(cfg.screen.brightness_percent);
    }
    save_config_locked();
    unlock_state();
    StaticJsonDocument<128> root;
    root["status"] = "ok";
    send_json(request, root);
}

void handle_chart(AsyncWebServerRequest* request, bool history) {
    StaticJsonDocument<2048> root;
    lock_state();
    const auto& board = *g_service->_board;
    const auto& rt = *g_service->_runtime;
    root["timestamp"] = millis();
    add_chart_labels(root.createNestedArray("labels"));
    JsonArray data = root.createNestedArray("statistics");
    JsonArray point = data.createNestedArray();
    add_chart_point(point, board, rt);
    root["size"] = history ? 1 : 1;
    root["sampledSize"] = 1;
    root["sampleInterval"] = 1;
    unlock_state();
    send_json(request, root);
}

void handle_lucky_history(AsyncWebServerRequest* request) {
    StaticJsonDocument<1024> root;
    lock_state();
    const auto& rt = *g_service->_runtime;
    root["timestamp"] = millis();
    JsonArray labels = root.createNestedArray("labels");
    labels.add("bestDiff");
    labels.add("lastDiff");
    labels.add("poolDiff");
    labels.add("networkDiff");
    labels.add("epoch");
    JsonArray row = root.createNestedArray("statistics").createNestedArray();
    row.add(rt.mining.diff_best_session);
    row.add(rt.mining.diff_last);
    row.add(rt.mining.diff_pool);
    row.add(rt.mining.diff_network);
    row.add(millis());
    root["size"] = 1;
    root["sampledSize"] = 1;
    unlock_state();
    send_json(request, root);
}

void handle_gauge_limits(AsyncWebServerRequest* request) {
    StaticJsonDocument<1024> root;
    lock_state();
    const auto& board = *g_service->_board;
    root["power"]["vbus"]["min"] = 0.0;
    root["power"]["vbus"]["max"] = 18.0;
    root["power"]["ibus"]["min"] = 0.0;
    root["power"]["ibus"]["max"] = 6.0;
    root["power"]["power"]["min"] = 0.0;
    root["power"]["power"]["max"] = 80.0;
    root["heat"]["mcu"]["min"] = 0.0;
    root["heat"]["mcu"]["max"] = 85.0;
    root["heat"]["asic"]["min"] = 0.0;
    root["heat"]["asic"]["max"] = 90.0;
    root["heat"]["vcore"]["min"] = 0.0;
    root["heat"]["vcore"]["max"] = 110.0;
    root["heat"]["fan"]["min"] = 0.0;
    root["heat"]["fan"]["max"] = 10000.0;
    root["performance"]["asic_freq_req"]["min"] = g_service->_config->benchmark.freq_min_mhz;
    root["performance"]["asic_freq_req"]["max"] = g_service->_config->benchmark.freq_max_mhz;
    root["performance"]["vcore_req"]["min"] = static_cast<float>(board.policies().min_vcore_mv) / 1000.0f;
    root["performance"]["vcore_req"]["max"] = static_cast<float>(board.policies().max_vcore_mv) / 1000.0f;
    root["performance"]["vcore_measure"]["min"] = static_cast<float>(board.policies().min_vcore_mv) / 1000.0f;
    root["performance"]["vcore_measure"]["max"] = static_cast<float>(board.policies().max_vcore_mv) / 1000.0f;
    unlock_state();
    send_json(request, root);
}

void handle_hr_dist(AsyncWebServerRequest* request) {
    StaticJsonDocument<512> root;
    JsonArray labels = root.createNestedArray("labels");
    labels.add("0-25%");
    labels.add("25-50%");
    labels.add("50-75%");
    labels.add("75-100%");
    JsonArray values = root.createNestedArray("values");
    values.add(0);
    values.add(0);
    values.add(0);
    values.add(g_service != nullptr && g_service->_runtime->mining.phase == state::MiningPhase::Running ? 1 : 0);
    send_json(request, root);
}

void handle_theme(AsyncWebServerRequest* request) {
    StaticJsonDocument<1024> root;
    lock_state();
    const auto& cfg = *g_service->_config;
    root["colorScheme"] = cfg.theme.color_scheme;
    root["theme"] = cfg.theme.name;
    root["accentColors"] = serialized(cfg.theme.accent_colors_json);
    unlock_state();
    send_json(request, root);
}

void post_theme(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
    StaticJsonDocument<2048> body;
    if (!parse_body(data, len, body)) {
        send_text(request, 400, "invalid json");
        return;
    }
    lock_state();
    auto& cfg = *g_service->_config;
    cfg.theme.color_scheme = body["colorScheme"] | cfg.theme.color_scheme;
    cfg.theme.name = body["theme"] | cfg.theme.name;
    if (body["accentColors"].is<JsonObject>()) {
        String colors;
        serializeJson(body["accentColors"], colors);
        cfg.theme.accent_colors_json = colors;
    }
    save_config_locked();
    unlock_state();
    StaticJsonDocument<128> root;
    root["status"] = "ok";
    send_json(request, root);
}

void handle_benchmark(AsyncWebServerRequest* request) {
    StaticJsonDocument<2048> root;
    lock_state();
    const auto& cfg = *g_service->_config;
    root["mode"] = cfg.benchmark.mode;
    root["freqMin"] = cfg.benchmark.freq_min_mhz;
    root["freqMax"] = cfg.benchmark.freq_max_mhz;
    root["freqStep"] = cfg.benchmark.freq_step_mhz;
    root["vcoreMin"] = cfg.benchmark.vcore_min_mv;
    root["vcoreMax"] = cfg.benchmark.vcore_max_mv;
    root["vcoreStep"] = cfg.benchmark.vcore_step_mv;
    root["sampleIntv"] = cfg.benchmark.sample_interval_s;
    root["bmTime"] = cfg.benchmark.benchmark_time_s;
    root["stabTime"] = cfg.benchmark.stabilize_time_s;
    root["curFreq"] = cfg.benchmark.current_freq_mhz;
    root["curVcore"] = cfg.benchmark.current_vcore_mv;
    root["results"] = serialized(cfg.benchmark.result_json);
    unlock_state();
    send_json(request, root);
}

void patch_benchmark(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
    StaticJsonDocument<1024> body;
    if (!parse_body(data, len, body)) {
        send_text(request, 400, "invalid json");
        return;
    }
    lock_state();
    auto& bm = g_service->_config->benchmark;
    bm.freq_min_mhz = body["freqMin"] | bm.freq_min_mhz;
    bm.freq_max_mhz = body["freqMax"] | bm.freq_max_mhz;
    bm.freq_step_mhz = body["freqStep"] | bm.freq_step_mhz;
    bm.vcore_min_mv = body["vcoreMin"] | bm.vcore_min_mv;
    bm.vcore_max_mv = body["vcoreMax"] | bm.vcore_max_mv;
    bm.vcore_step_mv = body["vcoreStep"] | bm.vcore_step_mv;
    bm.sample_interval_s = body["sampleIntv"] | bm.sample_interval_s;
    bm.benchmark_time_s = body["bmTime"] | bm.benchmark_time_s;
    bm.stabilize_time_s = body["stabTime"] | bm.stabilize_time_s;
    save_config_locked();
    unlock_state();
    StaticJsonDocument<128> root;
    root["status"] = "ok";
    send_json(request, root);
}

void handle_probe(AsyncWebServerRequest* request) {
    if (ota_is_running()) {
        send_ota_busy(request);
        return;
    }

    StaticJsonDocument<512> root;
    lock_state();
    const auto& board = *g_service->_board;
    const auto& cfg = *g_service->_config;
    const auto& rt = *g_service->_runtime;
    root["model"] = board.traits().display_name;
    root["hostname"] = cfg.network.hostname;
    root["ver"] = app::kFirmwareVersion;
    root["hr"] = hashrate_ghs(board, rt);
    root["sbd"] = rt.mining.diff_best_session;
    root["ebd"] = rt.mining.diff_best_session;
    root["ut"] = millis() / 1000u;
    root["sw"] = board.display_profile().width;
    root["sh"] = board.display_profile().height;
    unlock_state();
    send_json(request, root);
}

void handle_alive(AsyncWebServerRequest* request) {
    if (ota_is_running()) {
        send_ota_busy(request);
        return;
    }

    StaticJsonDocument<512> root;
    lock_state();
    root["self"] = g_service->_runtime->network.ip;
    root["scanning"] = false;
    root["progress"] = 0;
    root["total"] = 0;
    root.createNestedArray("ips");
    unlock_state();
    send_json(request, root);
}

void handle_empty_array(AsyncWebServerRequest* request) {
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", "[]");
    add_cors(response);
    request->send(response);
}

void handle_empty_object(AsyncWebServerRequest* request) {
    StaticJsonDocument<256> root;
    root["valid"] = false;
    root["present"] = false;
    send_json(request, root);
}

void handle_restart(AsyncWebServerRequest* request) {
    send_text(request, 200, "System will restart shortly.");
    schedule_restart("user_web_reboot", 500);
}

void handle_mining_state_patch(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
    if (ota_is_running()) {
        send_ota_busy(request);
        return;
    }

    StaticJsonDocument<256> body;
    if (!parse_body(data, len, body)) {
        send_text(request, 400, "invalid json");
        return;
    }
    const bool paused = body["paused"] | false;
    StaticJsonDocument<256> root;
    root["status"] = "ok";
    lock_state();
    root["state"] = paused ? "paused" : mining_phase_name(g_service->_runtime->mining.phase);
    root["paused"] = paused;
    root["reason"] = paused ? "user" : "";
    root["vcoreEnabled"] = !paused;
    unlock_state();
    send_json(request, root);
}

void handle_ota_progress(AsyncWebServerRequest* request) {
    StaticJsonDocument<256> root;
    root["running"] = g_ota_progress.running;
    root["progress"] = g_ota_progress.progress;
    root["filename"] = g_ota_progress.filename;
    send_json(request, root);
}

void handle_ota_last_result(AsyncWebServerRequest* request) {
    StaticJsonDocument<512> root;
    root["valid"] = g_ota_last_result.valid;
    root["success"] = g_ota_last_result.success;
    root["rebootPending"] = g_ota_last_result.reboot_pending;
    root["target"] = g_ota_last_result.target;
    root["filename"] = g_ota_last_result.filename;
    root["detail"] = g_ota_last_result.detail;
    root["httpStatus"] = g_ota_last_result.http_status;
    root["bytes"] = g_ota_last_result.bytes;
    root["tsMs"] = g_ota_last_result.ts_ms;
    root["running"] = g_ota_progress.running;
    root["progress"] = g_ota_progress.progress;
    send_json(request, root);
}

void set_ota_result(
    OtaTarget target,
    bool success,
    bool reboot_pending,
    uint16_t code,
    uint32_t bytes,
    const String& filename,
    const char* detail) {
    g_ota_last_result.valid = true;
    g_ota_last_result.success = success;
    g_ota_last_result.reboot_pending = reboot_pending;
    g_ota_last_result.http_status = code;
    g_ota_last_result.bytes = bytes;
    g_ota_last_result.ts_ms = millis();
    snprintf(g_ota_last_result.target, sizeof(g_ota_last_result.target), "%s", target_name(target));
    snprintf(g_ota_last_result.filename, sizeof(g_ota_last_result.filename), "%s", filename.c_str());
    snprintf(g_ota_last_result.detail, sizeof(g_ota_last_result.detail), "%s", detail != nullptr ? detail : "");
}

OtaTarget upload_target_for(AsyncWebServerRequest* request, const String& filename) {
    const String url = request->url();
    String lower_name = filename;
    lower_name.toLowerCase();

    if (url.indexOf("/api/update/screensaver") >= 0 || lower_name.endsWith(".gif")) {
        return OtaTarget::Screensaver;
    }
    if (url.endsWith("/api/system/OTAWWW") || url.indexOf("/api/update/spiffs") >= 0) {
        return OtaTarget::Spiffs;
    }
    if (url.endsWith("/api/system/OTA") || url.indexOf("/api/update/firmware") >= 0) {
        return OtaTarget::Firmware;
    }
    return OtaTarget::None;
}

const char* screensaver_path() {
    if (g_service != nullptr && g_service->_board != nullptr) {
        const auto& display = g_service->_board->display_profile();
        if (display.width == 320 && display.height == 240) {
            return "/screen_saver_320x240.gif";
        }
    }
    return "/screen_saver_240x135.gif";
}

void update_ota_progress(uint32_t transferred, size_t total, const String& filename, const char* prefix) {
    g_ota_progress.bytes = transferred;
    g_ota_progress.last_progress_ms = millis();

    if (total == 0) {
        return;
    }

    uint32_t percent = (static_cast<uint64_t>(transferred) * 100ULL) / total;
    if (percent > 100) {
        percent = 100;
    }
    g_ota_progress.progress = static_cast<uint8_t>(percent);

    if (static_cast<int>(percent) != g_ota_upload.last_logged_percent) {
        LOG_I("%s %s: %u%% (%u / %u bytes)",
              filename.c_str(),
              prefix != nullptr ? prefix : "ota",
              static_cast<unsigned>(percent),
              static_cast<unsigned>(transferred),
              static_cast<unsigned>(total));
        g_ota_upload.last_logged_percent = static_cast<int>(percent);
    }
}

void reset_upload_state() {
    if (g_ota_upload.screensaver_file) {
        g_ota_upload.screensaver_file.close();
    }
    g_ota_upload.abort = false;
    g_ota_upload.target = OtaTarget::None;
    g_ota_upload.request = nullptr;
    g_ota_upload.buffer_len = 0;
    g_ota_upload.last_logged_percent = -1;
    g_ota_upload.screensaver_path[0] = '\0';
}

void fail_upload(
    AsyncWebServerRequest* request,
    OtaTarget target,
    uint16_t http_status,
    uint32_t bytes,
    const String& filename,
    const char* detail) {
    g_ota_upload.abort = true;
    if (g_ota_upload.screensaver_file) {
        g_ota_upload.screensaver_file.close();
    }
    if (target == OtaTarget::Firmware || target == OtaTarget::Spiffs) {
        Update.abort();
    }
    if (target == OtaTarget::Spiffs && bytes == 0) {
        set_spiffs_update_flag(false);
    }

    g_ota_progress.running = false;
    g_ota_progress.error = true;
    g_ota_progress.bytes = bytes;
    g_ota_progress.last_progress_ms = millis();
    set_ota_result(target, false, false, http_status, bytes, filename, detail);

    LOG_E("[web] %s upload failed: %s", target_name(target), detail != nullptr ? detail : "unknown");
    send_text(request, http_status, detail != nullptr ? detail : "upload failed");
}

bool validate_upload_begin(AsyncWebServerRequest* request, OtaTarget target, const String& filename, size_t len, uint8_t* data) {
    String lower_name = filename;
    lower_name.toLowerCase();

    if (target == OtaTarget::Firmware && filename != "firmware.bin") {
        fail_upload(request, target, 400, 0, filename, "firmware update requires firmware.bin");
        return false;
    }
    if (target == OtaTarget::Spiffs && filename != "spiffs.bin") {
        fail_upload(request, target, 400, 0, filename, "website update requires spiffs.bin");
        return false;
    }
    if (target == OtaTarget::Screensaver && !lower_name.endsWith(".gif")) {
        fail_upload(request, target, 400, 0, filename, "screensaver update requires a GIF file");
        return false;
    }
    if (target == OtaTarget::Screensaver) {
        if (request->contentLength() > kScreensaverMaxBytes) {
            fail_upload(request, target, 400, 0, filename, "GIF file too large (max 400 KB)");
            return false;
        }
        if (len < 6 || data == nullptr || (memcmp(data, "GIF87a", 6) != 0 && memcmp(data, "GIF89a", 6) != 0)) {
            fail_upload(request, target, 400, 0, filename, "Not a valid GIF file");
            return false;
        }
    }
    return true;
}

bool begin_upload(AsyncWebServerRequest* request, OtaTarget target, const String& filename, size_t len, uint8_t* data) {
    if (ota_is_running() && g_ota_upload.request != request) {
        send_text(request, 409, "another OTA upload is already running");
        return false;
    }

    reset_upload_state();
    g_ota_upload.target = target;
    g_ota_upload.request = request;

    set_ota_result(target, false, false, 0, 0, filename, "upload_started");

    if (!validate_upload_begin(request, target, filename, len, data)) {
        return false;
    }

    g_ota_progress.running = true;
    g_ota_progress.error = false;
    g_ota_progress.progress = 0;
    g_ota_progress.bytes = 0;
    g_ota_progress.last_progress_ms = millis();
    snprintf(g_ota_progress.filename, sizeof(g_ota_progress.filename), "%s", filename.c_str());

    if (target == OtaTarget::Screensaver) {
        snprintf(g_ota_upload.screensaver_path,
                 sizeof(g_ota_upload.screensaver_path),
                 "%s",
                 screensaver_path());
        SPIFFS.remove(g_ota_upload.screensaver_path);
        g_ota_upload.screensaver_file = SPIFFS.open(g_ota_upload.screensaver_path, "w");
        if (!g_ota_upload.screensaver_file) {
            fail_upload(request, target, 500, 0, filename, "Failed to open screensaver file for writing");
            return false;
        }

        LOG_I("[web] GIF upload started: %s -> %s total=%u bytes",
              filename.c_str(),
              g_ota_upload.screensaver_path,
              static_cast<unsigned>(request->contentLength()));
        return true;
    }

    const int update_type = target == OtaTarget::Spiffs ? U_SPIFFS : U_FLASH;
    if (target == OtaTarget::Spiffs) {
        set_spiffs_update_flag(true);
    }

    LOG_I("[web] OTA started target=%s file=%s total=%u bytes",
          target_name(target),
          filename.c_str(),
          static_cast<unsigned>(request->contentLength()));

    if (!Update.begin(UPDATE_SIZE_UNKNOWN, update_type)) {
        fail_upload(request, target, 500, 0, filename, Update.errorString());
        return false;
    }
    return true;
}

bool write_ota_buffer(
    AsyncWebServerRequest* request,
    const String& filename,
    const uint8_t* data,
    size_t len,
    bool final,
    size_t index) {
    size_t offset = 0;
    while (offset < len) {
        const size_t copy_len = min(len - offset, kOtaWriteBufferSize - g_ota_upload.buffer_len);
        memcpy(g_ota_upload.buffer + g_ota_upload.buffer_len, data + offset, copy_len);
        g_ota_upload.buffer_len += copy_len;
        offset += copy_len;

        const bool flush = g_ota_upload.buffer_len == kOtaWriteBufferSize || (final && offset == len);
        if (!flush) {
            continue;
        }

        if (Update.write(g_ota_upload.buffer, g_ota_upload.buffer_len) != g_ota_upload.buffer_len) {
            fail_upload(
                request,
                g_ota_upload.target,
                500,
                static_cast<uint32_t>(index + offset),
                filename,
                Update.errorString());
            g_ota_upload.buffer_len = 0;
            return false;
        }

        g_ota_upload.buffer_len = 0;
        vTaskDelay(pdMS_TO_TICKS(1));
        update_ota_progress(
            static_cast<uint32_t>(index + offset),
            request->contentLength(),
            filename,
            "ota");
    }

    if (final && g_ota_upload.buffer_len > 0) {
        if (Update.write(g_ota_upload.buffer, g_ota_upload.buffer_len) != g_ota_upload.buffer_len) {
            fail_upload(
                request,
                g_ota_upload.target,
                500,
                static_cast<uint32_t>(index + len),
                filename,
                Update.errorString());
            g_ota_upload.buffer_len = 0;
            return false;
        }

        g_ota_upload.buffer_len = 0;
        vTaskDelay(pdMS_TO_TICKS(1));
        update_ota_progress(
            static_cast<uint32_t>(index + len),
            request->contentLength(),
            filename,
            "ota");
    }
    return true;
}

void upload_handler(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
    if (index == 0) {
        const OtaTarget target = upload_target_for(request, filename);
        if (target == OtaTarget::None) {
            fail_upload(request, target, 404, 0, filename, "unknown update target");
            return;
        }
        if (!begin_upload(request, target, filename, len, data)) {
            return;
        }
    }

    if (g_ota_upload.request != request || g_ota_upload.abort) {
        return;
    }

    const OtaTarget target = g_ota_upload.target;
    if (target == OtaTarget::Screensaver) {
        if (!g_ota_upload.screensaver_file || g_ota_upload.screensaver_file.write(data, len) != len) {
            fail_upload(request, target, 500, static_cast<uint32_t>(index + len), filename, "screensaver write failed");
            return;
        }
        update_ota_progress(static_cast<uint32_t>(index + len), request->contentLength(), filename, "upload");
        vTaskDelay(pdMS_TO_TICKS(1));

        if (final) {
            g_ota_upload.screensaver_file.close();
            g_ota_progress.running = false;
            g_ota_progress.progress = 100;
            g_ota_progress.bytes = static_cast<uint32_t>(index + len);
            set_ota_result(target, true, false, 200, g_ota_progress.bytes, filename, "upload_success");
            LOG_I("[web] GIF upload complete: %u bytes saved as %s",
                  static_cast<unsigned>(g_ota_progress.bytes),
                  g_ota_upload.screensaver_path);
            reset_upload_state();

            StaticJsonDocument<64> root;
            root["status"] = "ok";
            send_json(request, root);
        }
        return;
    }

    if (!write_ota_buffer(request, filename, data, len, final, index)) {
        return;
    }

    if (final) {
        bool ok = Update.end(true);
        if (!ok) {
            fail_upload(
                request,
                target,
                500,
                static_cast<uint32_t>(index + len),
                filename,
                Update.errorString());
            return;
        }

        if (target == OtaTarget::Spiffs) {
            set_spiffs_update_flag(false);
        }

        g_ota_progress.running = false;
        g_ota_progress.error = false;
        g_ota_progress.progress = 100;
        g_ota_progress.bytes = static_cast<uint32_t>(index + len);
        g_ota_progress.last_progress_ms = millis();
        set_ota_result(target, true, true, 200, g_ota_progress.bytes, filename, "upload_success_reboot_pending");
        LOG_W("[web] %s OTA success: %.1f KB, rebooting",
              target_name(target),
              g_ota_progress.bytes / 1024.0f);
        reset_upload_state();

        StaticJsonDocument<64> root;
        root["status"] = "ok";
        send_json(request, root);
        schedule_restart(target == OtaTarget::Spiffs ? "ota_spiffs_finished" : "ota_firmware_finished");
    }
}

void web_maintenance_task(void*) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(250));
        web_socket.cleanupClients();

        if (g_ota_progress.running && g_ota_progress.last_progress_ms != 0) {
            const uint32_t now = millis();
            if (static_cast<uint32_t>(now - g_ota_progress.last_progress_ms) > kOtaStallTimeoutMs) {
                LOG_E("[web] OTA stalled >60s at %u%%, rebooting",
                      static_cast<unsigned>(g_ota_progress.progress));
                if (g_ota_upload.target == OtaTarget::Firmware || g_ota_upload.target == OtaTarget::Spiffs) {
                    Update.abort();
                }
                if (g_ota_upload.screensaver_file) {
                    g_ota_upload.screensaver_file.close();
                }
                g_ota_progress.running = false;
                g_ota_progress.error = true;
                set_ota_result(
                    g_ota_upload.target,
                    false,
                    true,
                    500,
                    g_ota_progress.bytes,
                    String(g_ota_progress.filename),
                    "ota stalled, reboot pending");
                schedule_restart("ota_stall", 500);
            }
        }

        if (g_restart_schedule.pending &&
            static_cast<int32_t>(millis() - g_restart_schedule.due_ms) >= 0) {
            LOG_W("[web] restarting now reason=%s", g_restart_schedule.reason);
            Serial.flush();
            ESP.restart();
        }
    }
}

void register_common_routes(bool fs_ready) {
    web_server.on("/api/system/info", HTTP_GET, handle_system_info);
    web_server.on("/api/system/restart", HTTP_POST, handle_restart);
    web_server.on("/api/system/clearhits", HTTP_POST, [](AsyncWebServerRequest* request) {
        lock_state();
        g_service->_runtime->mining.diff_best_session = 0.0;
        unlock_state();
        StaticJsonDocument<128> root;
        root["status"] = "ok";
        send_json(request, root);
    });
    web_server.on("/api/mining/state", HTTP_PATCH, [](AsyncWebServerRequest*) {}, nullptr, handle_mining_state_patch);

    web_server.on("/api/wakeup", HTTP_GET, [](AsyncWebServerRequest* request) {
        StaticJsonDocument<128> root;
        root["status"] = "ok";
        send_json(request, root);
    });
    web_server.on("/api/log", HTTP_GET, [](AsyncWebServerRequest* request) {
        send_text(request, 200, "connect websocket /ws");
    });

    web_server.on("/api/dashboard/hr/dist", HTTP_GET, handle_hr_dist);
    web_server.on("/api/dashboard/gauge/limits", HTTP_GET, handle_gauge_limits);
    web_server.on("/api/dashboard/chart/history", HTTP_GET, [](AsyncWebServerRequest* r) { handle_chart(r, true); });
    web_server.on("/api/dashboard/chart/realtime", HTTP_GET, [](AsyncWebServerRequest* r) { handle_chart(r, false); });
    web_server.on("/api/dashboard/luck/history", HTTP_GET, handle_lucky_history);
    web_server.on("/api/dashboard/luck/realtime", HTTP_GET, handle_lucky_history);

    web_server.on("/api/theme", HTTP_GET, handle_theme);
    web_server.on("/api/theme", HTTP_POST, [](AsyncWebServerRequest*) {}, nullptr, post_theme);
    web_server.on("/api/theme", HTTP_OPTIONS, [](AsyncWebServerRequest* request) { send_text(request, 204, ""); });

    web_server.on("/api/setting/network", HTTP_GET, handle_setting_network);
    web_server.on("/api/setting/network", HTTP_PATCH, [](AsyncWebServerRequest*) {}, nullptr, patch_setting_network);
    web_server.on("/api/setting/time", HTTP_GET, handle_setting_time);
    web_server.on("/api/setting/time", HTTP_PATCH, [](AsyncWebServerRequest*) {}, nullptr, patch_setting_time);
    web_server.on("/api/setting/mining", HTTP_GET, handle_setting_mining);
    web_server.on("/api/setting/mining", HTTP_PATCH, [](AsyncWebServerRequest*) {}, nullptr, patch_setting_mining);
    web_server.on("/api/setting/market", HTTP_GET, handle_setting_market);
    web_server.on("/api/setting/market", HTTP_PATCH, [](AsyncWebServerRequest*) {}, nullptr, patch_setting_market);
    web_server.on("/api/setting/preference", HTTP_GET, handle_setting_preference);
    web_server.on("/api/setting/preference", HTTP_PATCH, [](AsyncWebServerRequest*) {}, nullptr, patch_setting_preference);

    web_server.on("/api/benchmark", HTTP_GET, handle_benchmark);
    web_server.on("/api/benchmark", HTTP_PATCH, [](AsyncWebServerRequest*) {}, nullptr, patch_benchmark);
    web_server.on("/api/benchmark/start", HTTP_POST, [](AsyncWebServerRequest* request) {
        lock_state();
        g_service->_config->benchmark.mode = 1;
        save_config_locked();
        unlock_state();
        StaticJsonDocument<128> root;
        root["status"] = "ok";
        send_json(request, root);
    });
    web_server.on("/api/benchmark/stop", HTTP_POST, [](AsyncWebServerRequest* request) {
        lock_state();
        g_service->_config->benchmark.mode = 0;
        save_config_locked();
        unlock_state();
        StaticJsonDocument<128> root;
        root["status"] = "ok";
        send_json(request, root);
    });
    web_server.on("/api/benchmark/results", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        lock_state();
        g_service->_config->benchmark.result_json = "[]";
        save_config_locked();
        unlock_state();
        StaticJsonDocument<128> root;
        root["status"] = "ok";
        send_json(request, root);
    });
    web_server.on("/api/benchmark/reset", HTTP_POST, [](AsyncWebServerRequest* request) {
        lock_state();
        g_service->_config->benchmark.mode = 0;
        save_config_locked();
        unlock_state();
        StaticJsonDocument<128> root;
        root["status"] = "ok";
        send_json(request, root);
    });
    web_server.on("/api/benchmark/apply", HTTP_POST, [](AsyncWebServerRequest*) {}, nullptr, [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
        StaticJsonDocument<256> body;
        if (!parse_body(data, len, body)) {
            send_text(request, 400, "invalid json");
            return;
        }
        lock_state();
        g_service->_config->mining.target_freq_mhz = body["freq"] | g_service->_config->mining.target_freq_mhz;
        g_service->_config->mining.target_vcore_mv = body["vcore"] | g_service->_config->mining.target_vcore_mv;
        save_config_locked();
        unlock_state();
        StaticJsonDocument<128> root;
        root["status"] = "ok";
        send_json(request, root);
    });

    web_server.on("/api/reboot/list", HTTP_GET, handle_empty_array);
    web_server.on("/api/reboot/list", HTTP_DELETE, [](AsyncWebServerRequest* request) { send_text(request, 200, "OK"); });
    web_server.on("/api/reboot/last", HTTP_GET, [](AsyncWebServerRequest* request) {
        StaticJsonDocument<256> root;
        root["valid"] = false;
        root["reason"] = "unknown";
        root["planned"] = false;
        root["tsMs"] = 0;
        send_json(request, root);
    });
    web_server.on("/api/coredump/info", HTTP_GET, handle_empty_object);
    web_server.on("/api/coredump", HTTP_DELETE, [](AsyncWebServerRequest* request) { send_text(request, 200, "OK"); });

    web_server.on("/api/swarm/scan", HTTP_POST, [](AsyncWebServerRequest* request) {
        StaticJsonDocument<128> root;
        root["status"] = "ok";
        send_json(request, root);
    });
    web_server.on("/api/swarm/find", HTTP_POST, [](AsyncWebServerRequest* request) {
        StaticJsonDocument<256> root;
        root["status"] = "ok";
        root.createNestedArray("ips");
        send_json(request, root);
    });

    web_server.on("/api/update/progress", HTTP_GET, handle_ota_progress);
    web_server.on("/api/update/last-result", HTTP_GET, handle_ota_last_result);
    web_server.on("/api/update/firmware", HTTP_POST, [](AsyncWebServerRequest*) {}, upload_handler);
    web_server.on("/api/update/spiffs", HTTP_POST, [](AsyncWebServerRequest*) {}, upload_handler);
    web_server.on("/api/update/screensaver", HTTP_POST, [](AsyncWebServerRequest*) {}, upload_handler);
    web_server.on("/api/system/OTA", HTTP_POST, [](AsyncWebServerRequest*) {}, upload_handler);
    web_server.on("/api/system/OTAWWW", HTTP_POST, [](AsyncWebServerRequest*) {}, upload_handler);

    web_server.on("/probe", HTTP_GET, handle_probe);
    web_server.on("/alive", HTTP_GET, handle_alive);
    web_server.on("/api-doc", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (SPIFFS.exists("/api-doc.html.gz")) {
            request->redirect("/api-doc.html");
            return;
        }
        send_text(request, 404, "api doc not found");
    });
    web_server.on("/*", HTTP_OPTIONS, [](AsyncWebServerRequest* request) { send_text(request, 204, ""); });
    web_server.on("/*", HTTP_GET, [fs_ready](AsyncWebServerRequest* request) {
        if (request->url().startsWith("/api/")) {
            send_text(request, 404, "not found");
            return;
        }
        if (fs_ready) {
            serve_static(request);
        } else {
            serve_recovery(request);
        }
    });
}

}  // namespace

bool WebService::start(
    const bsp::Board& board,
    config::ConfigStore& config_store,
    config::AppConfig& config,
    state::RuntimeState& runtime,
    system::EventFlags& events,
    SemaphoreHandle_t state_mutex) {
    if (_started) {
        return true;
    }

    _board = &board;
    _config_store = &config_store;
    _config = &config;
    _runtime = &runtime;
    _events = &events;
    _state_mutex = state_mutex;
    g_service = this;

    _fs_ready = file_system_init();

    web_socket.onEvent([](AsyncWebSocket*, AsyncWebSocketClient*, AwsEventType, void*, uint8_t*, size_t) {});
    web_server.addHandler(&web_socket);
    register_common_routes(_fs_ready);
    web_server.begin();

    if (g_web_maintenance_task == nullptr) {
        xTaskCreate(web_maintenance_task, "(web_maint)", 4096, nullptr, 1, &g_web_maintenance_task);
    }

    _started = true;
    LOG_I("[web] AxeOS service started fs=%u", _fs_ready ? 1u : 0u);
    return true;
}

}  // namespace nm::web
