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
