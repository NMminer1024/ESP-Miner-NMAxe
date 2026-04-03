#include "market.h"
#include "utils/logger/logger.h"
#include <ArduinoJson.h>
#include "global.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <string.h>
#include <algorithm>

// https://developers.binance.com/docs/zh-CN/binance-spot-api-docs/rest-api/market-data-endpoints
bool MarketClass::fetch_available_usdt_pairs() {
    // GET /api/v3/ticker/price returns a JSON array of ALL symbols (~100 KB).
    // We stream-parse it character by character so we never need to buffer the
    // full response — only a tiny symbol-name buffer is kept on the stack.
    HTTPClient http;
    WiFiClient client;

    String url = "http://" MARKET_HOST ":" MARKET_PORT "/api/v3/ticker/price";
    http.begin(client, url);
    http.setTimeout(15000);                      // larger timeout: big response
    http.setConnectTimeout(MARKET_HTTP_TIMEOUT_MS);
    http.addHeader("Connection", "close");

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        LOG_E("Failed to fetch ticker list. HTTP code: %d, error: %s",
              httpCode, http.errorToString(httpCode).c_str());
        http.end();
        return false;
    }

    this->_pairsBufLen = 0;
    this->_pairsCount  = 0;
    if (this->_pairsBuf) this->_pairsBuf[0] = '\0';

    WiFiClient *stream   = http.getStreamPtr();
    int32_t    remaining = http.getSize();       // -1 if chunked/unknown

    // State machine: search for the literal pattern  "symbol":"  then capture
    // the symbol name until the closing quote.
    static const char PATTERN[]  = "\"symbol\":\"";
    static const uint8_t PAT_LEN = sizeof(PATTERN) - 1;  // exclude null terminator
    uint8_t  match_pos  = 0;
    bool     capturing  = false;
    char     sym_buf[20];
    uint8_t  sym_pos    = 0;
    uint16_t found      = 0;

    uint8_t chunk[128];
    while ((remaining != 0) && (stream->connected() || stream->available())) {
        int avail = stream->available();
        if (avail == 0) { delay(1); continue; }

        int to_read = (avail < (int)sizeof(chunk)) ? avail : (int)sizeof(chunk);
        int n = stream->readBytes(chunk, to_read);
        if (remaining > 0) remaining -= n;

        for (int i = 0; i < n; i++) {
            char c = (char)chunk[i];

            if (!capturing) {
                // Advance pattern match; restart on mismatch.
                if (c == PATTERN[match_pos]) {
                    match_pos++;
                    if (match_pos == PAT_LEN) {
                        match_pos = 0;
                        capturing = true;
                        sym_pos   = 0;
                        memset(sym_buf, 0, sizeof(sym_buf));
                    }
                } else {
                    match_pos = (c == PATTERN[0]) ? 1 : 0;
                }
            } else {
                if (c == '"') {
                    capturing = false;
                    uint8_t len = (uint8_t)strlen(sym_buf);
                    // Only store USDT pairs.
                    if (len < 5 || strcmp(sym_buf + len - 4, "USDT") != 0) continue;
                    uint8_t slen = (uint8_t)strlen(sym_buf);
                    if (this->_pairsBuf && (this->_pairsBufLen + slen + 2 < MARKET_PAIRS_BUF_CAP)) {
                        memcpy(this->_pairsBuf + this->_pairsBufLen, sym_buf, slen);
                        this->_pairsBufLen += slen;
                        this->_pairsBuf[this->_pairsBufLen++] = '\n';
                        this->_pairsBuf[this->_pairsBufLen]   = '\0';
                        this->_pairsCount++;
                    }
                    found++;
                    LOG_D("  [%3d] %s", found, sym_buf);
                } else if (sym_pos < (uint8_t)(sizeof(sym_buf) - 1)) {
                    sym_buf[sym_pos++] = c;
                }
            }
        }
    }

    http.end();
    LOG_I("Available USDT pairs on Binance: %d", found);
    return true;
}

bool MarketClass::get_coin_ticker_24hr(const String &symbol, CoinPrice &out) {
    // Use ESP32 built-in HTTPClient: has explicit timeout, auto connection cleanup,
    // and does not block indefinitely on a slow server like ArduinoHttpClient can.
    HTTPClient http;
    WiFiClient client;

    String url = "http://" MARKET_HOST ":" MARKET_PORT "/api/v3/ticker/24hr?symbol=" + symbol;
    http.begin(client, url);
    http.setTimeout(MARKET_HTTP_TIMEOUT_MS);     // connection + read timeout
    http.setConnectTimeout(MARKET_HTTP_TIMEOUT_MS);
    http.addHeader("Connection", "close");        // don't keep-alive; free sockets promptly

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        BasicJsonDocument<PsramJsonAllocator> doc(800);
        DeserializationError error = deserializeJson(doc, http.getStream());
        http.end();
        if (!error) {
            out.price      = doc["lastPrice"].as<String>().toFloat();
            out.change_pct = doc["priceChangePercent"].as<String>().toFloat();
            LOG_D("24hr ticker for %s: Price = %.2f, Change Percent = %.2f%%",
                  symbol.c_str(), out.price, out.change_pct);
            return true;
        } else {
            LOG_E("Failed to parse ticker JSON: %s", error.c_str());
        }
    } else {
        LOG_E("Failed to get 24hr ticker data. HTTP code: %d, error: %s",
              httpCode, http.errorToString(httpCode).c_str());
        http.end();
    }
    return false;
}

