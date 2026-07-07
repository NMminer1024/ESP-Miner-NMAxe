// What: Concrete WiFi connection service.
// Why: The legacy loading page waited for STA connection before ASIC init; this
// restores that behavior in the BSP-first framework.
#include "services/wifi_service.h"

#include <Arduino.h>
#include <WiFi.h>
#include <stdio.h>

namespace nm::services {

namespace {
constexpr uint32_t kStaFallbackMs = 15000;
constexpr uint32_t kRetryLogPeriodMs = 1000;

void copy_string(char* dest, size_t dest_size, const String& value) {
    if (dest == nullptr || dest_size == 0) {
        return;
    }

    snprintf(dest, dest_size, "%s", value.c_str());
}

void copy_ip(char* dest, size_t dest_size, const IPAddress& ip) {
    if (dest == nullptr || dest_size == 0) {
        return;
    }

    const String text = ip.toString();
    snprintf(dest, dest_size, "%s", text.c_str());
}

}  // namespace

void WifiService::start(
    const config::AppConfig& config,
    state::RuntimeState& runtime,
    system::EventFlags& events) {
    _config = &config;
    _runtime = &runtime;
    _events = &events;
    _stage_started_ms = millis();
    _last_retry_log_ms = 0;

    runtime.network = {};
    runtime.network.force_config = config.network.force_config;
    _copy_network_identity();

    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_15dBm);
    if (!config.network.hostname.isEmpty()) {
        WiFi.setHostname(config.network.hostname.c_str());
    }

    if (config.network.force_config || config.network.sta_ssid.isEmpty()) {
        _start_ap();
        return;
    }

    _start_sta();
}

void WifiService::poll() {
    if (_config == nullptr || _runtime == nullptr || _events == nullptr) {
        return;
    }

    switch (_stage) {
        case Stage::Idle:
        case Stage::StaConnected:
        case Stage::Fault:
            return;

        case Stage::StaDelay:
            if (millis() >= _connect_after_ms) {
                Serial.printf("[wifi] Try to connect [%s]...\n", _config->network.sta_ssid.c_str());
                WiFi.begin(_config->network.sta_ssid.c_str(), _config->network.sta_password.c_str());
                _stage = Stage::StaConnecting;
                _stage_started_ms = millis();
                _last_retry_log_ms = 0;
            }
            return;

        case Stage::StaConnecting: {
            const wl_status_t status = WiFi.status();
            if (status == WL_CONNECTED) {
                _publish_sta_connected();
                _stage = Stage::StaConnected;
                return;
            }

            const uint32_t now_ms = millis();
            _publish_disconnected(static_cast<uint8_t>(status));

            if (_last_retry_log_ms == 0 || (now_ms - _last_retry_log_ms) >= kRetryLogPeriodMs) {
                const uint32_t elapsed_s = (now_ms - _stage_started_ms) / 1000u;
                Serial.printf("[wifi] Try to connect [%s] %us...\n", _config->network.sta_ssid.c_str(), elapsed_s);
                _last_retry_log_ms = now_ms;
            }

            if ((now_ms - _stage_started_ms) >= kStaFallbackMs) {
                _start_ap();
            }
            return;
        }

        case Stage::ApStarting:
            if (millis() >= _ap_ready_after_ms) {
                _stage = Stage::ApReady;
                _stage_started_ms = millis();
                _runtime->network.connecting = false;
                _runtime->network.ap_ready = true;
                _runtime->network.client_connected = WiFi.softAPgetStationNum() > 0;
                _events->set(system::Event::NetworkStateChanged);
            }
            return;

        case Stage::ApReady:
            _runtime->network.client_connected = WiFi.softAPgetStationNum() > 0;
            _events->set(system::Event::NetworkStateChanged);
            return;
    }
}

bool WifiService::sta_connected() const {
    return _stage == Stage::StaConnected ||
           (_runtime != nullptr && _runtime->network.sta_connected);
}

bool WifiService::ap_ready() const {
    return _stage == Stage::ApReady ||
           (_runtime != nullptr && _runtime->network.ap_ready);
}

