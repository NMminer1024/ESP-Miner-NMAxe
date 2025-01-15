#ifndef _MARKET_H_
#define _MARKET_H_
#include <WebSocketsClient.h>


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