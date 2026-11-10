#ifndef _MARKET_H_
#define _MARKET_H_
// Using ESP32 built-in HTTPClient instead of ArduinoHttpClient for better stability.
// HTTPClient has explicit timeout support and avoids indefinite blocking on slow servers.
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <map>
#include <utility>
#include <vector>
#include "utils/helper.h"

#define MARKET_HOST            "data-api.binance.vision"
#define MARKET_PORT            "80"
#define MARKET_HTTP_TIMEOUT_MS 4000   // 4 s — must be < TWDT timeout (5 s) to prevent watchdog abort
// PSRAM flat buffer capacity for all USDT pair names ('\n'-separated symbols).
// 500 pairs x avg ~9 chars each ~= 4.5 KB; 8 KB gives comfortable headroom.
#define MARKET_PAIRS_BUF_CAP   (8 * 1024)

struct CoinPrice {
    float price      = 0.0f;
    float change_pct = 0.0f;  // 24 h change percent
};

// Fixed-size row handed to readers (UI/web) so they never touch the guarded
// containers or their String allocations. Binance symbols are <= 12 chars.
struct WatchlistRow {
    char  symbol[16];
    float price;
    float change_pct;
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
    // Guards _watchlist_pairs + _sortedWatchlist: the (market) task rebuilds them
    // every refresh cycle while the LVGL task reads them at 1 Hz.
    SemaphoreHandle_t           _wl_mutex;
    WatchlistMap                _watchlist_pairs;  // key = full symbol e.g. "ETHUSDT"
    SortedWatchlist             _sortedWatchlist;  // price-descending sorted copy (PSRAM)
    // All USDT pairs stored as a flat PSRAM buffer: "BTCUSDT\nETHUSDT\n..."
    char*                       _pairsBuf;
    size_t                      _pairsBufLen;
    uint16_t                    _pairsCount;
    volatile bool               _needs_refresh;

    // Swap a fully built snapshot into the guarded state. The critical section holds
    // only two container swaps — no allocation, no I/O, no delay.
    void publish_watchlist(WatchlistMap&& fresh, SortedWatchlist&& sorted);

public:
    MarketClass() : _pairsBuf(nullptr), _pairsBufLen(0), _pairsCount(0),
                    _lastUpdate(0), _wl_mutex(nullptr), _needs_refresh(false) {
        _wl_mutex = xSemaphoreCreateMutex();
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
        if (_wl_mutex) { vSemaphoreDelete(_wl_mutex); _wl_mutex = nullptr; }
    }

    uint32_t get_last_update() const { return _lastUpdate; }
    const CoinPrice& get_main_pair() const { return _main_pair; }
    // Rows in the price-descending snapshot. 0 when the lock is busy, so a reader
    // keeps its previous frame instead of racing a rebuild.
    size_t watchlist_size() const;
    // Copy rows [offset, offset+count) of the snapshot into caller-owned `out` under
    // the lock; returns the number of rows written. Readers must not iterate
    // _sortedWatchlist directly — it is rebuilt by the (market) task every cycle.
    size_t copy_watchlist_window(size_t offset, size_t count, WatchlistRow* out) const;
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
    // fetch 24 hr ticker for each <symbol>USDT pair, then publish the result as the
    // new snapshot (sorted by price descending). All building happens outside the lock.
    void refresh_watchlist(const String &coin_watchlist);
};

// void market_thread_entry(void *args);
#endif

