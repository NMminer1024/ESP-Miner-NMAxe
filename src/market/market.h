#ifndef _MARKET_H_
#define _MARKET_H_
// Using ESP32 built-in HTTPClient instead of ArduinoHttpClient for better stability.
// HTTPClient has explicit timeout support and avoids indefinite blocking on slow servers.
#include <Arduino.h>
#include <map>
#include <vector>
#include "utils/helper.h"

#define MARKET_HOST            "data-api.binance.vision"
#define MARKET_PORT            "80"
#define MARKET_HTTP_TIMEOUT_MS 8000   // 8 s connection + read timeout
// PSRAM flat buffer capacity for all USDT pair names ('\n'-separated symbols).
// 500 pairs x avg ~9 chars each ~= 4.5 KB; 8 KB gives comfortable headroom.
#define MARKET_PAIRS_BUF_CAP   (8 * 1024)

struct CoinPrice {
    float price      = 0.0f;
    float change_pct = 0.0f;  // 24 h change percent
};

class MarketClass{
    // Watchlist map stored in PSRAM (map nodes allocated via PsramAllocator).
    using WatchlistMap = std::map<String, CoinPrice, std::less<String>,
                                  PsramAllocator<std::pair<const String, CoinPrice>>>;
public:
    // Watchlist sorted by price descending; populated by sort_watchlist_by_price().
    using SortedWatchlist = std::vector<std::pair<String, CoinPrice>,
                                        PsramAllocator<std::pair<String, CoinPrice>>>;
private:
    uint32_t                    _lastUpdate;
    CoinPrice                   _main_pair;
    WatchlistMap                _watchlist_pairs;  // key = full symbol e.g. "ETHUSDT"
    SortedWatchlist             _sortedWatchlist;  // price-descending sorted copy (PSRAM)
    // All USDT pairs stored as a flat PSRAM buffer: "BTCUSDT\nETHUSDT\n..."
    char*                       _pairsBuf;
    size_t                      _pairsBufLen;
    uint16_t                    _pairsCount;
    volatile bool               _needs_refresh;

public:
    MarketClass() : _pairsBuf(nullptr), _pairsBufLen(0), _pairsCount(0),
                    _lastUpdate(0), _needs_refresh(false) {
        _pairsBuf = (char*)heap_caps_malloc(MARKET_PAIRS_BUF_CAP,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }

    // Signal the market thread to refresh immediately (call after NVS coin settings change).
    void request_refresh() { _needs_refresh = true; }
    // Returns true and clears the flag if a refresh was requested.
    bool consume_refresh_request() {
        if (!_needs_refresh) return false;
        _needs_refresh = false;
        return true;
    }
    ~MarketClass(){
        if (_pairsBuf) { heap_caps_free(_pairsBuf); _pairsBuf = nullptr; }
    }

    uint32_t get_last_update() const { return _lastUpdate; }
    const CoinPrice& get_main_pair() const { return _main_pair; }
    const WatchlistMap& get_watchlist_pairs() const { return _watchlist_pairs; }
    // Returns watchlist sorted by price descending (populated by sort_watchlist_by_price()).
    const SortedWatchlist& get_sorted_watchlist() const { return _sortedWatchlist; }
    // Returns the flat PSRAM buffer of '\n'-separated pair symbols, or nullptr if unavailable.
    const char* get_pairs_buffer() const { return _pairsBuf; }
    uint16_t    get_pairs_count()  const { return _pairsCount; }

    bool get_coin_ticker_24hr(const String &symbol, CoinPrice &out);

    // Fetch all mainstream USDT trading pairs from Binance, log them, and populate the pairs buffer.
    // Uses HTTP streaming to avoid loading the full ~100 KB response into RAM.
    bool fetch_available_usdt_pairs();

    // Fetch <coin_symbol>USDT ticker, update main_pair and lastUpdate. Returns true on success.
    bool refresh_main_pair(const String &coin_symbol);

    // Parse a comma-separated coin_watchlist string (e.g. "BTC,ETH,BNB"),
    // fetch 24 hr ticker for each <symbol>USDT pair, and refresh watchlist_pairs.
    void refresh_watchlist(const String &coin_watchlist);

    // Copy watchlist into _sortedWatchlist and sort by price descending.
    void sort_watchlist_by_price();
};

// void market_thread_entry(void *args);
#endif

