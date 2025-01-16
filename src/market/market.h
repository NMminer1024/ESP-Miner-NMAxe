#ifndef _MARKET_H_
#define _MARKET_H_
#include <WebSocketsClient.h>

#define MARKET_URL "data-stream.binance.vision"
#define MARKET_PORT 443
#define MARKET_PATH "/ws/btcusdt@avgPrice"


class MarketClass
{
private:
    WebSocketsClient  *_wsclient;
public:
    bool  updated;
    bool  timeout;
    float price;

    MarketClass(String host, uint16_t port, String url);
    ~MarketClass();
    void loop();
};

void market_thread_entry(void *args);
#endif