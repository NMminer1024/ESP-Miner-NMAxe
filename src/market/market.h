#ifndef _MARKET_H_
#define _MARKET_H_
// Using ESP32 built-in HTTPClient instead of ArduinoHttpClient for better stability.
// HTTPClient has explicit timeout support and avoids indefinite blocking on slow servers.
#include <Arduino.h>
#include <map>
#include <vector>

#define MARKET_HOST            "data-api.binance.vision"
#define MARKET_PORT            "80"
#define MARKET_HTTP_TIMEOUT_MS 8000   // 8 s connection + read timeout

struct CoinPrice {
    float price      = 0.0f;
    float change_pct = 0.0f;  // 24 h change percent
};

class MarketClass{

private:

public:
    uint32_t                    lastUpdate;
    CoinPrice                   main_pair;
    std::map<String, CoinPrice> watchlist_pairs;  // key = full symbol e.g. "ETHUSDT"
    // All USDT pairs discovered from Binance (populated by fetch_available_usdt_pairs).
    // Each entry is the full symbol e.g. "BTCUSDT", "ETHUSDT".
    std::vector<String>         availablePairs;

    MarketClass(){
        this->lastUpdate = 0;
    }
    ~MarketClass(){

    }
    bool get_coin_ticker_24hr(const String &symbol, CoinPrice &out);

    // Fetch all mainstream USDT trading pairs from Binance, log them, and populate availablePairs.
    // Uses HTTP streaming to avoid loading the full ~100 KB response into RAM.
    bool fetch_available_usdt_pairs();
};

// void market_thread_entry(void *args);
#endif

