#include "services/stratum_service.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <stdio.h>

#include "app/firmware_identity.h"
#include "app/task_config.h"
#include "utils/logger/logger.h"

namespace nm::services {

namespace {

constexpr uint32_t kPoolConnectTimeoutMs = 4000;
constexpr uint32_t kSubscribeTimeoutMs = 10000;
constexpr uint32_t kAuthorizeTimeoutMs = 10000;
constexpr uint32_t kReconnectDelayMs = 5000;
constexpr uint32_t kHelloPoolIntervalMs = 120000;
constexpr uint32_t kLostPoolTimeoutMs = 300000;
constexpr uint32_t kSubmitTimeoutMs = 120000;
constexpr uint32_t kFallbackProbeMinMs = 60000;
constexpr uint32_t kFallbackProbeJitterMs = 540000;
constexpr uint16_t kPoolMaxRetries = 5;
constexpr uint32_t kMaxPendingResponseCache = 20;
constexpr double kDefaultInitialDifficulty = 1024.0;

void copy_text(char* dest, size_t dest_size, const char* text) {
    if (dest == nullptr || dest_size == 0) {
        return;
    }

    snprintf(dest, dest_size, "%s", text != nullptr ? text : "");
}

void copy_text(char* dest, size_t dest_size, const String& text) {
    copy_text(dest, dest_size, text.c_str());
}

}  // namespace

void StratumService::start(const bsp::Board& board, const config::AppConfig& config) {
    if (_started) {
        return;
    }

    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
    }
    if (_write_mutex == nullptr) {
        _write_mutex = xSemaphoreCreateMutex();
    }
    if (_new_job_sem == nullptr) {
        _new_job_sem = xSemaphoreCreateCounting(1, 0);
    }
    if (_clear_job_sem == nullptr) {
        _clear_job_sem = xSemaphoreCreateCounting(1, 0);
    }
    if (_mutex == nullptr || _write_mutex == nullptr || _new_job_sem == nullptr || _clear_job_sem == nullptr) {
        LOG_E("[stratum] sync primitive create failed");
        return;
    }

    if (!_parse_endpoint(config.stratum.primary, _primary_endpoint)) {
        _set_error("Wrong pool URL!!!");
        return;
    }
    _fallback_endpoint = {};
    _parse_endpoint(config.stratum.fallback, _fallback_endpoint);

    _using_fallback = false;
    _select_endpoint(false);
    _client_id = String(board.traits().display_name != nullptr ? board.traits().display_name : board.key()) +
                 "/" + app::kFirmwareVersion;
    _request_id = 1;
    _authorize_id = 0;
    _pool_retry = 0;
    _pool_difficulty = kDefaultInitialDifficulty;
    _stop_requested = false;
    _reset_protocol_state();
    _reset_session_status();

    const BaseType_t ok = xTaskCreatePinnedToCore(
        _task_entry,
        "(stratum)",
        app::kStratumTaskStackBytes,
        this,
        app::kTaskPriorityStratum,
        &_task,
        app::kTaskCoreNet);
    if (ok != pdPASS) {
        LOG_E("[stratum] failed to create task");
        _task = nullptr;
        _set_error("Stratum task fail");
        return;
    }

    _started = true;
    LOG_I("[stratum] task started client_id=%s primary=%s:%u fallback=%s",
          _client_id.c_str(),
          _primary_endpoint.host.c_str(),
          static_cast<unsigned>(_primary_endpoint.port),
          _fallback_endpoint.valid ? _fallback_endpoint.host.c_str() : "none");
}

void StratumService::stop() {
    _stop_requested = true;
    if (_client != nullptr) {
        _client->stop();
    }
}

bool StratumService::poll(state::RuntimeState& runtime) {
    if (_mutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(_mutex, 0) != pdTRUE) {
        return false;
    }
    runtime.stratum = _telemetry;
    xSemaphoreGive(_mutex);
    return true;
}

