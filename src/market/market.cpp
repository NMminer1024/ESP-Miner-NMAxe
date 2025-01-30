#include "market.h"
#include "logger.h"
#include <ArduinoJson.h>
#include "global.h"

#define MARKET_TIMEOUT (1000*50)

static void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
    if(g_nmaxe.market == NULL) return;

    switch (type) {
        case WStype_DISCONNECTED:
            LOG_W("Market Disconnected");
            break;
        case WStype_CONNECTED:
            LOG_I("Market Connected");
            break;
        case WStype_TEXT:{
                StaticJsonDocument<128> doc;
                DeserializationError error = deserializeJson(doc, payload, length);
                if (error == DeserializationError::Ok) {
                    const char* event = doc["e"];
                    if (strcmp(event, "avgPrice") == 0) {
                        g_nmaxe.market->updated = true;
                        g_nmaxe.market->price = doc["w"].as<float>();
                    }
                } else {
                    LOG_W("Failed to parse JSON");
                }
            }
            break;
        case WStype_BIN:
            break;
        case WStype_PING:
            break;
        case WStype_PONG:
            break;
        default:
            break;
    }
}

MarketClass::MarketClass(String host, uint16_t port, String url){   
    this->timeout = false;
    this->updated = false;
    this->_wsclient = new WebSocketsClient();
    this->_wsclient->onEvent(onWebSocketEvent);
    this->_wsclient->beginSSL(host, port, url);
    this->_wsclient->setReconnectInterval(5000);
}

MarketClass::~MarketClass(){
    delete this->_wsclient;
}

void MarketClass::loop(){
    this->_wsclient->loop();
}

void market_thread_entry(void *args){
    char *name = (char*)malloc(20);
    strcpy(name, (char*)args);
    LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
    free(name);

    while (g_nmaxe.market == NULL){
        LOG_W("MarketClass instance is NULL, waiting...");
        delay(1000);
    }

    while(true){
        if(WL_CONNECTED == g_nmaxe.connection.wifi.status_param.status){
            g_nmaxe.market->loop();
        }

        if(!g_nmaxe.market->updated){
            static uint16_t start = millis();
            if(millis() - start > MARKET_TIMEOUT){
                g_nmaxe.market->timeout = true;
            }
        }

        if(g_nmaxe.ota.ota_running)break;
        delay(200);
    }

    g_nmaxe.market->~MarketClass();
    LOG_W("Market thread exit.");
    vTaskDelete(NULL);
}