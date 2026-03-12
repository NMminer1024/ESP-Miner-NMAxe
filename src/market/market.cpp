#include "market.h"
#include "utils/logger/logger.h"
#include <ArduinoJson.h>
#include "global.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <string.h>

// Mainstream coins whitelist — ~100 well-known tokens (base names, no USDT suffix).
// Covers: top-cap L1/L2, major DeFi, GameFi/NFT, Meme, Storage, AI, RWA, and
// exchange tokens.  Obscure / low-cap tokens are intentionally excluded.
static const char* const MAINSTREAM[] = {
    // ── Proof-of-Work coins ──────────────────────────────────────────────────
    // Bitcoin family
    "BTC",   "LTC",   "BCH",   "ETC",   "XEC",   "BTG",
    // Ethereum-derived PoW (pre-merge classic / forks)
    "ETH",   "ETC",
    // Privacy PoW
    "XMR",   "ZEC",   "ZEN",   "BEAM",  "GRIN",
    // ASIC-resistant / GPU mining
    "RVN",   "FLUX",  "ERG",
    // Merged-mining / hybrid
    "DOGE",  "DGB",   "SYS",   "CKB",
    // ASIC mining (alt algorithms)
    "KAS",   "DASH",  "DCR",   "SC",    "BTM",
    // ── Major L1 (PoS / other) ───────────────────────────────────────────────
    "BNB",   "SOL",   "XRP",   "ADA",   "AVAX",  "TRX",   "TON",   "DOT",
    "ATOM",  "NEAR",  "ALGO",  "FTM",   "ONE",   "EGLD",  "HBAR",  "VET",
    "XLM",   "ICP",   "XTZ",   "EOS",
    // ── Layer-2 / Scaling ────────────────────────────────────────────────────
    "MATIC", "ARB",   "OP",    "IMX",   "STRK",  "METIS", "MANTA",
    // ── DeFi Blue-chips ──────────────────────────────────────────────────────
    "AAVE",  "MKR",   "UNI",   "LINK",  "CRV",   "SNX",   "COMP",  "SUSHI",
    "1INCH", "BAL",   "YFI",   "DYDX",  "GMX",   "LDO",   "RPL",   "SSV",
    "PENDLE","EIGEN",
    // ── NFT / GameFi / Metaverse ─────────────────────────────────────────────
    "SAND",  "MANA",  "AXS",   "GALA",  "ENJ",   "CHZ",   "FLOW",  "THETA",
    "BLUR",  "LOOKS", "APE",   "IMX",
    // ── Meme coins ───────────────────────────────────────────────────────────
    "SHIB",  "PEPE",  "FLOKI", "BONK",  "WIF",   "BOME",
    // ── Storage / Infra / Oracle ─────────────────────────────────────────────
    "FIL",   "AR",    "GRT",   "ANKR",  "STORJ", "BAND",
    // ── AI / Data ────────────────────────────────────────────────────────────
    "WLD",   "FET",   "RENDER","TAO",   "AGIX",  "OCEAN",
    // ── Cross-chain / Interop ────────────────────────────────────────────────
    "RUNE",  "STX",   "ZRO",   "W",     "OMNI",
    // ── Newer high-cap L1 ────────────────────────────────────────────────────
    "APT",   "SUI",   "SEI",   "INJ",   "TIA",   "PYTH",  "JTO",   "JUP",
    // ── Exchange / CEX tokens ────────────────────────────────────────────────
    "OKB",   "CRO",   "GT",    "HT",    "KCS",
    // ── Staking / Liquid ─────────────────────────────────────────────────────
    "CBETH", "STETH", "WBETH",
};

// https://developers.binance.com/docs/zh-CN/binance-spot-api-docs/rest-api/market-data-endpoints
bool MarketClass::print_available_usdt_pairs() {
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

    static const uint8_t MAINSTREAM_COUNT = sizeof(MAINSTREAM) / sizeof(MAINSTREAM[0]);

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
                    // Only process USDT pairs.
                    if (len < 4 || strcmp(sym_buf + len - 4, "USDT") != 0) continue;

                    // Extract base coin and check against mainstream whitelist.
                    char base[16] = {0};
                    uint8_t base_len = len - 4;
                    if (base_len >= sizeof(base)) continue;
                    memcpy(base, sym_buf, base_len);

                    for (uint8_t j = 0; j < MAINSTREAM_COUNT; j++) {
                        if (strcmp(base, MAINSTREAM[j]) == 0) {
                            LOG_I("  [%2d] %s", ++found, sym_buf);
                            break;
                        }
                    }
                } else if (sym_pos < (uint8_t)(sizeof(sym_buf) - 1)) {
                    sym_buf[sym_pos++] = c;
                }
            }
        }
    }

    http.end();
    LOG_I("Available mainstream USDT pairs on Binance: %d", found);
    return true;
}

bool MarketClass::get_coin_ticker_24hr(const String &symbol) {
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
            this->price          = doc["lastPrice"].as<String>().toFloat();
            this->change_percent = doc["priceChangePercent"].as<String>().toFloat();
            LOG_D("24hr ticker for %s: Price = %.2f, Change Percent = %.2f%%",
                  symbol.c_str(), this->price, this->change_percent);
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
