#include "thread_entry.h"
#include "../app/application.h"
#include "../app/system_events.h"
#include "../utils/logger/logger.h"
#include "../utils/reboot_log/reboot_log.h"
#include "../board/board.h"
#include "../mining/mining_types.h"
#include "../drivers/power/power_hal.h"
#include "../drivers/power/power_ctx.h"
#include "../drivers/fan/fan.h"
#include "../drivers/fan/fan_ctx.h"
#include "../drivers/temp/temp_hal.h"
#include "../drivers/temp/temp_ctx.h"
#include "../drivers/temp/tmp102.h"
#include "../net/wifi_ctx.h"
#include "../market/market_ctx.h"
#include "../net/swarm_ctx.h"
#include "../net/mdns_discovery.h"
#include "../app/daemon_ctx.h"
#include "../app/monitor_ctx.h"
#include "../app/button_ctx.h"
#include "../app/led_ctx.h"
#include "../app/benchmark_ctx.h"
#include "../app/aphorism_ctx.h"
#include "../web/web_ctx.h"
#include "../web/http_server.h"
#include "../ui/ui_manager.h"
#include "../web/recovery_page.h"
#include "../nvs/nvs_config.h"
#include <nvs.h>
#include <OneButton.h>
#include <Adafruit_NeoPixel.h>
#include "../utils/helper.h"
#include "../utils/sha/csha256.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ESPmDNS.h>
#include <set>
#include <sys/time.h>
#include "lwip/sockets.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip4.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include <limits>
#include <unordered_set>
#include <vector>

#include "thread_shared.h"
using namespace thread_shared;


// ── Market: fetch crypto price & watchlist from Binance ─────────────────────
//    Mirrors legacy market_thread_entry; board_sal_t* -> MarketCtx* (DI).
void market_thread_entry(void* args) {
    MarketCtx* ctx = static_cast<MarketCtx*>(args);
    MarketClass* market = ctx->market;

    // Wait until the MarketClass instance has been allocated
    while (market == NULL) {
        LOG_W("MarketClass instance is NULL, waiting...");
        delay(1000);
    }

    wait_for_wifi_sta_connected(ctx->init_evt, "(market)");

    // Wait for WiFi, then print all available USDT trading pairs once at startup.
    LOG_I("Fetching available USDT trading pairs from Binance...");
    market->fetch_available_usdt_pairs();
    delay(1);

    const uint8_t  MARKET_MAX_RETRIES    = 3;
    const uint32_t MARKET_RETRY_DELAY_MS = 1000 * 10;

    while (true) {
        delay(1000);
        // Skip market update if OTA is running to avoid instability during updates.
        if (*ctx->ota_running) {
            LOG_D("Market update skipped: OTA in progress.");
            continue;
        }
        // Skip market update during screensaver to reduce network activity.
        if (xEventGroupGetBits(ctx->sys_evt) & SYS_EVENT_SCREEN_SAVER_TRIGGERED) {
            LOG_D("Market update skipped: screensaver active.");
            continue;
        }

        if (*ctx->wifi_status == WL_CONNECTED) {
            // Re-read the coin settings from NVS every cycle instead of using the
            // values copied into MarketCtx at boot. That copy is a snapshot, so the
            // web UI could change the main coin / watchlist and the thread would keep
            // polling the OLD symbol forever, while the UI showed the NEW symbol next
            // to the OLD coin's price.
            //
            // Same pattern as the pool / timezone hot-reload paths: the owning thread
            // re-reads NVS rather than sharing a mutable String with the web thread
            // (a String assignment on one task can reallocate while the other holds
            // c_str(), which is a use-after-free). Read-only NVS access here, no
            // growth of flash wear: NVS reads do not write.
            String coin_price     = nvs_config_get_string_value(NVS_CONFIG_PRICE_DISPLAY_COIN, "BTC");
            String coin_watchlist = nvs_config_get_string_value(NVS_CONFIG_COIN_WATCHLIST,
                                                                "BTC,ETH,LTC,BNB,DOGE,XRP,TRX,SOL");
            coin_price.toUpperCase();

            bool fetched = false;
            for (uint8_t attempt = 1; attempt <= MARKET_MAX_RETRIES; attempt++) {
                if (market->refresh_main_pair(coin_price)) {
                    fetched = true;
                    break;
                }
                LOG_D("Market data fetch failed (attempt %d/%d) for symbol [%sUSDT]",
                      attempt, MARKET_MAX_RETRIES, coin_price.c_str());
                if (attempt < MARKET_MAX_RETRIES) {
                    delay(MARKET_RETRY_DELAY_MS);
                }
            }
            if (!fetched) {
                LOG_E("Market data fetch failed after %d attempts. Please verify that the Binance API is accessible in your country.", MARKET_MAX_RETRIES);
            }

            delay(1);
            // Fetch watchlist pairs; refresh_watchlist() publishes them already
            // sorted by price descending for display.
            market->refresh_watchlist(coin_watchlist);
            delay(1);
        } else {
            LOG_D("Market update skipped: WiFi not connected.");
        }

        // Interruptible sleep: wake immediately when NVS coin settings change.
        for (uint32_t _end = millis() + MINER_MARKET_UPDATE_INTERVAL; millis() < _end; ) {
            if (market->consume_refresh_request()) break;
            delay(100);
        }
    }
    delete market;
    ctx->market = nullptr;
    LOG_W("Market thread exit.");
    vTaskDelete(NULL);
}
