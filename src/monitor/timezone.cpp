#include "timezone.h"
#include "logger.h"

TimezoneFetcher::TimezoneFetcher() {
    _sslClient.setInsecure(); 
    _httpClient = new HttpClient(_sslClient, "ipapi.co", 443);
}

TimezoneFetcher::~TimezoneFetcher() {
    delete _httpClient;
}

bool TimezoneFetcher::fetch() {
    if (WiFi.status() != WL_CONNECTED) {
        LOG_E("WiFi not connected");
        return false;
    }

    _httpClient->beginRequest();
    _httpClient->get("/json/");
    _httpClient->sendHeader("User-Agent", "ESP32-TimezoneFetcher");
    _httpClient->sendHeader("Accept", "application/json");
    _httpClient->endRequest();

    int statusCode = _httpClient->responseStatusCode();
    if (statusCode != 200) {
        LOG_D("ipapi.co connection failed, status code: %d", statusCode);
        return false;
    }

    String response = _httpClient->responseBody();
    LOG_D("Timezone fetch response: %d bytes, %s", response.length(), response.c_str());
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
        LOG_E("JSON parse failed: %s", error.c_str());
        return false;
    }

    if (doc.containsKey("utc_offset")) {
        String offset = doc["utc_offset"].as<String>(); // "+0800"
        float hours = 0.0f;
        if (offset.length() == 5) {
            int sign = (offset[0] == '-') ? -1 : 1;
            int h = offset.substring(1, 3).toInt();
            int m = offset.substring(3, 5).toInt();
            hours = sign * (h + m / 60.0f);
        }
        timezone = String(hours, (hours * 10) == int(hours * 10) ? 1 : 2); 
        return true;
    }
    return false;
}