#ifndef _MARKET_H_
#define _MARKET_H_
#include <WebSocketsClient.h>



class marketClass
{
private:
    WebSocketsClient  *_wsclient;
public:
    marketClass(String host, uint16_t port, String url);
    ~marketClass();

    void loop();
};

extern marketClass  g_market;

void market_thread_entry(void *args);
#endif