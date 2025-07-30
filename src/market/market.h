#ifndef _MARKET_H_
#define _MARKET_H_
#include <ArduinoHttpClient.h> 
#include <WiFiClient.h>



#define MARKET_HOST "data-api.binance.vision"
#define MARKET_PORT 80


class MarketClass{

private:

public:
    uint32_t lastUpdate;
    float    price;
    float    change_percent;
    MarketClass(){
        this->lastUpdate = 0;
    }
    ~MarketClass(){

    }
    // String get_coin_price(const String &symbol = "BTCUSDT");
    bool get_coin_ticker_24hr(const String &symbol = "BTCUSDT");
};

void market_thread_entry(void *args);
#endif


