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


// ── Aphorism: fetch motivational quotes from zenquotes.io (screensaver content)
//    Mirrors legacy aphorism_thread_entry; board_sal_t* -> AphorismCtx* (DI).
//    Producer-only: appends keyword-matched quotes into a bounded pool; the UI
//    screensaver pops from the front. Logic/timing/keywords preserved verbatim.
void aphorism_thread_entry(void* args) {
    AphorismCtx* ctx = static_cast<AphorismCtx*>(args);
    AphorismState& aphorism = *ctx->state;
    auto& pool = aphorism.pool;

    wait_for_wifi_sta_connected(ctx->init_evt, "(aphorism)");

    static const char* const KEYWORDS[] = {
        "keep", "continue", "never", "quit", "grind", "work", "effort", "dedication",
        "commitment", "resilience", "fortitude", "tenacity", "grit", "strength",
        "courage", "discipline", "focus", "purpose",
        "time", "moment", "soon", "next", "tomorrow", "chance", "opportunity",
        "happen", "closer", "near", "almost", "sudden", "overnight", "surprise",
        "luck", "lucky", "fortune", "miracle", "destiny", "fate", "blessing",
        "unexpected", "breakthrough",
        "hope", "faith", "believe", "trust", "wish", "dream", "vision", "goal",
        "reward", "win", "success", "achieve", "triumph", "glory", "victory",
        "celebrate", "worthy", "prize",
        "bitcoin", "btc", "crypto", "cryptocurrency", "blockchain", "satoshi",
        "halving", "hash", "block", "miner", "hashrate", "nonce", "coinbase",
        "hodl", "hodler", "solo", "difficulty", "proof", "decentralized",
        "freedom", "scarce", "digital", "gold", "immutable", "trustless",
        "accumulate", "stack"
    };
    static const uint8_t  KEYWORD_COUNT     = sizeof(KEYWORDS) / sizeof(KEYWORDS[0]);
    static const uint16_t MAX_CHARS         = 80;
    static const uint8_t  POOL_MAX          = 30;
    static const uint8_t  POOL_LOW_THR      = (uint8_t)(POOL_MAX * 0.3f);
    static const uint32_t POOL_POLL_MS      = 1000 * 60;
    static const uint8_t  BATCHES           = 2;
    static const uint32_t BATCH_DELAY_MS    = 1000 * 30;
    static const uint32_t STARTUP_JITTER_MS = 1000UL * 60 * 5;

    uint32_t init_jitter_ms = esp_random() % STARTUP_JITTER_MS;
    LOG_W("[Aphorism] Startup jitter: ~%lu s (desync LAN devices)", init_jitter_ms / 1000);
    delay(init_jitter_ms);

    LOG_W("[Aphorism] Config => MAX_CHARS:%d  POOL_MAX:%d  POOL_LOW_THR:%d  BATCHES:%d  KEYWORDS:%d",
          (int)MAX_CHARS, (int)POOL_MAX, (int)POOL_LOW_THR, (int)BATCHES, (int)KEYWORD_COUNT);

    uint32_t batch_no = 0;
    while (true) {
        delay(10);
        if (ctx->wifi_status && *ctx->wifi_status != WL_CONNECTED) continue;
        if (ctx->ota_running && *ctx->ota_running)                 continue;

        xSemaphoreTake(aphorism.mutex, portMAX_DELAY);
        size_t cur_pool_size = pool.size();
        xSemaphoreGive(aphorism.mutex);

        if (cur_pool_size >= POOL_LOW_THR) {
            delay(POOL_POLL_MS);
            continue;
        }

        batch_no++;
        LOG_I("[Aphorism] Round %lu: pool low (%d quotes), fetching...", batch_no, (int)cur_pool_size);

        std::set<String> seen;
        xSemaphoreTake(aphorism.mutex, portMAX_DELAY);
        for (auto& qt : pool) seen.insert(qt.quote);
        xSemaphoreGive(aphorism.mutex);

        for (uint8_t b = 0; b < BATCHES; b++) {
            {
            HTTPClient http;
            WiFiClientSecure client;
            client.setInsecure();
            http.begin(client, "https://zenquotes.io/api/quotes");
            http.setTimeout(30000);
            http.setConnectTimeout(15000);
            http.addHeader("Connection", "close");

            int code = http.GET();
            if (code == HTTP_CODE_OK) {
                String body = http.getString();
                http.end();

                if (!body.startsWith("[")) {
                    LOG_E("[Aphorism] Response is not a JSON array, skipping. Content: %.100s", body.c_str());
                    delay(BATCH_DELAY_MS);
                    continue;
                }

                BasicJsonDocument<PsramJsonAllocator> doc(1024 * 64);
                DeserializationError err = deserializeJson(doc, body);
                if (!err) {
                    int cnt_total = 0, cnt_too_long = 0, cnt_no_kw = 0, cnt_hit = 0;
                    for (JsonObject item : doc.as<JsonArray>()) {
                        String q = item["q"].as<String>();
                        String a = item["a"].as<String>();
                        if (q.isEmpty() || seen.count(q)) continue;
                        seen.insert(q);
                        cnt_total++;
                        if ((uint16_t)q.length() > MAX_CHARS) { cnt_too_long++; continue; }
                        String ql = q;
                        ql.toLowerCase();
                        bool matched = false;
                        String matched_kw;
                        for (uint8_t k = 0; k < KEYWORD_COUNT && !matched; k++) {
                            if (ql.indexOf(KEYWORDS[k]) >= 0) { matched = true; matched_kw = KEYWORDS[k]; }
                        }
                        if (matched) {
                            xSemaphoreTake(aphorism.mutex, portMAX_DELAY);
                            if (pool.size() < POOL_MAX) {
                                pool.push_back({q, a, matched_kw});
                                cnt_hit++;
                            }
                            xSemaphoreGive(aphorism.mutex);
                        } else {
                            cnt_no_kw++;
                        }
                    }
                    LOG_I("[Aphorism] Batch %d: total=%d too_long=%d no_kw=%d hit=%d",
                          (int)b + 1, cnt_total, cnt_too_long, cnt_no_kw, cnt_hit);
                } else {
                    LOG_E("[Aphorism] JSON parse error: %s | body: %.100s", err.c_str(), body.c_str());
                }
            } else {
                String body = http.getString();
                http.end();
                LOG_E("[Aphorism] HTTP error: %d | body: %.100s", code, body.c_str());
            }
            } // HTTP scope freed

            if (b < BATCHES - 1) delay(BATCH_DELAY_MS);
        }

        xSemaphoreTake(aphorism.mutex, portMAX_DELAY);
        int pool_snap_size = (int)pool.size();
        xSemaphoreGive(aphorism.mutex);
        LOG_I("[Aphorism] Round %lu complete, pool now has %d quotes", batch_no, pool_snap_size);

        static const uint32_t ROUND_JITTER_MIN_MS = 1000UL * 60 * 2;
        uint32_t round_jitter_ms = ROUND_JITTER_MIN_MS + esp_random() % (STARTUP_JITTER_MS - ROUND_JITTER_MIN_MS + 1);
        LOG_I("[Aphorism] Next check in ~%lu s", round_jitter_ms / 1000);
        delay(round_jitter_ms);
    }
}
