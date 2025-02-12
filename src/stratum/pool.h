#ifndef _POOL_H_
#define _POOL_H_
#include <WiFi.h>
#include <WiFiClientSecure.h>

#define  POOL_KEEP_ALIVE_TIME_MS        1000*30
#define  POOL_INACTIVITY_TIME_MS        1000*60*5

typedef struct{
    String      url;
    uint16_t    port;
    bool        ssl;
}pool_info_t; 

class PoolClass{
private:
    WiFiClientSecure        *_wificlientSecure;
    WiFiClient              *_wificlient;
    WiFiClient              *_pwclient;
    pool_info_t             _pool_cfg;
    uint32_t                _last_write;
    uint32_t                _last_read;
    IPAddress               _pool_ip;
    String                  _line;
    String                  _last_err_str;
public:
    PoolClass(){};
    PoolClass(pool_info_t config);
    ~PoolClass();
    bool begin(bool ssl);
    bool connect();
    void end();
    bool is_connected();
    bool available();
    size_t write(const String data);
    String readline(uint32_t timeout_ms = 1000);
    String get_last_errormsg();
    pool_info_t get_pool_info();
    uint32_t get_last_write_ms();
    uint32_t get_last_read_ms();
};













#endif