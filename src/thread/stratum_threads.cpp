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

//    Mirrors legacy stratum_thread_entry; board_sal_t* -> MinerCtx*.
void stratum_thread_entry(void* args) {
    MinerCtx* ctx = static_cast<MinerCtx*>(args);
    StratumClass* stratum = ctx->stratum;
    MinerStatus&  st      = *ctx->status;
    ConnInfo&     conn    = *ctx->conn;

    BasicJsonDocument<PsramJsonAllocator> json(1024 * 4);
    bool is_primary_pool = true;

    double pool_init_diff = ctx->spec->asic.diff_thr_init;
    stratum->set_pool_difficulty(pool_init_diff);
    wait_for_wifi_sta_connected(ctx->init_evt, "(stratum)");

    // Wait for ASIC chip enumeration to complete before connecting to pool.
    LOG_I("(stratum) waiting for ASIC counted gate...");
    xEventGroupWaitBits(ctx->init_evt, INIT_EVENT_ASIC_COUNTED, pdFALSE, pdTRUE, portMAX_DELAY);
    LOG_I("(stratum) ASIC counted gate opened.");

    // Parse a Stratum JSON-RPC error response into a human-readable string.
    auto parse_stratum_error = [](const String& raw) -> String {
        StaticJsonDocument<256> err_doc;
        if (deserializeJson(err_doc, raw) != DeserializationError::Ok) return "Unknown error";
        if (!err_doc.containsKey("error") || err_doc["error"].isNull())  return "Unknown error";
        int code = err_doc["error"][0] | 0;
        switch (code) {
            case 20: return "Other/Unknown";
            case 21: return "Stale share";
            case 22: return "Duplicate share";
            case 23: return "Low difficulty";
            case 24: return "Unauthorized worker";
            case 25: return "Not subscribed";
            default: return String("Error code ") + code;
        }
    };

    while (true) {
        static int w_retry = 0, w_maxRetries = 24;
        if (*ctx->wifi_status != WL_CONNECTED) {
            w_retry++;
            LOG_W("WiFi reconnecting %d/%d...", w_retry, w_maxRetries);
            if (w_retry >= w_maxRetries) {
                reboot_intent_set(REBOOT_INTENT_WIFI_RECONNECT_FAIL,
                                  "stratum loop: %d/%d retries exhausted", w_retry, w_maxRetries);
                ESP.restart();
            }
            xSemaphoreGive(ctx->wifi_reconnect_xsem);
            stratum->reset();
            stratum->set_pool_difficulty(pool_init_diff);
            delay(5000);
            continue;
        } else w_retry = 0;

        // check pool status
        if ((conn.pool.use == conn.pool.primary) && (stratum->pool->is_connected())) {
            is_primary_pool = true;
        } else if ((conn.pool.use == conn.pool.fallback) && (stratum->pool->is_connected())) {
            is_primary_pool = false;
        }

        static uint32_t last = millis();
        static uint16_t random_seconds = 60 + esp_random() % (9 * 60 + 1); // 1~10 min jitter
        if ((millis() - last > 1000UL * random_seconds) && !is_primary_pool) {
            bool res = stratum->is_primary_pool_available(conn.pool.primary.url, conn.pool.primary.port);
            if (res) {
                LOG_I("Primary pool [%s] available now, switching to primary pool...", conn.pool.primary.url.c_str());
                conn.pool.use    = conn.pool.primary;
                conn.stratum.use = conn.stratum.primary;
                stratum->reset(conn.pool.use, conn.stratum.use);
                stratum->set_pool_difficulty(pool_init_diff);
                stratum->pool->begin(conn.pool.use.ssl);
                stratum->pool->connect();
                st.diff.last = 0;
            } else {
                LOG_W("Primary pool [%s] is not available, keep using fallback pool [%s]...",
                      conn.pool.primary.url.c_str(), conn.pool.fallback.url.c_str());
            }
            last = millis();
        }

        static uint16_t p_retry = 0, p_maxRetries = 5;
        if (!stratum->pool->is_connected()) {
            static bool first_connect = true;
            if (first_connect) { LOG_I("Pool connecting..."); first_connect = false; }
            else LOG_W("Lost connection to pool, reconnecting %d/%d...", p_retry, p_maxRetries);

            if (++p_retry % p_maxRetries == 0) {
                if (is_primary_pool) {
                    conn.pool.use    = conn.pool.fallback;
                    conn.stratum.use = conn.stratum.fallback;
                    LOG_W(">>>> Set pool to fallback [%s:%d] <<<<", conn.pool.use.url.c_str(), conn.pool.use.port);
                } else {
                    conn.pool.use    = conn.pool.primary;
                    conn.stratum.use = conn.stratum.primary;
                    LOG_W(">>>> Set pool to primary [%s:%d] <<<<", conn.pool.use.url.c_str(), conn.pool.use.port);
                }
            }
            stratum->reset(conn.pool.use, conn.stratum.use);
            stratum->set_pool_difficulty(pool_init_diff);
            stratum->pool->begin(conn.pool.use.ssl);
            stratum->pool->connect();
            st.diff.last = 0;
            delay(5000);
            continue;
        } else p_retry = 0;

        if (!stratum->is_subscribed()) {
            if (!stratum->subscribe()) {
                LOG_W("Failed to subscribe to pool, retrying in 5 seconds...");
                delay(100);
                continue;
            }
            if (!stratum->authorize()) {
                LOG_W("Failed to authorize to pool, retrying in 5 seconds...");
                delay(100);
                continue;
            }
            if (!stratum->config_version_rolling()) {
                LOG_W("Failed to config version rolling, retrying in 5 seconds...");
                delay(100);
                continue;
            }
            if (!stratum->suggest_difficulty()) {
                LOG_W("Failed to suggest difficulty to pool, retrying in 5 seconds...");
                delay(100);
                continue;
            }
        }

        if (!stratum->hello_pool(HELLO_POOL_INTERVAL_MS, POOL_INACTIVITY_TIME_MS)) {
            LOG_W("Pool is inactive, retrying in 5 seconds...");
            delay(5000);
            continue;
        }

        while (stratum->pool->available()) {
            stratum_method_data method = stratum->listen_methods();
            switch (method.type) {
                case STRATUM_DOWN_PARSE_ERROR:
                    if (method.raw != "") LOG_E("Stratum parse error, id : %d, raw : %s", method.id, method.raw.c_str());
                    else                  LOG_E("Stratum parse error, id : %d", method.id);
                    break;
                case STRATUM_DOWN_NOTIFY: {
                    LOG_D("Stratum notify, id : %d => %s", method.id, method.raw.c_str());
                    pool_job_data_t job;
                    json.clear();
                    DeserializationError error = deserializeJson(json, method.raw);
                    if (error) { LOG_E("Failed to parse STRATUM_DOWN_NOTIFY json"); break; }

                    job.id       = String((const char*)json["params"][0]);
                    job.prevhash = String((const char*)json["params"][1]);
                    job.coinb1   = String((const char*)json["params"][2]);
                    job.coinb2   = String((const char*)json["params"][3]);
                    job.merkle_branch = json["params"][4];
                    job.version  = String((const char*)json["params"][5]);
                    job.nbits    = String((const char*)json["params"][6]);
                    job.ntime    = String((const char*)json["params"][7]);
                    job.clean_jobs = json["params"][8];

                    LOG_D("Job ID            : %s", job.id.c_str());
                    LOG_D("Version mask      : 0x%08x", stratum->get_version_mask());
                    LOG_D("Pool difficulty   : %s", formatNumber(stratum->get_pool_difficulty(), 5).c_str());

                    if (job.clean_jobs) {
                        stratum->clear_job_cache();
                        xSemaphoreGive(stratum->clear_job_xsem);
                    }
                    stratum->push_job_cache(job);
                    xSemaphoreGive(stratum->new_job_xsem); // asic tx thread
                    stratum->job_counter_inc();
                    st.stratum_update = millis();          // pool alive timestamp
                    break;
                }
                case STRATUM_DOWN_SET_DIFFICULTY: {
                    LOG_D("Stratum set difficulty, id : %d => %s", method.id, method.raw.c_str());
                    st.stratum_update = millis();
                    json.clear();
                    DeserializationError error = deserializeJson(json, method.raw);
                    if (error) { LOG_E("Failed to parse STRATUM_DOWN_SET_DIFFICULTY json"); break; }
                    if (json["method"] == "mining.set_difficulty") {
                        if (json["params"].size() > 0) {
                            stratum->set_pool_difficulty(json["params"][0]);
                            LOG_D("Pool difficulty set : %s", formatNumber(json["params"][0], 5).c_str());
                        } else LOG_W("Pool difficulty not found in params");
                    }
                    break;
                }
                case STRATUM_DOWN_SET_VERSION_MASK: {
                    LOG_D("Stratum set version mask , id : %d => %s", method.id, method.raw.c_str());
                    st.stratum_update = millis();
                    stratum->set_msg_rsp_map(method.id, true);
                    json.clear();
                    DeserializationError error = deserializeJson(json, method.raw);
                    if (error) { LOG_E("Failed to parse STRATUM_DOWN_SET_VERSION_MASK json"); break; }
                    if (json["method"] == "mining.set_version_mask") {
                        if (json["params"].size() > 0) {
                            stratum->set_version_mask(strtoul(json["params"][0].as<const char*>(), NULL, 16));
                            LOG_L("Version mask set to %s", json["params"][0].as<const char*>());
                        } else {
                            stratum->set_version_mask(0x00000000);
                            LOG_W("Version mask not found in params, disable version rolling");
                        }
                    } else {
                        stratum->set_version_mask(0x00000000);
                        LOG_W("Version rolling key not found in response, disable version rolling");
                    }
                    stratum->del_msg_rsp_map(method.id);
                    break;
                }
                case STRATUM_DOWN_SET_EXTRANONCE: {
                    LOG_L("Stratum set extranonce => %s", method.id, method.raw.c_str());
                    st.stratum_update = millis();
                    json.clear();
                    DeserializationError error = deserializeJson(json, method.raw);
                    if (error) { LOG_E("Failed to parse STRATUM_DOWN_SET_EXTRANONCE json"); break; }
                    stratum->set_sub_extranonce1(json["params"][0]);
                    stratum->set_sub_extranonce2_size(json["params"][1]);
                    break;
                }
                case STRATUM_DOWN_SUCCESS:
                    if (method.id != -1) {
                        stratum->set_msg_rsp_map(method.id, true);
                        stratum_rsp rsp = stratum->get_method_rsp_by_id(method.id);
                        if (rsp.method == "mining.submit") {
                            st.latency = millis() - rsp.stamp;
                            st.stratum_update = millis();
                            if (rsp.status == true) {
                                st.share_accepted++;
                                LOG_L("#%d share accepted, %ldms", st.share_accepted + st.share_rejected, st.latency);
                            } else {
                                st.share_rejected++;
                                String err_msg = parse_stratum_error(method.raw);
                                LOG_E("#%d share rejected, %ldms, %s", st.share_accepted + st.share_rejected, st.latency, err_msg.c_str());
                            }
                        } else if (rsp.method == "mining.configure") {
                            json.clear();
                            DeserializationError error = deserializeJson(json, method.raw);
                            if (error) { LOG_E("Failed to parse STRATUM_DOWN_SUCCESS json"); }
                            else {
                                if (json["result"]["version-rolling"] == true) {
                                    if (json["result"].containsKey("version-rolling.mask")) {
                                        stratum->set_version_mask(strtoul(json["result"]["version-rolling.mask"].as<const char*>(), NULL, 16));
                                        LOG_I("Version mask set to %s", json["result"]["version-rolling.mask"].as<const char*>());
                                    } else {
                                        stratum->set_version_mask(0x00000000);
                                        LOG_W("Version mask not found in response, disable version rolling");
                                    }
                                } else {
                                    stratum->set_version_mask(0x00000000);
                                    LOG_W("Version rolling not supported, disable version rolling");
                                }
                            }
                        } else if (rsp.method == "mining.authorize") {
                            DeserializationError error = deserializeJson(json, method.raw);
                            if (error) { LOG_E("Failed to parse STRATUM_DOWN_NOTIFY json"); }
                            else if (json.containsKey("result")) {
                                stratum->set_authorize(json["result"]);
                                LOG_W("Authorization %s ", json["result"] ? "success" : "failed");
                            }
                        } else {
                            LOG_D("Stratum success, id : %d => %s", method.id, method.raw.c_str());
                        }
                    }
                    break;
                case STRATUM_DOWN_ERROR:
                    st.stratum_update = millis();
                    if (method.id != -1) {
                        stratum->set_msg_rsp_map(method.id, true);
                        stratum_rsp rsp = stratum->get_method_rsp_by_id(method.id);
                        if (rsp.method == "mining.submit") {
                            st.latency = millis() - rsp.stamp;
                            st.share_rejected++;
                            String err_msg = parse_stratum_error(method.raw);
                            LOG_E("#%d share rejected, %ldms, %s", st.share_accepted + st.share_rejected, st.latency, err_msg.c_str());
                        } else if (rsp.method == "mining.authorize") {
                            stratum->set_authorize(false);
                            LOG_E("Authorization failed.");
                        } else {
                            LOG_E("Unknown error response, id : %d", method.id);
                        }
                    }
                    break;
                case STRATUM_DOWN_UNKNOWN:
                    LOG_E("Stratum unknown, id : %d", method.id);
                    break;
                default:
                    LOG_E("Stratum unknown, id : %d", method.id);
                    break;
            }
            delay(1);
        }
        // Drain shares enqueued by the ASIC RX thread. Only the stratum thread
        // may touch the pool TLS socket, so it owns the actual write + bookkeeping.
        stratum->flush_submit_queue();
        // responsive idle: poll available() every 1ms (up to 50ms) to minimize latency error
        for (uint8_t i = 0; i < 50 && !stratum->pool->available(); i++) { delay(1); }
    }
}
