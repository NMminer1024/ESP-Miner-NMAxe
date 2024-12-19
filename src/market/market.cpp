#include "market.h"
#include "logger.h"
#include <ArduinoJson.h>
#include "global.h"

void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            LOG_W("WebSocket Disconnected");
            break;
        case WStype_CONNECTED:
            LOG_I("WebSocket Connected");
            break;
        case WStype_TEXT:{
                StaticJsonDocument<128> doc;
                DeserializationError error = deserializeJson(doc, payload, length);
                if (error == DeserializationError::Ok) {
                    const char* event = doc["e"];
                    if (strcmp(event, "avgPrice") == 0) {
                        g_nmaxe.market.connected = true;
                        g_nmaxe.market.price = String(doc["w"].as<float>(), 1).toFloat();
                    }
                } else {
                    LOG_W("Failed to parse JSON");
                }
            }
            break;
        case WStype_BIN:
            LOG_W("Binary data received");
            break;
        case WStype_PING:
            LOG_D("Ping received");
            break;
        case WStype_PONG:
            LOG_D("Pong received");
            break;
    }
}


marketClass::marketClass(String host, uint16_t port, String url){
    this->_wsclient = new WebSocketsClient();
    this->_wsclient->onEvent(onWebSocketEvent);
    this->_wsclient->beginSSL(host, port, url);
    this->_wsclient->setReconnectInterval(5000);
}

marketClass::~marketClass(){
    delete this->_wsclient;
}


void marketClass::loop(){
    this->_wsclient->loop();
}

static marketClass  market = marketClass("data-stream.binance.vision", 443, "/ws/btcusdt@avgPrice");

void market_thread_entry(void *args){
    char *name = (char*)malloc(20);
    strcpy(name, (char*)args);
    LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
    free(name);


    while(true){
        if(g_nmaxe.connection.wifi.status_param.status == WL_CONNECTED){
            market.loop();
        }

        if(g_nmaxe.ota.ota_running)break;

        delay(100);
    }
    LOG_W("Market thread exit.");
    vTaskDelete(NULL);
}