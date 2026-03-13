#include "market.h"
#include "utils/logger/logger.h"
#include <ArduinoJson.h>
#include "global.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <string.h>

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

    this->_availablePairs.clear();

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
                    this->_availablePairs.push_back(String(sym_buf));
                    LOG_D("  [%3d] %s", ++found, sym_buf);
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
        String payload = http.getString();
        http.end();

        StaticJsonDocument<800> doc;
        DeserializationError error = deserializeJson(doc, payload);
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
        LOG_W("[Market] %sUSDT  price=%.4f  change=%.2f%%",
              coin_symbol.c_str(), mp.price, mp.change_pct);
        return true;
    }
    return false;
}

void MarketClass::refresh_watchlist(const String &coin_watchlist) {
    if (coin_watchlist.length() == 0) return;

    this->_watchlist_pairs.clear();
    int start = 0;
    while (true) {
        int    comma = coin_watchlist.indexOf(',', start);
        String sym   = (comma < 0) ? coin_watchlist.substring(start)
                                   : coin_watchlist.substring(start, comma);
        sym.trim();
        if (sym.length() > 0) {
            CoinPrice cp;
            if (this->get_coin_ticker_24hr(sym + "USDT", cp)) {
                this->_watchlist_pairs[sym + "USDT"] = cp;
                LOG_W("[Watchlist] %sUSDT  price=%.4f  change=%.2f%%",
                      sym.c_str(), cp.price, cp.change_pct);
            }
        }
        if (comma < 0) break;
        start = comma + 1;
    }
}
