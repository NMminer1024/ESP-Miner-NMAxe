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
        LOG_E("Failed to get 24hr ticker data.");
    }
    return false;
}




void market_thread_entry(void *args){
    char *name = (char*)malloc(20);
    strcpy(name, (char*)args);
    LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
    // free(name);

    while (g_board.market == NULL){
        LOG_W("MarketClass instance is NULL, waiting...");
        delay(1000);
    }   

    g_board.market->lastUpdate = 0;
    
    while(true){
        if(g_board.info.connection.wifi.status_param.status == WL_CONNECTED){
            // Fetch the 24hr ticker data for the coin
            bool res = g_board.market->get_coin_ticker_24hr(g_board.info.base.coin_price + "USDT");
            g_board.market->lastUpdate = (res) ? millis() : g_board.market->lastUpdate;
        }
        delay(MARKET_UPDATE_INTERVAL);
    }

    delete g_board.market;
    g_board.market = nullptr;
    LOG_W("Market thread exit.");
    vTaskDelete(NULL);
}





