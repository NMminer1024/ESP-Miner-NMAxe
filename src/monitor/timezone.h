#ifndef _TIMEZONE_H_
#define _TIMEZONE_H_

#include <WiFi.h>
#include <ArduinoHttpClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

class TimezoneFetcher {
private:
    WiFiClientSecure _sslClient;
    HttpClient* _httpClient;
public:
    String timezone;

    TimezoneFetcher();
    ~TimezoneFetcher();

    bool fetch();
};

#endif