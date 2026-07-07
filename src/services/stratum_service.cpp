#include "services/stratum_service.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <stdio.h>

#include "app/firmware_identity.h"
#include "app/task_config.h"

namespace nm::services {

namespace {

constexpr uint32_t kPoolConnectTimeoutMs = 4000;
constexpr uint32_t kSubscribeTimeoutMs = 10000;
constexpr uint32_t kAuthorizeTimeoutMs = 10000;
constexpr uint32_t kReconnectDelayMs = 5000;
constexpr uint32_t kSuggestDifficultyIntervalMs = 120000;
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
    if (_mutex == nullptr) {
        Serial.println("[stratum] mutex create failed");
        return;
    }

    Endpoint endpoint{};
    if (!_parse_endpoint(config.stratum.primary, endpoint)) {
        _set_error("Wrong pool URL!!!");
        return;
    }

    _endpoint = endpoint;
    _client_id = String(board.traits().display_name != nullptr ? board.traits().display_name : board.key()) +
                 "/" + app::kFirmwareVersion;
    _request_id = 1;
    _authorize_id = 0;
    _stop_requested = false;
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
        Serial.println("[stratum] failed to create task");
        _task = nullptr;
        _set_error("Stratum task fail");
        return;
    }

    _started = true;
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

bool StratumService::_parse_endpoint(const config::StratumEndpointConfig& config, Endpoint& endpoint) {
    String url = config.url;
    url.trim();
    if (url.isEmpty()) {
        return false;
    }

    endpoint.ssl = url.indexOf("stratum+ssl://") == 0 || url.indexOf("stratum+tls://") == 0;
    int scheme = url.indexOf("://");
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
    return !endpoint.host.isEmpty() && endpoint.port != 0 && !endpoint.user.isEmpty();
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
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        _connect_and_listen();
        if (!_stop_requested) {
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
}

void StratumService::_connect_and_listen() {
    _reset_session_status();
    if (!_connect_pool(_endpoint)) {
        return;
    }

    if (!_subscribe()) {
        return;
    }
    if (!_authorize()) {
        return;
    }

    _configure_version_rolling();
    _suggest_difficulty();
    uint32_t last_suggest_ms = millis();

    while (!_stop_requested && _client != nullptr && _client->connected() && WiFi.status() == WL_CONNECTED) {
        const String line = _read_line(100);
        if (!line.isEmpty()) {
            _handle_line(line);
        }

        if ((millis() - last_suggest_ms) >= kSuggestDifficultyIntervalMs) {
            _suggest_difficulty();
            last_suggest_ms = millis();
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
        _telemetry.port = endpoint.port;
        copy_text(_telemetry.host, sizeof(_telemetry.host), endpoint.host);
        copy_text(_telemetry.user, sizeof(_telemetry.user), endpoint.user);
        copy_text(_telemetry.last_error, sizeof(_telemetry.last_error), "");
        _telemetry.last_update_ms = millis();
        xSemaphoreGive(_mutex);
    }

    IPAddress pool_ip;
    Serial.printf(
        "[stratum] Resolve pool address %s use dns1: %s, dns2: %s\n",
        endpoint.host.c_str(),
        WiFi.dnsIP(0).toString().c_str(),
        WiFi.dnsIP(1).toString().c_str());
    if (WiFi.hostByName(endpoint.host.c_str(), pool_ip) != 1) {
        _set_error("Wrong pool URL!!!");
        return false;
    }

    Serial.printf("[stratum] Connecting %s:%u [%s]\n",
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

    if (_mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _telemetry.connecting = false;
        _telemetry.connected = true;
        _telemetry.last_update_ms = millis();
        xSemaphoreGive(_mutex);
    }
    Serial.printf("[stratum] Connected to pool %s:%u\n", endpoint.host.c_str(), static_cast<unsigned>(endpoint.port));
    return true;
}

bool StratumService::_send_line(const String& line) {
    if (_client == nullptr || !_client->connected()) {
        return false;
    }

    const size_t written = _client->print(line);
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
            if (c == '\n') {
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
    if (!_send_line(payload)) {
        _set_error("Subscribe send fail");
        return false;
    }
    Serial.printf("[stratum] Sending mining.subscribe: %s", payload.c_str());

    const uint32_t start_ms = millis();
    while ((millis() - start_ms) < kSubscribeTimeoutMs && !_stop_requested) {
        const String line = _read_line(100);
        if (line.isEmpty()) {
            continue;
        }

        DynamicJsonDocument doc(4096);
        if (deserializeJson(doc, line) != DeserializationError::Ok) {
            continue;
        }

        if (doc["method"] == "mining.notify") {
            _handle_line(line);
            continue;
        }

        if ((doc["id"] | 0) != static_cast<int>(id)) {
            _handle_line(line);
            continue;
        }

        if (!doc["result"].is<JsonArray>()) {
            _set_error("Subscribe failed");
            return false;
        }

        if (_mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
            _telemetry.subscribed = true;
            _telemetry.last_update_ms = millis();
            xSemaphoreGive(_mutex);
        }
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
    if (!_send_line(payload)) {
        _set_error("Authorize send fail");
        return false;
    }
    Serial.printf("[stratum] Sending mining.authorize: %s", payload.c_str());

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
    if (!_send_line(payload)) {
        Serial.println("[stratum] mining.configure send failed");
        return false;
    }
    Serial.printf("[stratum] Sending mining.configure: %s", payload.c_str());
    return true;
}

bool StratumService::_suggest_difficulty() {
    const uint32_t id = _next_id();
    const String payload = "{\"id\": " + String(id) +
                           ", \"method\": \"mining.suggest_difficulty\", \"params\": [" +
                           String(kDefaultInitialDifficulty, 4) + "]}\n";
    if (!_send_line(payload)) {
        Serial.println("[stratum] mining.suggest_difficulty send failed");
        return false;
    }
    Serial.printf("[stratum] Sending mining.suggest_difficulty: %s", payload.c_str());
    return true;
}

void StratumService::_handle_line(const String& line) {
    DynamicJsonDocument doc(4096);
    const DeserializationError err = deserializeJson(doc, line);
    if (err) {
        Serial.printf("[stratum] parse error: %s raw=%s\n", err.c_str(), line.c_str());
        return;
    }

    const char* method = doc["method"] | "";
    if (strcmp(method, "mining.notify") == 0) {
        uint32_t job_counter = 0;
        if (_mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
            _telemetry.job_received = true;
            _telemetry.job_counter++;
            job_counter = _telemetry.job_counter;
            _telemetry.last_update_ms = millis();
            xSemaphoreGive(_mutex);
        }
        Serial.printf("[stratum] mining.notify job_counter=%lu\n", static_cast<unsigned long>(job_counter));
        return;
    }

    if (strcmp(method, "mining.set_difficulty") == 0 ||
        strcmp(method, "mining.set_version_mask") == 0 ||
        strcmp(method, "mining.set_extranonce") == 0) {
        if (_mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
            _telemetry.last_update_ms = millis();
            xSemaphoreGive(_mutex);
        }
        return;
    }

    const int id = doc["id"] | -1;
    if (id == static_cast<int>(_authorize_id) && doc.containsKey("result")) {
        const bool authorized = doc["result"] | false;
        if (_mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
            _telemetry.authorized = authorized;
            _telemetry.last_update_ms = millis();
            if (!authorized) {
                copy_text(_telemetry.last_error, sizeof(_telemetry.last_error), "Wrong stratum user!");
            }
            xSemaphoreGive(_mutex);
        }
        Serial.printf("[stratum] Authorization %s\n", authorized ? "success" : "failed");
        return;
    }

    if (doc.containsKey("error") && !doc["error"].isNull()) {
        _set_error("Pool error");
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
        Serial.printf("[stratum] %s\n", message);
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
        _telemetry.ssl = _endpoint.ssl;
        _telemetry.port = _endpoint.port;
        _telemetry.last_update_ms = millis();
        copy_text(_telemetry.host, sizeof(_telemetry.host), _endpoint.host);
        copy_text(_telemetry.user, sizeof(_telemetry.user), _endpoint.user);
        copy_text(_telemetry.last_error, sizeof(_telemetry.last_error), "");
        xSemaphoreGive(_mutex);
    }
}

uint32_t StratumService::_next_id() {
    return _request_id++;
}

}  // namespace nm::services
