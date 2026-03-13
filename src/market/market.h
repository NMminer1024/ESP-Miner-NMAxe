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
    uint32_t                    _lastUpdate;
    CoinPrice                   _main_pair;
    std::map<String, CoinPrice> _watchlist_pairs;  // key = full symbol e.g. "ETHUSDT"
    // All USDT pairs discovered from Binance (populated by fetch_available_usdt_pairs).
    // Each entry is the full symbol e.g. "BTCUSDT", "ETHUSDT".
    std::vector<String>         _availablePairs;

public:
    MarketClass(){
        this->_lastUpdate = 0;
    }
    ~MarketClass(){

    }

    uint32_t get_last_update() const { return _lastUpdate; }
    const CoinPrice& get_main_pair() const { return _main_pair; }
    const std::map<String, CoinPrice>& get_watchlist_pairs() const { return _watchlist_pairs; }
    const std::vector<String>& get_available_pairs() const { return _availablePairs; }

    bool get_coin_ticker_24hr(const String &symbol, CoinPrice &out);

    // Fetch all mainstream USDT trading pairs from Binance, log them, and populate availablePairs.
    // Uses HTTP streaming to avoid loading the full ~100 KB response into RAM.
    bool fetch_available_usdt_pairs();

    // Fetch <coin_symbol>USDT ticker, update main_pair and lastUpdate. Returns true on success.
    bool refresh_main_pair(const String &coin_symbol);

    // Parse a comma-separated coin_watchlist string (e.g. "BTC,ETH,BNB"),
    // fetch 24 hr ticker for each <symbol>USDT pair, and refresh watchlist_pairs.
    void refresh_watchlist(const String &coin_watchlist);
};

// void market_thread_entry(void *args);
#endif

