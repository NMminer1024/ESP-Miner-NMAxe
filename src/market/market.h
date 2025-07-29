// #ifndef _MARKET_H_
// #define _MARKET_H_
// #include <WebSocketsClient.h>

// //API reference: https://www.gate.io/docs/developers/apiv4/ws/en/#tickers-channel
// #define MARKET_HOST "api.gateio.ws"
// #define MARKET_URL  "/ws/v4/"
// #define MARKET_PORT 443

// class MarketClass
// {
// private:
//     WebSocketsClient  *_wsclient;
//     String             _host, _url;
//     uint16_t           _port;
//     String             _subscribeMessage;
// public:
//     bool   updated;
//     bool   timeout;
//     float  price;
//     MarketClass(String host, uint16_t port, String url, String cpair);
//     ~MarketClass();
//     bool connect();
//     bool subscribe();
//     void loop();
// };

// void market_thread_entry(void *args);
// #endif




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