bool StratumService::pop_job(PoolJobData& job) {
    if (_mutex == nullptr || xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    const bool has_job = !_job_cache.empty();
    if (has_job) {
        job = _job_cache.front();
        _job_cache.pop_front();
        _telemetry.job_cache_size = static_cast<uint8_t>(_job_cache.size());
    }

    xSemaphoreGive(_mutex);
    return has_job;
}

size_t StratumService::job_cache_size() const {
    if (_mutex == nullptr || xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) {
        return 0;
    }
    const size_t size = _job_cache.size();
    xSemaphoreGive(_mutex);
    return size;
}

bool StratumService::submit(
    const String& pool_job_id,
    const String& extranonce2,
    uint32_t ntime,
    uint32_t nonce,
    uint32_t version) {
    if (_client == nullptr || !_client->connected()) {
        LOG_W("[stratum] submit rejected locally, pool disconnected");
        return false;
    }

    const uint32_t id = _next_id();
    char ntime_str[9] = {};
    char nonce_str[9] = {};
    char version_str[9] = {};
    snprintf(ntime_str, sizeof(ntime_str), "%08lx", static_cast<unsigned long>(ntime));
    snprintf(nonce_str, sizeof(nonce_str), "%08lx", static_cast<unsigned long>(nonce));
    snprintf(version_str, sizeof(version_str), "%08lx", static_cast<unsigned long>(version));

    const String payload =
        "{\"id\": " + String(id) +
        ", \"method\": \"mining.submit\", \"params\": [\"" +
        _endpoint.user + "\", \"" +
        pool_job_id + "\", \"" +
        extranonce2 + "\", \"" +
        String(ntime_str) + "\", \"" +
        String(nonce_str) + "\", \"" +
        String(version_str) + "\"]}\n";

    _record_pending(id, "mining.submit");
    if (!_send_line(payload)) {
        LOG_E("[stratum] Failed to send mining.submit request");
        return false;
    }
    LOG_I("[stratum] Sending mining.submit job=%s id=%lu", pool_job_id.c_str(), static_cast<unsigned long>(id));
    return true;
}

String StratumService::next_extranonce2() {
    if (_mutex == nullptr || xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) {
        return "0";
    }

    const uint8_t size = _telemetry.extranonce2_size;
    if (size == 0) {
        xSemaphoreGive(_mutex);
        return "0";
    }

    uint64_t value = strtoull(_extranonce2.c_str(), nullptr, 16);
    value = (value + 1) & ((1ULL << (8 * size)) - 1);

    char buffer[17] = {};
    snprintf(buffer, sizeof(buffer), "%0*llx", 2 * size, static_cast<unsigned long long>(value));
    _extranonce2 = buffer;
    const String result = _extranonce2;
    xSemaphoreGive(_mutex);
    return result;
}

bool StratumService::clear_extranonce2() {
    if (_mutex == nullptr || xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    _extranonce2 = "0";
    xSemaphoreGive(_mutex);
    return true;
}

bool StratumService::_parse_endpoint(const config::StratumEndpointConfig& config, Endpoint& endpoint) {
    endpoint = {};

    String url = config.url;
    url.trim();
    if (url.isEmpty()) {
        return false;
    }

    endpoint.ssl = url.indexOf("stratum+ssl://") == 0 || url.indexOf("stratum+tls://") == 0;
    const int scheme = url.indexOf("://");
    if (scheme >= 0) {
        url = url.substring(scheme + 3);
    }

    const int slash = url.indexOf('/');
    if (slash >= 0) {
        url = url.substring(0, slash);
    }

    const int colon = url.lastIndexOf(':');
    if (colon <= 0 || colon >= static_cast<int>(url.length() - 1)) {
        return false;
    }

    endpoint.host = url.substring(0, colon);
    endpoint.port = static_cast<uint16_t>(url.substring(colon + 1).toInt());
    endpoint.user = config.user;
    endpoint.password = config.password.isEmpty() ? "x" : config.password;
    endpoint.valid = !endpoint.host.isEmpty() && endpoint.port != 0 && !endpoint.user.isEmpty();
    return endpoint.valid;
}

void StratumService::_select_endpoint(bool fallback) {
    if (fallback && _fallback_endpoint.valid) {
        _endpoint = _fallback_endpoint;
        _using_fallback = true;
        return;
    }
    _endpoint = _primary_endpoint;
    _using_fallback = false;
}

void StratumService::_task_entry(void* args) {
    auto* self = static_cast<StratumService*>(args);
    if (self != nullptr) {
        self->_run();
    }
    vTaskDelete(nullptr);
}

void StratumService::_run() {
    while (!_stop_requested) {
        if (WiFi.status() != WL_CONNECTED) {
            _reset_session_status();
            _set_error("");
            LOG_W("[stratum] waiting for WiFi STA connection");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        if (_using_fallback && _primary_available()) {
            LOG_I("[stratum] Primary pool [%s] available now, switching to primary pool", _primary_endpoint.host.c_str());
            _select_endpoint(false);
            _pool_retry = 0;
        }

        _connect_and_listen();
        if (!_stop_requested) {
            ++_pool_retry;
            if (_pool_retry % kPoolMaxRetries == 0 && _fallback_endpoint.valid) {
                _select_endpoint(!_using_fallback);
                LOG_W("[stratum] >>>> Set pool to %s [%s:%u] <<<<",
                      _using_fallback ? "fallback" : "primary",
                      _endpoint.host.c_str(),
                      static_cast<unsigned>(_endpoint.port));
            }
            vTaskDelay(pdMS_TO_TICKS(kReconnectDelayMs));
        }
    }

    if (_client != nullptr) {
        _client->stop();
    }
    if (_mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _telemetry.task_running = false;
        _telemetry.connecting = false;
        xSemaphoreGive(_mutex);
    }
    _task = nullptr;
    _started = false;
    LOG_W("[stratum] task stopped");
}

void StratumService::_connect_and_listen() {
    _reset_protocol_state();
    _reset_session_status();
    if (!_connect_pool(_endpoint)) {
        return;
    }

    _pool_retry = 0;
    if (!_subscribe()) {
        LOG_W("[stratum] Failed to subscribe to pool, retrying");
        return;
    }
    if (!_authorize()) {
        LOG_W("[stratum] Failed to authorize to pool, retrying");
        return;
    }
    if (!_configure_version_rolling()) {
        LOG_W("[stratum] Failed to config version rolling");
    }
    if (!_suggest_difficulty()) {
        LOG_W("[stratum] Failed to suggest difficulty to pool");
    }

    while (!_stop_requested && _client != nullptr && _client->connected() && WiFi.status() == WL_CONNECTED) {
        if (!_hello_pool(millis())) {
            LOG_W("[stratum] Pool is inactive, reconnecting");
            break;
        }

        const String line = _read_line(50);
        if (!line.isEmpty()) {
            _handle_line(line);
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (_client != nullptr) {
        _client->stop();
    }
    _reset_session_status();
}

bool StratumService::_connect_pool(const Endpoint& endpoint) {
    _client = endpoint.ssl ? static_cast<Client*>(&_ssl_client) : static_cast<Client*>(&_tcp_client);
    if (endpoint.ssl) {
        _ssl_client.setInsecure();
    }

    if (_mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _telemetry.task_running = true;
        _telemetry.connecting = true;
        _telemetry.ssl = endpoint.ssl;
        _telemetry.using_fallback = _using_fallback;
        _telemetry.port = endpoint.port;
        _telemetry.pool_difficulty = _pool_difficulty;
        copy_text(_telemetry.host, sizeof(_telemetry.host), endpoint.host);
        copy_text(_telemetry.user, sizeof(_telemetry.user), endpoint.user);
        copy_text(_telemetry.last_error, sizeof(_telemetry.last_error), "");
        _telemetry.last_update_ms = millis();
        xSemaphoreGive(_mutex);
    }

    IPAddress pool_ip;
    LOG_I("[stratum] Resolve pool address %s use dns1: %s, dns2: %s",
          endpoint.host.c_str(),
          WiFi.dnsIP(0).toString().c_str(),
          WiFi.dnsIP(1).toString().c_str());
    if (WiFi.hostByName(endpoint.host.c_str(), pool_ip) != 1) {
        _set_error("Wrong pool URL!!!");
        LOG_E("[stratum] Failed to resolve pool [%s]", endpoint.host.c_str());
        return false;
    }

    LOG_I("[stratum] Connecting %s:%u [%s]",
          endpoint.host.c_str(),
          static_cast<unsigned>(endpoint.port),
          endpoint.ssl ? "ssl" : "tcp");
    const bool connected = endpoint.ssl
        ? (_ssl_client.connect(pool_ip, endpoint.port, static_cast<int32_t>(kPoolConnectTimeoutMs)) != 0)
        : (_tcp_client.connect(pool_ip, endpoint.port, static_cast<int32_t>(kPoolConnectTimeoutMs)) != 0);
    if (!connected) {
        _set_error("Wrong pool port!!!");
        return false;
    }

    _last_read_ms = millis();
    _last_write_ms = millis();
    if (_mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _telemetry.connecting = false;
        _telemetry.connected = true;
        _telemetry.last_read_ms = _last_read_ms;
        _telemetry.last_write_ms = _last_write_ms;
        _telemetry.last_update_ms = millis();
        xSemaphoreGive(_mutex);
    }
    LOG_I("[stratum] Connected to pool %s:%u", endpoint.host.c_str(), static_cast<unsigned>(endpoint.port));
    return true;
}

bool StratumService::_send_line(const String& line) {
    if (_write_mutex != nullptr) {
        xSemaphoreTake(_write_mutex, portMAX_DELAY);
    }
    if (_client == nullptr || !_client->connected()) {
        if (_write_mutex != nullptr) {
            xSemaphoreGive(_write_mutex);
        }
        return false;
    }
    const size_t written = _client->print(line);
    if (_write_mutex != nullptr) {
        xSemaphoreGive(_write_mutex);
    }
    if (written > 0) {
        _last_write_ms = millis();
        if (_mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
            _telemetry.last_write_ms = _last_write_ms;
            _telemetry.last_update_ms = _last_write_ms;
            xSemaphoreGive(_mutex);
        }
    }
    return written == line.length();
}

String StratumService::_read_line(uint32_t timeout_ms) {
    if (_client == nullptr || !_client->connected()) {
        return "";
    }

    String line;
    uint32_t last_byte_ms = millis();
    while ((millis() - last_byte_ms) < timeout_ms && !_stop_requested) {
        while (_client->available()) {
            const char c = static_cast<char>(_client->read());
            line += c;
            last_byte_ms = millis();
            _last_read_ms = last_byte_ms;
            if (c == '\n') {
                if (_mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
                    _telemetry.last_read_ms = _last_read_ms;
                    _telemetry.last_update_ms = _last_read_ms;
                    xSemaphoreGive(_mutex);
                }
                return line;
            }
            if (line.length() >= 4096) {
                _set_error("Pool rsp too long");
                return "";
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return line;
}

bool StratumService::_subscribe() {
    const uint32_t id = _next_id();
    const String payload = "{\"id\": " + String(id) +
                           ", \"method\": \"mining.subscribe\", \"params\": [\"" +
                           _client_id + "\"]}\n";
    _record_pending(id, "mining.subscribe");
    if (!_send_line(payload)) {
        _set_error("Subscribe send fail");
        return false;
    }
    LOG_I("[stratum] Sending mining.subscribe: %s", payload.c_str());

    const uint32_t start_ms = millis();
    while ((millis() - start_ms) < kSubscribeTimeoutMs && !_stop_requested) {
        const String line = _read_line(100);
        if (line.isEmpty()) {
            continue;
        }

        DynamicJsonDocument doc(4096);
        const DeserializationError err = deserializeJson(doc, line);
        if (err) {
            LOG_E("[stratum] subscribe parse error: %s raw=%s", err.c_str(), line.c_str());
            continue;
        }

        if (doc["method"] == "mining.notify") {
            _handle_notify(doc);
            continue;
        }

        if ((doc["id"] | 0) != static_cast<int>(id)) {
            _handle_response(doc, line);
            continue;
        }

        if (!doc["result"].is<JsonArray>() || doc["result"].as<JsonArray>().size() < 3) {
            _set_error("Subscribe failed");
            return false;
        }

        const char* extranonce1 = doc["result"][1] | "";
        const uint8_t extranonce2_size = doc["result"][2] | 0;
        if (_mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
            _telemetry.subscribed = true;
            _telemetry.extranonce2_size = extranonce2_size;
            copy_text(_telemetry.extranonce1, sizeof(_telemetry.extranonce1), extranonce1);
            _extranonce2 = "0";
            _telemetry.last_update_ms = millis();
            xSemaphoreGive(_mutex);
        }
        LOG_I("[stratum] Pool subscribed extranonce1=%s extranonce2_size=%u",
              extranonce1,
              static_cast<unsigned>(extranonce2_size));
        return true;
    }

    _set_error("Subscribe timeout");
    return false;
}

bool StratumService::_authorize() {
    _authorize_id = _next_id();
    const String payload = "{\"id\": " + String(_authorize_id) +
                           ", \"method\": \"mining.authorize\", \"params\": [\"" +
                           _endpoint.user + "\", \"" + _endpoint.password + "\"]}\n";
    _record_pending(_authorize_id, "mining.authorize");
    if (!_send_line(payload)) {
        _set_error("Authorize send fail");
        return false;
    }
    LOG_I("[stratum] Sending mining.authorize: %s", payload.c_str());

    const uint32_t start_ms = millis();
    while ((millis() - start_ms) < kAuthorizeTimeoutMs && !_stop_requested) {
        const String line = _read_line(100);
        if (line.isEmpty()) {
            continue;
        }
        _handle_line(line);

        if (_mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
            const bool authorized = _telemetry.authorized;
            xSemaphoreGive(_mutex);
            if (authorized) {
                return true;
            }
        }
    }

    _set_error("Wrong stratum user!");
    return false;
}

bool StratumService::_configure_version_rolling() {
    const uint32_t id = _next_id();
    const String payload =
        "{\"id\": " + String(id) +
        ", \"method\": \"mining.configure\", \"params\": [[\"version-rolling\"], {\"version-rolling.mask\": \"ffffffff\"}]}\n";
    _record_pending(id, "mining.configure");
    if (!_send_line(payload)) {
        LOG_E("[stratum] mining.configure send failed");
        return false;
    }
    LOG_I("[stratum] Sending mining.configure: %s", payload.c_str());
    return true;
}

bool StratumService::_suggest_difficulty() {
    const uint32_t id = _next_id();
    const String payload = "{\"id\": " + String(id) +
                           ", \"method\": \"mining.suggest_difficulty\", \"params\": [" +
                           String(_pool_difficulty, 4) + "]}\n";
    _record_pending(id, "mining.suggest_difficulty");
    if (!_send_line(payload)) {
        LOG_E("[stratum] mining.suggest_difficulty send failed");
        return false;
    }
    LOG_I("[stratum] Sending mining.suggest_difficulty: %s", payload.c_str());
    return true;
}

void StratumService::_handle_line(const String& line) {
    DynamicJsonDocument doc(4096);
    const DeserializationError err = deserializeJson(doc, line);
    if (err) {
        LOG_E("[stratum] parse error: %s raw=%s", err.c_str(), line.c_str());
        return;
    }

    const char* method = doc["method"] | "";
    if (method[0] != '\0') {
        _handle_method(method, doc);
        return;
    }

    _handle_response(doc, line);
}

void StratumService::_handle_notify(DynamicJsonDocument& doc) {
    JsonArray params = doc["params"].as<JsonArray>();
    if (params.isNull() || params.size() < 9) {
        LOG_E("[stratum] mining.notify missing params");
        return;
    }

    PoolJobData job;
    job.id = String((const char*)params[0]);
    job.prevhash = String((const char*)params[1]);
    job.coinb1 = String((const char*)params[2]);
    job.coinb2 = String((const char*)params[3]);
    job.merkle_branch.set(params[4]);
    job.version = String((const char*)params[5]);
    job.nbits = String((const char*)params[6]);
    job.ntime = String((const char*)params[7]);
    job.clean_jobs = params[8] | false;

    _push_job(job);
    if (_new_job_sem != nullptr) {
        xSemaphoreGive(_new_job_sem);
    }

    LOG_I("[stratum] mining.notify job=%s clean=%u cache=%u diff=%s mask=0x%08lx",
          job.id.c_str(),
          job.clean_jobs ? 1u : 0u,
          static_cast<unsigned>(_telemetry.job_cache_size),
          formatNumber(static_cast<float>(_pool_difficulty), 5).c_str(),
          static_cast<unsigned long>(_telemetry.version_mask));
}

void StratumService::_handle_method(const char* method, DynamicJsonDocument& doc) {
    if (strcmp(method, "mining.notify") == 0) {
        _handle_notify(doc);
        return;
    }

    if (strcmp(method, "mining.set_difficulty") == 0) {
        JsonArray params = doc["params"].as<JsonArray>();
        if (!params.isNull() && params.size() > 0) {
            _set_pool_difficulty(params[0] | _pool_difficulty);
            LOG_I("[stratum] Pool difficulty set: %s", formatNumber(static_cast<float>(_pool_difficulty), 5).c_str());
        } else {
            LOG_W("[stratum] Pool difficulty not found in params");
        }
        return;
    }

    if (strcmp(method, "mining.set_version_mask") == 0) {
        JsonArray params = doc["params"].as<JsonArray>();
        if (!params.isNull() && params.size() > 0) {
            const char* mask = params[0] | "ffffffff";
            _set_version_mask(strtoul(mask, nullptr, 16), true);
            LOG_I("[stratum] Version mask set to %s", mask);
        } else {
            _set_version_mask(0xffffffff, false);
            LOG_W("[stratum] Version mask not found in params");
        }
        return;
    }

    if (strcmp(method, "mining.set_extranonce") == 0) {
        JsonArray params = doc["params"].as<JsonArray>();
        if (!params.isNull() && params.size() >= 2 && _mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
            copy_text(_telemetry.extranonce1, sizeof(_telemetry.extranonce1), params[0] | "");
            _telemetry.extranonce2_size = params[1] | 0;
            _extranonce2 = "0";
            _telemetry.last_update_ms = millis();
            xSemaphoreGive(_mutex);
            LOG_I("[stratum] Extranonce updated extranonce1=%s extranonce2_size=%u",
                  _telemetry.extranonce1,
                  static_cast<unsigned>(_telemetry.extranonce2_size));
        }
        return;
    }

    LOG_W("[stratum] unknown method=%s", method);
}

void StratumService::_handle_response(DynamicJsonDocument& doc, const String& raw) {
    const int id = doc["id"] | -1;
    if (id < 0) {
        if (doc.containsKey("error") && !doc["error"].isNull()) {
            _set_error("Pool error");
        }
        return;
    }

    PendingResponse pending{};
    bool known = false;
    if (_mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        auto it = _pending.find(static_cast<uint32_t>(id));
        if (it != _pending.end()) {
            pending = it->second;
            known = true;
        }
        xSemaphoreGive(_mutex);
    }

    const bool has_error = doc.containsKey("error") && !doc["error"].isNull();
    if (!known) {
        if (has_error) {
            LOG_E("[stratum] Unknown error response id=%d err=%s", id, _parse_stratum_error(raw).c_str());
        } else {
            LOG_D("[stratum] Response id=%d raw=%s", id, raw.c_str());
        }
        return;
    }

    if (pending.method == "mining.authorize") {
        const bool authorized = !has_error && (doc["result"] | false);
        if (_mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
            _telemetry.authorized = authorized;
            _telemetry.last_update_ms = millis();
            if (!authorized) {
                copy_text(_telemetry.last_error, sizeof(_telemetry.last_error), "Wrong stratum user!");
            }
            _pending.erase(static_cast<uint32_t>(id));
            xSemaphoreGive(_mutex);
        }
        LOG_I("[stratum] Authorization %s", authorized ? "success" : "failed");
        return;
    }

    if (pending.method == "mining.configure") {
        if (!has_error && doc["result"].is<JsonObject>()) {
            JsonObject result = doc["result"].as<JsonObject>();
            bool supported = result["version-rolling"] | false;
            uint32_t mask = 0xffffffff;
            if (supported && result.containsKey("version-rolling.mask")) {
                mask = strtoul((const char*)result["version-rolling.mask"], nullptr, 16);
            }
            _set_version_mask(mask, supported);
            LOG_I("[stratum] Version rolling %s mask=0x%08lx",
                  supported ? "supported" : "not supported",
                  static_cast<unsigned long>(mask));
        } else {
            _set_version_mask(0xffffffff, false);
            LOG_W("[stratum] Version rolling not supported");
        }
    } else if (pending.method == "mining.suggest_difficulty") {
        if (has_error) {
            if (_mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
                _telemetry.suggest_difficulty_supported = false;
                _pending.erase(static_cast<uint32_t>(id));
                xSemaphoreGive(_mutex);
            }
            LOG_W("[stratum] Pool doesn't support suggest_difficulty");
            return;
        }
    } else if (pending.method == "mining.submit") {
        const uint32_t latency = millis() - pending.stamp_ms;
        const bool accepted = !has_error && (doc["result"] | false);
        uint32_t total_shares = 0;
        if (_mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
            _telemetry.last_share_latency_ms = latency;
            if (accepted) {
                ++_telemetry.share_accepted;
            } else {
                ++_telemetry.share_rejected;
            }
            total_shares = _telemetry.share_accepted + _telemetry.share_rejected;
            _telemetry.last_update_ms = millis();
            _pending.erase(static_cast<uint32_t>(id));
            xSemaphoreGive(_mutex);
        }
        if (accepted) {
            LOG_L("#%lu share accepted, %lums",
                  static_cast<unsigned long>(total_shares),
                  static_cast<unsigned long>(latency));
        } else {
            LOG_E("#%lu share rejected, %lums, %s",
                  static_cast<unsigned long>(total_shares),
                  static_cast<unsigned long>(latency),
                  _parse_stratum_error(raw).c_str());
        }
        return;
    } else if (has_error) {
        LOG_E("[stratum] Error response id=%d method=%s err=%s",
              id,
              pending.method.c_str(),
              _parse_stratum_error(raw).c_str());
    }

    if (_mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        auto it = _pending.find(static_cast<uint32_t>(id));
        if (it != _pending.end()) {
            _pending.erase(it);
        }
        _telemetry.last_update_ms = millis();
        xSemaphoreGive(_mutex);
    }
}

void StratumService::_record_pending(uint32_t id, const char* method) {
    if (_mutex == nullptr || xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    PendingResponse response;
    response.method = method != nullptr ? method : "";
    response.status = false;
    response.stamp_ms = millis();
    _pending[id] = response;
    _clear_old_pending();
    xSemaphoreGive(_mutex);
}

void StratumService::_clear_old_pending() {
    while (_pending.size() > kMaxPendingResponseCache) {
        _pending.erase(_pending.begin());
    }
}

void StratumService::_push_job(const PoolJobData& job) {
    if (_mutex == nullptr || xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    if (job.clean_jobs) {
        _clear_job_cache_locked();
        if (_clear_job_sem != nullptr) {
            xSemaphoreGive(_clear_job_sem);
        }
    }
    while (_job_cache.size() >= _job_cache_max) {
        LOG_D("[stratum] Job [%s] popped from cache", _job_cache.front().id.c_str());
        _job_cache.pop_front();
    }
    _job_cache.push_back(job);

    _telemetry.job_received = true;
    ++_telemetry.job_counter;
    _telemetry.job_cache_size = static_cast<uint8_t>(_job_cache.size());
    copy_text(_telemetry.last_job_id, sizeof(_telemetry.last_job_id), job.id);
    _telemetry.last_update_ms = millis();
    xSemaphoreGive(_mutex);
}

void StratumService::_clear_job_cache_locked() {
    _job_cache.clear();
    _telemetry.job_cache_size = 0;
}

void StratumService::_set_pool_difficulty(double difficulty) {
    _pool_difficulty = difficulty;
    if (_mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _telemetry.pool_difficulty = difficulty;
        _telemetry.last_update_ms = millis();
        xSemaphoreGive(_mutex);
    }
}

void StratumService::_set_version_mask(uint32_t mask, bool version_rolling) {
    if (_mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _telemetry.version_mask = mask;
        _telemetry.version_rolling = version_rolling;
        _telemetry.last_update_ms = millis();
        xSemaphoreGive(_mutex);
    }
}

void StratumService::_set_error(const char* message) {
    if (_mutex == nullptr) {
        return;
    }

    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        copy_text(_telemetry.last_error, sizeof(_telemetry.last_error), message);
        _telemetry.connecting = false;
        _telemetry.last_update_ms = millis();
        xSemaphoreGive(_mutex);
    }
    if (message != nullptr && message[0] != '\0') {
        LOG_E("[stratum] %s", message);
    }
}

void StratumService::_reset_session_status() {
    if (_client != nullptr) {
        _client->stop();
    }
    _client = nullptr;
    _authorize_id = 0;

    if (_mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _telemetry.task_running = true;
        _telemetry.connecting = false;
        _telemetry.connected = false;
        _telemetry.subscribed = false;
        _telemetry.authorized = false;
        _telemetry.job_received = false;
        _telemetry.job_counter = 0;
        _telemetry.job_cache_size = 0;
        _telemetry.ssl = _endpoint.ssl;
        _telemetry.using_fallback = _using_fallback;
        _telemetry.port = _endpoint.port;
        _telemetry.pool_difficulty = _pool_difficulty;
        _telemetry.version_mask = 0xffffffff;
        _telemetry.version_rolling = false;
        _telemetry.suggest_difficulty_supported = true;
        _telemetry.extranonce2_size = 0;
        _telemetry.last_read_ms = _last_read_ms;
        _telemetry.last_write_ms = _last_write_ms;
        _telemetry.last_update_ms = millis();
        copy_text(_telemetry.host, sizeof(_telemetry.host), _endpoint.host);
        copy_text(_telemetry.user, sizeof(_telemetry.user), _endpoint.user);
        copy_text(_telemetry.extranonce1, sizeof(_telemetry.extranonce1), "");
        copy_text(_telemetry.last_job_id, sizeof(_telemetry.last_job_id), "");
        copy_text(_telemetry.last_error, sizeof(_telemetry.last_error), "");
        xSemaphoreGive(_mutex);
    }
}

void StratumService::_reset_protocol_state() {
    if (_mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _pending.clear();
        _job_cache.clear();
        _extranonce2 = "0";
        xSemaphoreGive(_mutex);
    }
    _request_id = 1;
    _authorize_id = 0;
}

bool StratumService::_hello_pool(uint32_t now_ms) {
    if (_client == nullptr || !_client->connected()) {
        return false;
    }

    _clear_old_pending();
    if ((now_ms - _last_write_ms) > kHelloPoolIntervalMs && _telemetry.suggest_difficulty_supported) {
        LOG_W("[stratum] Hello pool...");
        if (!_suggest_difficulty()) {
            return false;
        }
    }

    if ((now_ms - _last_read_ms) > kLostPoolTimeoutMs) {
        LOG_W("[stratum] It seems pool inactive, last received %lus ago",
              static_cast<unsigned long>((now_ms - _last_read_ms) / 1000));
        return false;
    }

    for (auto it = _pending.begin(); it != _pending.end(); ++it) {
        if (it->second.method == "mining.submit" && (now_ms - it->second.stamp_ms) > kSubmitTimeoutMs) {
            LOG_E("[stratum] Submit response timeout id=%lu", static_cast<unsigned long>(it->first));
            return false;
        }
    }
    return true;
}

bool StratumService::_primary_available() {
    if (!_primary_endpoint.valid || !_using_fallback) {
        return false;
    }

    const uint32_t now_ms = millis();
    const uint32_t interval_ms = kFallbackProbeMinMs + (esp_random() % (kFallbackProbeJitterMs + 1));
    if (_last_primary_probe_ms != 0 && (now_ms - _last_primary_probe_ms) < interval_ms) {
        return false;
    }
    _last_primary_probe_ms = now_ms;

    WiFiClient client;
    const bool connected = client.connect(_primary_endpoint.host.c_str(), _primary_endpoint.port, kPoolConnectTimeoutMs);
    if (connected) {
        client.stop();
    }
    LOG_I("[stratum] Primary pool probe %s:%u %s",
          _primary_endpoint.host.c_str(),
          static_cast<unsigned>(_primary_endpoint.port),
          connected ? "available" : "unavailable");
    return connected;
}

String StratumService::_parse_stratum_error(const String& raw) {
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, raw) != DeserializationError::Ok) {
        return "Unknown error";
    }
    if (!doc.containsKey("error") || doc["error"].isNull()) {
        return "Unknown error";
    }

    const int code = doc["error"][0] | 0;
    switch (code) {
        case 20: return "Other/Unknown";
        case 21: return "Stale share";
        case 22: return "Duplicate share";
        case 23: return "Low difficulty";
        case 24: return "Unauthorized worker";
        case 25: return "Not subscribed";
        default: return String("Error code ") + code;
    }
}

uint32_t StratumService::_next_id() {
    return _request_id++;
}

}  // namespace nm::services
