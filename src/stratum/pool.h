#ifndef _POOL_H_
#define _POOL_H_
#include <WiFi.h>
#include <WiFiClientSecure.h>

#define  POOL_KEEP_ALIVE_TIME_MS        1000*30
#define  POOL_INACTIVITY_TIME_MS        1000*60*5

typedef struct pool_info_s {
    String      url;
    uint16_t    port;
    bool        ssl;
    
    bool operator==(const pool_info_s& other) const {
        return url == other.url && 
               port == other.port && 
               ssl == other.ssl;
    }
    
    bool operator!=(const pool_info_s& other) const {
        return !(*this == other);
    }
} pool_info_t;

// Parse "stratum+tcp://host:port" / "stratum+ssl://host:port" into a pool_info_t.
// Returns false when the string has no scheme separator or a zero port, so
// callers (boot init / web hot-reload) can reject malformed URLs consistently.
static inline bool parse_pool_url(const String& url_str, pool_info_t& out) {
    const int scheme_sep = url_str.indexOf("://");
    if (scheme_sep < 0) return false;
    const int last_colon = url_str.lastIndexOf(":");
    if (last_colon <= scheme_sep + 2) return false;

    out.ssl  = (url_str.indexOf("ssl") != -1) || (url_str.indexOf("tls") != -1);
    out.url  = url_str.substring(scheme_sep + 3, last_colon);
    out.port = url_str.substring(last_colon + 1).toInt();
    return out.url.length() > 0 && out.port > 0;
}

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