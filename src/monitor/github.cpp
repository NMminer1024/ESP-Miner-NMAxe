#include "github.h"
#include "logger.h"

ReleaseCheckerClass::ReleaseCheckerClass() {
    String path = "/repos/NMminer1024/NMMiner/releases/latest";
    this->_sslwifi.setInsecure(); 
    this->_httpclient = new HttpClient(_sslwifi, "api.github.com", 443);
}

String ReleaseCheckerClass::get_latest_release(){
    String path = "/repos/NMminer1024/ESP-Miner-NMAxe/releases/latest";
    
    this->_httpclient->beginRequest();
    this->_httpclient->get(path);
    this->_httpclient->sendHeader("User-Agent", "ESP32-ReleaseChecker");
    this->_httpclient->sendHeader("Accept", "application/json");
    this->_httpclient->endRequest();


    int statusCode = this->_httpclient->responseStatusCode();
    if (statusCode != 200) {
        LOG_E("Github connection failed, status code: %d", statusCode);
        return "None";
    }

    String response = this->_httpclient->responseBody();
    DynamicJsonDocument doc(1024*5);
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
        LOG_E("JSON parse failed: %s", error.c_str());
        return "None";
    }

    String version = doc["tag_name"].as<String>();

    int startIndex = version.indexOf('v');
    if (startIndex != -1) {
        int endIndex = version.indexOf('-', startIndex); 
        if (endIndex == -1) {
            endIndex = version.length();
        }
        return version.substring(startIndex, endIndex); 
    }
    return "None";
}
