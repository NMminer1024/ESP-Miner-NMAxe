#ifndef _GITHUB_H_
#define _GITHUB_H_
#include <WiFi.h>
#include <ArduinoHttpClient.h> 
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

class ReleaseCheckerClass{
private:
    WiFiClientSecure  _sslwifi;
    HttpClient*       _httpclient;
public:
    ReleaseCheckerClass();
    ~ReleaseCheckerClass(){
        delete this->_httpclient;
    }
    String get_latest_release();
};
#endif