void WifiService::_start_sta() {
    if (_config == nullptr || _runtime == nullptr || _events == nullptr) {
        return;
    }

    _stage = Stage::StaConnecting;
    _stage_started_ms = millis();
    _last_retry_log_ms = 0;
    _runtime->network.connecting = true;
    _runtime->network.sta_connected = false;
    _runtime->network.ap_ready = false;
    _runtime->network.status = static_cast<uint8_t>(WL_DISCONNECTED);
    _copy_network_identity();
    _events->set(system::Event::NetworkStateChanged);

    const uint16_t random_delay_ms = static_cast<uint16_t>(random(0, 1000 * 8));
    Serial.printf("[wifi] Initializing WiFi, delay: %ums...\n", random_delay_ms);
    _connect_after_ms = _stage_started_ms + random_delay_ms;
    _stage = Stage::StaDelay;
}

void WifiService::_start_ap() {
    if (_config == nullptr || _runtime == nullptr || _events == nullptr) {
        return;
    }

    const char* ap_ssid = !_config->network.ap_ssid.isEmpty()
        ? _config->network.ap_ssid.c_str()
        : _config->network.hostname.c_str();
    Serial.printf("[wifi] Set softAP [%s]...\n", ap_ssid);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));

    _stage = Stage::ApStarting;
    _stage_started_ms = millis();
    _ap_ready_after_ms = _stage_started_ms + 500u;
    _runtime->network.connecting = true;
    _runtime->network.sta_connected = false;
    _runtime->network.ap_ready = false;
    _runtime->network.client_connected = false;
    _runtime->network.status = static_cast<uint8_t>(WL_DISCONNECTED);
    copy_string(_runtime->network.ap_ssid, sizeof(_runtime->network.ap_ssid), String(ap_ssid));
    copy_ip(_runtime->network.ip, sizeof(_runtime->network.ip), IPAddress(192, 168, 4, 1));
    _events->set(system::Event::NetworkStateChanged);
}

void WifiService::_publish_sta_connected() {
    if (_runtime == nullptr || _events == nullptr) {
        return;
    }

    _runtime->network.connecting = false;
    _runtime->network.sta_connected = true;
    _runtime->network.ap_ready = false;
    _runtime->network.status = static_cast<uint8_t>(WL_CONNECTED);
    _runtime->network.rssi_dbm = WiFi.RSSI();
    _runtime->network.channel = static_cast<uint8_t>(WiFi.channel());
    copy_ip(_runtime->network.ip, sizeof(_runtime->network.ip), WiFi.localIP());
    copy_ip(_runtime->network.gateway, sizeof(_runtime->network.gateway), WiFi.gatewayIP());
    copy_ip(_runtime->network.subnet, sizeof(_runtime->network.subnet), WiFi.subnetMask());
    copy_ip(_runtime->network.dns, sizeof(_runtime->network.dns), WiFi.dnsIP());

    Serial.println("[wifi] ------------------------------------");
    Serial.printf("[wifi] SSID     : %s\n", WiFi.SSID().c_str());
    Serial.printf("[wifi] IP       : %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[wifi] RSSI     : %d dBm\n", WiFi.RSSI());
    Serial.printf("[wifi] Channel  : %d\n", WiFi.channel());
    Serial.printf("[wifi] Gateway  : %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("[wifi] Subnet   : %s\n", WiFi.subnetMask().toString().c_str());
    Serial.printf("[wifi] MAC      : %s\n", WiFi.macAddress().c_str());
    Serial.printf("[wifi] Hostname : %s\n", WiFi.getHostname());
    Serial.println("[wifi] ------------------------------------");

    _events->set(system::Event::NetworkStateChanged);
}

void WifiService::_publish_disconnected(uint8_t status) {
    if (_runtime == nullptr || _events == nullptr) {
        return;
    }

    _runtime->network.connecting = true;
    _runtime->network.sta_connected = false;
    _runtime->network.status = status;
    _runtime->network.rssi_dbm = 0;
    _runtime->network.channel = 0;
    _runtime->network.ip[0] = '\0';
    _runtime->network.gateway[0] = '\0';
    _runtime->network.subnet[0] = '\0';
    _runtime->network.dns[0] = '\0';
    _events->set(system::Event::NetworkStateChanged);
}

void WifiService::_copy_network_identity() {
    if (_config == nullptr || _runtime == nullptr) {
        return;
    }

    copy_string(_runtime->network.ssid, sizeof(_runtime->network.ssid), _config->network.sta_ssid);
    copy_string(_runtime->network.hostname, sizeof(_runtime->network.hostname), _config->network.hostname);
    copy_string(_runtime->network.ap_ssid, sizeof(_runtime->network.ap_ssid), _config->network.ap_ssid);
}

}  // namespace nm::services
