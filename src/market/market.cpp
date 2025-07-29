// #include "market.h"
// #include "logger.h"
// #include <ArduinoJson.h>
// #include "global.h"

// #define MARKET_TIMEOUT (1000*50)

// static void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
//     if(g_nmaxe.market == NULL) return;

//     switch (type) {
//         case WStype_DISCONNECTED:
//             LOG_W("Market Disconnected");
//             break;
//         case WStype_CONNECTED:
//             LOG_I("Market Connected");
//             g_nmaxe.market->subscribe();
//             break;
//         case WStype_TEXT:{
//                 StaticJsonDocument<512> doc;
//                 DeserializationError error = deserializeJson(doc, payload, length);
//                 if (error == DeserializationError::Ok) {
//                     if(doc.containsKey("result")){
//                         if(doc["result"].containsKey("last")){
//                             g_nmaxe.market->updated = true;
//                             g_nmaxe.market->price = doc["result"]["last"].as<float>();
//                             LOG_D("%s_USDT price: %f", g_nmaxe.coin.c_str(), g_nmaxe.market->price);
//                         }
//                     }
//                 } else {
//                     LOG_W("Failed to parse JSON");
//                 }
//             }
//             break;
//         case WStype_BIN:
//             break;
//         case WStype_PING:
//             LOG_I("Market PING");
//             break;
//         case WStype_PONG:
//             LOG_I("Market PONG");
//             break;
//         default:
//             break;
//     }
// }

// MarketClass::MarketClass(String host, uint16_t port, String url, String symbol) {
//     this->timeout = false;
//     this->updated = false;
//     this->_host = host;
//     this->_port = port;
//     this->_url = url;
//     this->_wsclient = new WebSocketsClient();

//     StaticJsonDocument<256> doc;
//     doc["time"] = millis();
//     doc["channel"] = "spot.tickers";
//     doc["event"]   = "subscribe";
//     JsonArray payload = doc.createNestedArray("payload");
//     payload.add(symbol);
//     serializeJson(doc, this->_subscribeMessage);
// }

// MarketClass::~MarketClass(){
//     delete this->_wsclient;
// }

// bool MarketClass::connect(){
//     if(this->_wsclient == NULL) return false;
//     this->_wsclient->onEvent(onWebSocketEvent);
//     this->_wsclient->setReconnectInterval(2000);
//     this->_wsclient->beginSSL(this->_host, this->_port, this->_url);
//     return true;
// }

// bool MarketClass::subscribe(){
//     return this->_wsclient->sendTXT(this->_subscribeMessage);
// }


// void MarketClass::loop(){
//     this->_wsclient->loop();
// }

// void market_thread_entry(void *args){
//     char *name = (char*)malloc(20);
//     strcpy(name, (char*)args);
//     LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
//     free(name);

//     while (g_nmaxe.market == NULL){
//         LOG_W("MarketClass instance is NULL, waiting...");
//         delay(1000);
//     }

//     g_nmaxe.market->connect();

//     while(true){
//         if(WL_CONNECTED == g_nmaxe.connection.wifi.status_param.status){
//             g_nmaxe.market->loop();
//         }

//         if(!g_nmaxe.market->updated){
//             static uint16_t start = millis();
//             if(millis() - start > MARKET_TIMEOUT){
//                 g_nmaxe.market->timeout = true;
//             }
//         }

//         if(g_nmaxe.ota.ota_running)break;
//         delay(250);
//     }

//     g_nmaxe.market->~MarketClass();
//     LOG_W("Market thread exit.");
//     vTaskDelete(NULL);
// }








#include "market.h"
#include "logger.h"
#include <ArduinoJson.h>
#include "global.h"



// https://developers.binance.com/docs/zh-CN/binance-spot-api-docs/rest-api/market-data-endpoints

// String MarketClass::get_coin_price(const String &symbol) {
//     WiFiClient         _wificlient;
//     HttpClient httpclient(_wificlient, MARKET_HOST, MARKET_PORT);
//     String url = "/api/v3/avgPrice?symbol=" + symbol;
//     httpclient.get(url);

//     if (httpclient.responseStatusCode() == 200) {
//         StaticJsonDocument<128> doc;
//         DeserializationError error = deserializeJson(doc, httpclient.responseBody());
//         if (!error) {
//             return doc["price"].as<String>();
//         }
//     }
//     return "";
// }

bool MarketClass::get_coin_ticker_24hr(const String &symbol) {
    WiFiClient _wificlient;
    HttpClient httpclient(_wificlient, MARKET_HOST, MARKET_PORT);
    String url = "/api/v3/ticker/24hr?symbol=" + symbol;
    httpclient.get(url);

    if (httpclient.responseStatusCode() == 200) {
        StaticJsonDocument<800> doc;
        DeserializationError error = deserializeJson(doc, httpclient.responseBody());
        if (!error) {
            this->price          = doc["lastPrice"].as<String>().toFloat();
            this->change_percent = doc["priceChangePercent"].as<String>().toFloat();
            LOG_D("24hr ticker for %s: Price = %.2f, Change Percent = %.2f%%", symbol.c_str(), this->price, this->change_percent);
            return true;
        }
    }
    else{
        LOG_E("Failed to get 24hr ticker data, err = %s", httpclient.responseBody().c_str());
    }
    return false;
}




void market_thread_entry(void *args){
    char *name = (char*)malloc(20);
    strcpy(name, (char*)args);
    LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
    // free(name);

    while (g_nmaxe.market == NULL){
        LOG_W("MarketClass instance is NULL, waiting...");
        delay(1000);
    }   

    g_nmaxe.market->lastUpdate = 0;
    
    while(true){
        delay(MARKET_UPDATE_INTERVAL);
        if(g_nmaxe.connection.wifi.status_param.status != WL_CONNECTED) continue;
        
        // Fetch the 24hr ticker data for the coin
        bool res = g_nmaxe.market->get_coin_ticker_24hr(g_nmaxe.coin + "USDT");
        g_nmaxe.market->lastUpdate = (res) ? millis() : g_nmaxe.market->lastUpdate;
    }

    delete g_nmaxe.market;
    g_nmaxe.market = nullptr;
    LOG_W("Market thread exit.");
    vTaskDelete(NULL);
}