bool MarketClass::refresh_main_pair(const String &coin_symbol) {
    CoinPrice mp;
    if (this->get_coin_ticker_24hr(coin_symbol + "USDT", mp)) {
        this->_main_pair  = mp;
        this->_lastUpdate = millis();
        LOG_D("[Market] %sUSDT  price=%.4f  change=%.2f%%",
              coin_symbol.c_str(), mp.price, mp.change_pct);
        return true;
    }
    return false;
}

void MarketClass::refresh_watchlist(const String &coin_watchlist) {
    if (coin_watchlist.length() == 0) return;

    // Parse comma-separated list into full symbol names
    std::vector<String> symbols;
    int start = 0;
    while (true) {
        int    comma = coin_watchlist.indexOf(',', start);
        String sym   = (comma < 0) ? coin_watchlist.substring(start)
                                   : coin_watchlist.substring(start, comma);
        sym.trim();
        if (sym.length() > 0) {
            symbols.push_back(sym + "USDT");
        }
        if (comma < 0) break;
        start = comma + 1;
    }
    if (symbols.empty()) return;

    // Build URL-encoded symbols array: ["BTCUSDT","ETHUSDT"] → %5B%22BTCUSDT%22%2C%22ETHUSDT%22%5D
    String params = "%5B";
    for (size_t i = 0; i < symbols.size(); i++) {
        if (i > 0) params += "%2C";
        params += "%22" + symbols[i] + "%22";
    }
    params += "%5D";

    // Single batch request for all watchlist symbols
    String url = "http://" MARKET_HOST ":" MARKET_PORT "/api/v3/ticker/24hr?symbols=" + params;
    HTTPClient http;
    WiFiClient client;
    http.begin(client, url);
    http.setTimeout(MARKET_HTTP_TIMEOUT_MS);
    http.setConnectTimeout(MARKET_HTTP_TIMEOUT_MS);
    http.addHeader("Connection", "close");

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        LOG_E("[Watchlist] Batch fetch failed. HTTP code: %d, error: %s",
              httpCode, http.errorToString(httpCode).c_str());
        http.end();
        return;
    }

    // Filter: only keep symbol / lastPrice / priceChangePercent to save RAM
    StaticJsonDocument<128> filter;
    filter[0]["symbol"]             = true;
    filter[0]["lastPrice"]          = true;
    filter[0]["priceChangePercent"] = true;

    // PSRAM-backed JsonDocument sized for the filtered result (~150 bytes per entry)
    BasicJsonDocument<PsramJsonAllocator> doc(150 * symbols.size() + 256);
    DeserializationError error = deserializeJson(doc, http.getStream(),
                                                 DeserializationOption::Filter(filter));
    http.end();

    if (error) {
        LOG_E("[Watchlist] Failed to parse batch JSON: %s", error.c_str());
        return;
    }

    this->_watchlist_pairs.clear();
    for (JsonObject item : doc.as<JsonArray>()) {
        String sym = item["symbol"].as<String>();
        if (sym.isEmpty()) continue;
        CoinPrice cp;
        cp.price      = item["lastPrice"].as<String>().toFloat();
        cp.change_pct = item["priceChangePercent"].as<String>().toFloat();
        this->_watchlist_pairs[sym] = cp;
        LOG_D("[Watchlist] %s  price=%.4f  change=%.2f%%", sym.c_str(), cp.price, cp.change_pct);
    }
}

void MarketClass::sort_watchlist_by_price() {
    _sortedWatchlist.clear();
    _sortedWatchlist.reserve(_watchlist_pairs.size());
    for (const auto& kv : _watchlist_pairs) {
        _sortedWatchlist.push_back(kv);
    }
    std::sort(_sortedWatchlist.begin(), _sortedWatchlist.end(),
              [](const std::pair<String, CoinPrice>& a,
                 const std::pair<String, CoinPrice>& b) {
                  return a.second.price > b.second.price;
              });
    LOG_D("[Market] Watchlist sorted by price desc, %d entries", (int)_sortedWatchlist.size());
}
