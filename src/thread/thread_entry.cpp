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
#include "../app/daemon_ctx.h"
#include "../app/monitor_ctx.h"
#include "../app/button_ctx.h"
#include "../app/led_ctx.h"
#include "../app/benchmark_ctx.h"
#include "../app/aphorism_ctx.h"
#include "../web/web_ctx.h"
#include "../web/http_server.h"
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

// ── Stratum: pool connect / subscribe / job notify / share results ──────────
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
                            stratum->set_version_mask(0xffffffff);
                            LOG_W("Version mask not found in params");
                        }
                    } else {
                        stratum->set_version_mask(0xffffffff);
                        LOG_W("Version rolling key not found in response");
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
                                stratum->set_version_mask(0xffffffff);
                                if (json["result"]["version-rolling"] == true) {
                                    if (json["result"].containsKey("version-rolling.mask")) {
                                        stratum->set_version_mask(strtoul(json["result"]["version-rolling.mask"].as<const char*>(), NULL, 16));
                                        LOG_I("Version mask set to %s", json["result"]["version-rolling.mask"].as<const char*>());
                                    } else LOG_W("Version mask not found in response");
                                } else LOG_W("Version rolling not supported");
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
        // responsive idle: poll available() every 1ms (up to 50ms) to minimize latency error
        for (uint8_t i = 0; i < 50 && !stratum->pool->available(); i++) { delay(1); }
    }
}

// ── Miner count: wait for power, enumerate ASIC chips ───────────────────────
//    Mirrors legacy miner_asic_count_thread_entry; board_sal_t* -> MinerCtx*.
void miner_count_thread_entry(void* args) {
    MinerCtx* ctx = static_cast<MinerCtx*>(args);

    while (ctx->miner == nullptr) {
        LOG_W("Waiting for miner instance ready...");
        delay(1000);
    }

    // wait for vdd and vpll ready, avoid some asic chip not detected issue
    xEventGroupWaitBits(ctx->init_evt, INIT_EVENT_VDD_VPLL_READY, pdFALSE, pdTRUE, portMAX_DELAY);

    // wait for asic detected, avoid some usb-sata bridge not ready issue
    while (ctx->miner->connect_chip() == 0) {
        LOG_W("Waiting for asic chip detected...");
        delay(1000);
    }

    xEventGroupSetBits(ctx->init_evt, INIT_EVENT_ASIC_COUNTED);

    if (ctx->miner->get_asic_count() != ctx->spec->asic.num_req) {
        LOG_E("Detected ASIC count (%d/%d) does not match required ASIC count!!!!",
              ctx->miner->get_asic_count(), ctx->spec->asic.num_req);
    }

    vTaskDelete(NULL);
}

// ── Miner init: bring up ASIC hardware at the required frequency/diff ────────
//    Mirrors legacy miner_asic_init_thread_entry; board_sal_t* -> MinerCtx*.
void miner_init_thread_entry(void* args) {
    MinerCtx* ctx = static_cast<MinerCtx*>(args);

    while (ctx->miner == nullptr) {
        LOG_W("Waiting for miner instance ready...");
        delay(1000);
    }

    // Preserve the legacy two-stage bring-up:
    // 1. ASIC count runs first on VDD/VPLL only, so USB-only power can still detect chips.
    // 2. Full ASIC init waits until Vcore, fan self-test, and WiFi are all ready.
    xEventGroupWaitBits(ctx->init_evt,
                        INIT_EVENT_ASIC_COUNTED |
                        INIT_EVENT_VCORE_READY |
                        INIT_EVENT_FAN_READY |
                        INIT_EVENT_WIFI_STA_CONNECTED,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    if (!ctx->miner->begin(ctx->spec->asic.req_frq, ctx->spec->asic.diff_thr_init,
                           ctx->spec->asic.com_baud_work)) {
        while (true) {
            LOG_E("Miner ASIC init failed, retrying...");
            delay(1000);
        }
    }
    LOG_I("%s init completed, job interval set to %d ms",
          ctx->spec->asic.name.c_str(), ctx->spec->asic.job_interval_ms);
    vTaskDelete(NULL);
}

// ── Miner TX: pull jobs, drive ASIC, runtime pause/resume control ───────────
//    Mirrors legacy miner_asic_tx_thread_entry; board_sal_t* -> MinerCtx*.
void miner_tx_thread_entry(void* args) {
    MinerCtx* ctx = static_cast<MinerCtx*>(args);
    AsicMinerClass* miner   = ctx->miner;
    StratumClass*   stratum = ctx->stratum;
    AxePowerHal*    power   = ctx->power;
    MinerStatus&    st      = *ctx->status;
    const BoardSpecConfig& spec = *ctx->spec;

    auto calculate_diff = [](String nBits) -> double {
        static const uint8_t TARGET_BUFFER_SIZE = 64;
        uint8_t netdiff_array[TARGET_BUFFER_SIZE / 2];
        char str[TARGET_BUFFER_SIZE + 1];
        memset(str, '0', TARGET_BUFFER_SIZE);
        int k = (int)strtol(nBits.substring(0, 2).c_str(), 0, 16) - 3;
        uint8_t index = 58 - 2 * k;
        memcpy(str + index, nBits.substring(2).c_str(), nBits.length() - 2);
        str[TARGET_BUFFER_SIZE] = 0;
        str_to_byte_array(str, TARGET_BUFFER_SIZE / 2, netdiff_array);
        reverse_bytes(netdiff_array, TARGET_BUFFER_SIZE / 2);
        return le_hash_to_diff(netdiff_array);
    };

    auto apply_pending_frequency = [&]() -> bool {
        if (miner == nullptr) return false;
        if (!miner->apply_pending_asic_frequency()) return false;
        uint16_t current_freq = miner->get_asic_frequency_current();
        if (current_freq > 0) ctx->spec->asic.req_frq = current_freq;
        st.hashrate = {0.0, 0.0, 0.0};
        st.asic_update = millis();
        LOG_W("ASIC PLL hot-switch completed, freq now %uMHz", current_freq);
        return true;
    };

    auto refresh_mining_timeouts = [&]() {
        uint32_t now = millis();
        st.asic_update = now;
        st.stratum_update = now;
    };

    auto clear_mining_runtime_caches = [&]() {
        if (miner != nullptr) {
            miner->clear_asic_job_cache();
            miner->reset_hashrate();
            miner->end();
        }
        if (stratum != nullptr) {
            stratum->clear_job_cache();
            stratum->clear_sub_extranonce2();
            while (xSemaphoreTake(stratum->new_job_xsem, 0) == pdTRUE) {}
            while (xSemaphoreTake(stratum->clear_job_xsem, 0) == pdTRUE) {}
        }
        st.hashrate = {0.0, 0.0, 0.0};
        refresh_mining_timeouts();
        xSemaphoreGive(st.update_xsem);
    };

    auto apply_mining_control = [&]() -> bool {
        if (miner == nullptr || power == nullptr) return false;

        MinerRuntimeState state = st.runtime_state;
        if (state == MINER_RUNTIME_PAUSING) {
            LOG_W("Pausing mining: clearing ASIC work and powering off Vcore");
            st.user_paused = true;
            if (st.pause_started_ms == 0) st.pause_started_ms = millis();
            clear_mining_runtime_caches();
            power->set_vcore_status(PWR_OFF);
            st.runtime_state = MINER_RUNTIME_PAUSED;
            st.resume_grace_until_ms = 0;
            refresh_mining_timeouts();
            xSemaphoreGive(st.update_xsem);
            LOG_W("Mining paused, ASIC Vcore is off");
            return true;
        }

        if (state == MINER_RUNTIME_RESUMING) {
            LOG_W("Resuming mining: restoring Vcore and reinitializing ASIC");
            refresh_mining_timeouts();
            power->set_vcore_voltage(spec.asic.req_vcore);
            power->set_vcore_status(PWR_ON);

            bool vcore_ready = false;
            uint32_t start_ms = millis();
            while (millis() - start_ms < 10000) {
                if (power->is_vcore_ready()) { vcore_ready = true; break; }
                refresh_mining_timeouts();
                delay(100);
            }

            if (!vcore_ready) {
                LOG_E("Mining resume failed: Vcore not ready");
                power->set_vcore_status(PWR_OFF);
                st.runtime_state = MINER_RUNTIME_ERROR;
                st.user_paused = true;
                st.pause_started_ms = 0;
                refresh_mining_timeouts();
                xSemaphoreGive(st.update_xsem);
                return true;
            }

            miner->clear_asic_job_cache();
            miner->reset_hashrate();
            st.hashrate = {0.0, 0.0, 0.0};
            if (!miner->begin(spec.asic.req_frq, spec.asic.diff_thr_init, spec.asic.com_baud_work)) {
                LOG_E("Mining resume failed: ASIC reinitialization failed");
                power->set_vcore_status(PWR_OFF);
                st.runtime_state = MINER_RUNTIME_ERROR;
                st.user_paused = true;
                st.pause_started_ms = 0;
                refresh_mining_timeouts();
                xSemaphoreGive(st.update_xsem);
                return true;
            }

            refresh_mining_timeouts();
            st.resume_grace_until_ms = millis() + MINER_RESUME_GRACE_MS;
            st.user_paused = false;
            st.pause_started_ms = 0;
            st.runtime_state = MINER_RUNTIME_RUNNING;
            if (stratum != nullptr) xSemaphoreGive(stratum->new_job_xsem);
            xSemaphoreGive(st.update_xsem);
            LOG_W("Mining resumed, ASIC timeout counters refreshed");
            return true;
        }

        if (state == MINER_RUNTIME_PAUSED || state == MINER_RUNTIME_ERROR) {
            refresh_mining_timeouts();
            return true;
        }
        return false;
    };

    // forever loop
    while (true) {
        if (apply_mining_control()) {
            if (st.control_xsem != NULL) {
                xSemaphoreTake(st.control_xsem, pdMS_TO_TICKS(200));
            } else {
                delay(200);
            }
            continue;
        }

        apply_pending_frequency();

        // null loop if not subscribed
        if (!stratum->is_subscribed()) {
            miner->end();
            miner->reset_hashrate();
            st.hashrate = {0.0, 0.0, 0.0};
            delay(1000);
            continue;
        }
        // wait for new job signal 1000ms max
        if (xSemaphoreTake(stratum->new_job_xsem, 1000) != pdTRUE) {
            apply_pending_frequency();
            continue;
        }

        // get job from pool job caches
        {
            pool_job_data_t next_job = stratum->pop_job_cache();
            if (next_job.id != "") miner->pool_job_now = next_job;
        }
        if (miner->pool_job_now.id == "") continue;

        // calculate network diff + update pool diff
        st.diff.network = calculate_diff(miner->pool_job_now.nbits);
        st.diff.pool    = stratum->get_pool_difficulty();

        LOG_W("Job [%s] from %s:%d", miner->pool_job_now.id.c_str(),
              stratum->pool->get_pool_info().url.c_str(), stratum->pool->get_pool_info().port);
        while (true) {
            if (apply_mining_control()) break;
            apply_pending_frequency();

            // construct asic job and send to asic
            if (!miner->mining(&miner->pool_job_now)) {
                delay(10); // avoid tight spin loop tripping interrupt WDT when mining() fails
                continue;
            }
            // exit if pool disconnected
            if (!stratum->is_subscribed()) break;

            // set asic diff as pool diff if pool diff < initial asic diff
            double target_diff = min(stratum->get_pool_difficulty(), (double)spec.asic.diff_thr_init);
            static double last_diff = 0.0;
            if (target_diff != last_diff) {
                uint32_t diff = miner->set_asic_diff(target_diff);
                LOG_W("Change asic diff from [%.1f] to [%d/%.1f] successfully", last_diff, diff, target_diff);
                last_diff = target_diff;
            }

            // interval 'job_interval_ms' per asic job, exit if a new pool job arrived
            if (xSemaphoreTake(stratum->new_job_xsem, spec.asic.job_interval_ms) == pdTRUE) {
                // clear job cache if clean job signal received (avoid stale share submit)
                if (xSemaphoreTake(stratum->clear_job_xsem, 0) == pdTRUE) {
                    miner->clear_asic_job_cache();
                    stratum->clear_sub_extranonce2();
                    LOG_D("Stratum job cache clear...");
                }
                xSemaphoreGive(stratum->new_job_xsem); // release for next pool job
                break;
            }
        }
    }
}

// ── Miner RX: listen ASIC nonces, verify, dedup, submit shares ──────────────
//    Mirrors legacy miner_asic_rx_thread_entry; board_sal_t* -> MinerCtx*.
void miner_rx_thread_entry(void* args) {
    MinerCtx* ctx = static_cast<MinerCtx*>(args);
    AsicMinerClass* miner   = ctx->miner;
    StratumClass*   stratum = ctx->stratum;
    MinerStatus&    st      = *ctx->status;

    asic_job      job    = {0,};
    miner_result  result = {0,};

    auto calculate_diff = [](uint32_t version, uint8_t* prev_block_hash, uint8_t* merkle_root,
                             uint32_t ntime, uint32_t nbits, uint32_t nonce) -> double {
        uint8_t header[4 + 32 + 32 + 4 + 4 + 4] = {0,};
        uint8_t hash[32] = {0,};
        uint8_t prev_block_hash_t[32] = {0}, merkle_root_t[32] = {0};
        memcpy(prev_block_hash_t, prev_block_hash, 32);
        memcpy(merkle_root_t, merkle_root, 32);
        reverse_words(prev_block_hash_t, 32);
        reverse_words(merkle_root_t, 32);
        memcpy(header, (uint8_t*)&version, 4);
        memcpy(header + 4, prev_block_hash_t, 32);
        memcpy(header + 36, merkle_root_t, 32);
        memcpy(header + 68, (uint8_t*)&ntime, 4);
        memcpy(header + 72, (uint8_t*)&nbits, 4);
        memcpy(header + 76, (uint8_t*)&nonce, 4);
        csha256d(header, sizeof(header), hash);
        return le_hash_to_diff(hash);
    };

    const bool ota_running_default = false;
    while (true) {
        if (st.is_controlled_idle()) { delay(100); continue; }
        if (!stratum->is_subscribed()) { delay(1000); continue; }
        if (miner->is_asic_frequency_updating()) { delay(50); continue; }
        if (ctx->ota_running ? *ctx->ota_running : ota_running_default) { delay(50); continue; }

        esp_err_t err = miner->listen_asic_rsp(&result, 1000 * 30);
        if (miner->is_asic_frequency_updating()) continue;
        if (ESP_OK == err) {
            if (!stratum->is_subscribed()) continue;
            if (miner->find_job_by_asic_job_id(result.asic.job_id, &job)) {
                st.asic_update = millis();
                uint32_t version_bits = (reverse_uint16(result.asic.version) << 13); // logic from bitaxe
                uint32_t version      = version_bits | (*(uint32_t*)job.version);
                double diff = calculate_diff(version, job.prev_block_hash, job.merkle_root,
                                             *(uint32_t*)job.ntime, *(uint32_t*)job.nbits, result.asic.nonce);

                if ((diff <= std::numeric_limits<double>::epsilon()) || std::isnan(diff) || std::isinf(diff)) continue;
                if (diff < miner->get_asic_diff()) continue;

                // fetch job context first so dedup can run before hashrate is counted
                uint32_t version_submit = version ^ (*(uint32_t*)job.version);
                String   pool_id_submit = miner->get_pool_job_id_by_asic_job_id(result.asic.job_id);
                String   extra2_submit  = miner->get_extranonce2_by_asic_job_id(result.asic.job_id);
                if (pool_id_submit.length() == 0 || extra2_submit.length() == 0) continue; // slot evicted

                // Deduplicate nonces within the same (pool_job_id, extranonce2) scope.
                static std::unordered_set<uint32_t, std::hash<uint32_t>, std::equal_to<uint32_t>, PsramAllocator<uint32_t>> _submitted_nonces;
                static String _dedup_job_key = "";
                String _cur_job_key = pool_id_submit + extra2_submit;
                if (_cur_job_key != _dedup_job_key) {
                    _submitted_nonces.clear();
                    _dedup_job_key = _cur_job_key;
                }
                if (_submitted_nonces.count(result.asic.nonce)) {
                    LOG_W("Dup nonce 0x%08x skipped (pool_job=%s)", result.asic.nonce, pool_id_submit.c_str());
                    continue;
                }
                _submitted_nonces.insert(result.asic.nonce);

                // record nonce into hashrate ring; calculation driven by monitor thread
                miner->record_nonce();

                // per-asic share count
                st.asic_rsp_counter[result.asic_id]++;

                // throttled summary log
                static uint32_t last = millis();
                if (millis() - last >= MINER_LOG_SUMMARY_INTERVAL) {
                    LOG_L(" ============%s=========== ", ctx->fw_version.c_str());
                    LOG_L("|            Summary           |");
                    LOG_L("+------------Uptime------------+");
                    LOG_L("|%s | %s |", convert_uptime_to_string(st.uptime_session).c_str(), convert_uptime_to_string(st.uptime_ever).c_str());
                    LOG_L("+-----------HashRate-----------+");
                    LOG_L("|   3m    |    30m   |    1h   |");
                    LOG_L("|%-4sH/s| %-4sH/s|%-4sH/s|",
                          formatNumber(st.hashrate._3m, 4).c_str(),
                          formatNumber(st.hashrate._30m, 4).c_str(),
                          formatNumber(st.hashrate._1h, 4).c_str());
                    LOG_L("+----------Difficulty----------+");
                    LOG_L("|From boot| Best ever| Network |");
                    LOG_L("| %-6s |  %-5s | %-7s |",
                          formatNumber(st.diff.best_session, 5).c_str(),
                          formatNumber(st.diff.best_ever, 5).c_str(),
                          formatNumber(st.diff.network, 5).c_str());
                    LOG_L("+---Free heap-----Efficiency---+");
                    LOG_L("|    %-3sKB   |   %-3sJ/TH   |", formatNumber(ESP.getFreeHeap() / 1024.0f, 4).c_str(), formatNumber(st.efficiency, 4).c_str());
                    LOG_L(" ============================== ");
                    log_i("\r\n");
                    LOG_I("| ASIC | Last | Pool | Network |");
                    LOG_I("|------|------|------|---------|");
                    last = millis();
                }

                LOG_I("|%-6s|%-6s|%-6s|%-7s|",
                      formatNumber(miner->get_asic_diff(), 4).c_str(),
                      formatNumber(diff, 4).c_str(),
                      formatNumber(stratum->get_pool_difficulty(), 4).c_str(),
                      formatNumber(st.diff.network, 7).c_str());

                if (diff < stratum->get_pool_difficulty()) continue;

                bool res = miner->submit_job_share(pool_id_submit, extra2_submit, result.asic.nonce, *(uint32_t*)job.ntime, version_submit);
                if (!res) continue;

                // block hit?
                if (diff >= st.diff.network) {
                    st.hits = (st.hits >= 99) ? 0 : (st.hits);
                    st.hits++;

                    uint8_t header[4 + 32 + 32 + 4 + 4 + 4] = {0,};
                    uint8_t hash[32] = {0,};
                    uint8_t prev_block_hash_t[32] = {0}, merkle_root_t[32] = {0};
                    memcpy(prev_block_hash_t, job.prev_block_hash, 32);
                    memcpy(merkle_root_t, job.merkle_root, 32);
                    reverse_words(prev_block_hash_t, 32);
                    reverse_words(merkle_root_t, 32);
                    memcpy(header, (uint8_t*)&version, 4);
                    memcpy(header + 4, prev_block_hash_t, 32);
                    memcpy(header + 36, merkle_root_t, 32);
                    memcpy(header + 68, (uint8_t*)&job.ntime, 4);
                    memcpy(header + 72, (uint8_t*)&job.nbits, 4);
                    memcpy(header + 76, (uint8_t*)&result.asic.nonce, 4);
                    csha256d(header, sizeof(header), hash);

                    LOG_W("******************************* Your Are The Chosen One ********************************");
                    LOG_I("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!BLOCK FOUND!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
                    log_i("Nonce       : %08x", result.asic.nonce);
                    log_i("\r\nVersion     : %08x", version);
                    log_i("\r\nBlock header: ");
                    for (int i = 0; i < 40; i++) log_i("%02x", header[i]);
                    log_i("\r\n              ");
                    for (int i = 40; i < 80; i++) log_i("%02x", header[i]);
                    log_i("\r\nBlock hash  : ");
                    for (size_t i = 0; i < sizeof(hash); i++) log_i("%02x", hash[i]);
                    log_i("\r\n");
                    LOG_I("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\r\n");

                    xSemaphoreGive(ctx->nvs_save_xsem);
                    xEventGroupSetBits(ctx->sys_evt, SYS_EVENT_MINER_BLOCK_HIT);
                }

                // update diff stats
                st.diff.last         = diff;
                st.diff.best_session = (diff > st.diff.best_session) ? diff : st.diff.best_session;
                st.diff.best_ever    = (diff > st.diff.best_ever) ? diff : st.diff.best_ever;

                if (diff == st.diff.best_ever) {
                    xSemaphoreGive(ctx->nvs_save_xsem);
                    if (diff > 100.0f * 1000.0f * 1000.0f) { // > 100M
                        xEventGroupSetBits(ctx->sys_evt, SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED);
                    }
                }

                // add to block-proximity history
                if (xSemaphoreTake(st.proximity_history.mutex, portMAX_DELAY) == pdTRUE) {
                    proximity_node_t node;
                    node.block_proximity = diff / st.diff.network;
                    node.share_diff      = diff;
                    node.net_diff        = st.diff.network;
                    node.epoch           = (ctx->utc ? *ctx->utc : 0ULL) * 1000ULL;
                    add_share_diff_history(st.proximity_history.deque, node, 36);
                    xSemaphoreGive(st.proximity_history.mutex);
                }
            }
        } else if (ESP_ERR_INVALID_SIZE == err) {
            LOG_W("Asic response size error.");
        } else if (ESP_ERR_TIMEOUT == err) {
            LOG_W("Asic response timeout.");
        } else if (ESP_ERR_INVALID_RESPONSE == err) {
            LOG_W("Asic response header error.");
        } else {
            LOG_W("Asic response error: %s", esp_err_to_name(err));
        }
    }
}

// ── WiFi config monitor: reboots if no client connects within the window ────
//    Mirrors legacy config_monitor_thread_entry; board_sal_t* -> WifiCtx*.
void config_monitor_thread_entry(void* args) {
    WifiCtx* ctx = static_cast<WifiCtx*>(args);
    WifiState& st = *ctx->state;
    LOG_I("(config_monitor) thread started on core %d...", xPortGetCoreID());

    st.config_timeout = MINER_WIFI_CONFIG_TIMEOUT;
    while (true) {
        if (WL_CONNECTED == st.status) break;

        if (st.client_connected == false) {
            // For NMQAxe++: the UI thread owns the decrement (lv_indev touch detect);
            // this thread only fires the reboot when timeout reaches 0.
            if (ctx->cfg->board_name == BOARD_NMQAXE_PLUS_PLUS_NAME) {
                if (st.config_timeout == 0) {
                    LOG_W("WiFi configuration timeout, rebooting...");
                    reboot_intent_set(REBOOT_INTENT_WIFI_CONFIG_TIMEOUT,
                                      "no client connected during AP setup window");
                    delay(1000);
                    ESP.restart();
                }
                // Do NOT decrement here — UI thread handles it
            } else {
                if (st.config_timeout == 0) {
                    LOG_W("WiFi configuration timeout, rebooting...");
                    reboot_intent_set(REBOOT_INTENT_WIFI_CONFIG_TIMEOUT,
                                      "no client connected during AP setup window");
                    delay(1000);
                    ESP.restart();
                }
                st.config_timeout--;
            }
        }
        delay(1000);
    }
    LOG_I("WiFi configuration monitor exit...");
    vTaskDelete(NULL);
}

// ── WiFi: STA connect with AP config-portal fallback ────────────────────────
//    Mirrors legacy wifi_connect_thread_entry; board_sal_t* -> WifiCtx*.
void wifi_connect_thread_entry(void* args) {
    WifiCtx* ctx = static_cast<WifiCtx*>(args);
    WifiState&      st  = *ctx->state;
    WifiConnConfig& cfg = *ctx->cfg;

    auto wifiEvent = [ctx](WiFiEvent_t event, WiFiEventInfo_t info) {
        WifiState& st = *ctx->state;
        const char* reason = NULL;
        static uint8_t retry_cnt = 0, max_retries = 120;
        wifi_event_sta_disconnected_t disconnected;
        switch (event) {
            case SYSTEM_EVENT_STA_CONNECTED:
                LOG_I("WiFi connected to [%s], waiting for IP...", WiFi.SSID().c_str());
                break;
            case SYSTEM_EVENT_STA_GOT_IP:
                st.ip      = WiFi.localIP();
                st.gateway = WiFi.gatewayIP();
                st.subnet  = WiFi.subnetMask();
                st.dns     = WiFi.dnsIP();
                st.status  = WL_CONNECTED;
                retry_cnt  = 0;
                LOG_I("Got IP : %s", WiFi.localIP().toString().c_str());
                break;
            case SYSTEM_EVENT_STA_DISCONNECTED:
                st.ip = st.gateway = st.subnet = st.dns = IPAddress(0, 0, 0, 0);
                st.status = WL_DISCONNECTED;
                disconnected = info.wifi_sta_disconnected;
                reason = WiFi.disconnectReasonName((wifi_err_reason_t)disconnected.reason);
                retry_cnt++;
                LOG_W("WiFi disconnected, reason: %s", reason);
                break;
            default:
                break;
        }
        if (retry_cnt >= max_retries) {
            LOG_W("WiFi connection retry limit reached, rebooting...");
            reboot_intent_set(REBOOT_INTENT_WIFI_RECONNECT_FAIL, "reached %d/%d retries", retry_cnt, max_retries);
            delay(1000);
            ESP.restart();
        }
    };

    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_15dBm);
    WiFi.onEvent(wifiEvent);
    WiFi.setHostname(cfg.hostname.c_str());

    // ── force config mode ──
    if (st.force_config_required) {
        nvs_config_set_u8(NVS_CONFIG_FORCE_CONFIG, false);
        LOG_W("Set softAP [%s]...", cfg.ap_ssid.c_str());
        WiFi.mode(WIFI_AP);
        WiFi.softAP(cfg.ap_ssid);
        WiFi.softAPConfig(cfg.ap_ip, cfg.ap_ip, IPAddress(255, 255, 255, 0));
        delay(500);
        xEventGroupSetBits(ctx->init_evt, INIT_EVENT_WIFI_AP_READY);
        xTaskCreatePinnedToCore(config_monitor_thread_entry, "(config_monitor)", 1024 * 4, ctx, TASK_PRIORITY_CONFIG, NULL, 1);
        while (true) {
            st.client_connected = (WiFi.softAPgetStationNum() > 0);
            if (WiFi.softAPgetStationNum() == 0)
                LOG_W("Force configuration, ssid[%s], timeout: %ds...", cfg.ap_ssid.c_str(), st.config_timeout);
            delay(1000);
        }
    }

    // ── normal wifi connection ──
    uint16_t random_delay = random(0, 1000 * 8);
    LOG_I("Initializing WiFi, delay: %dms...", random_delay);
    delay(random_delay);
    LOG_I("Try to connect [%s]...", cfg.sta_ssid.c_str());
    WiFi.begin(cfg.sta_ssid.c_str(), cfg.sta_pwd.c_str());
    int maxRetries = 0;
    while (WiFi.status() != WL_CONNECTED && maxRetries < 60 * 5) {
        maxRetries++;
        LOG_I("Try to connect [%s] %ds...", cfg.sta_ssid.c_str(), maxRetries);
        if (maxRetries >= 15) {
            LOG_I("Set softAP [%s]...", cfg.hostname.c_str());
            WiFi.mode(WIFI_AP);
            WiFi.softAP(cfg.ap_ssid);
            WiFi.softAPConfig(cfg.ap_ip, cfg.ap_ip, IPAddress(255, 255, 255, 0));
            delay(500);
            xEventGroupSetBits(ctx->init_evt, INIT_EVENT_WIFI_AP_READY);
            xTaskCreatePinnedToCore(config_monitor_thread_entry, "(config_monitor)", 1024 * 4, ctx, TASK_PRIORITY_CONFIG, NULL, 1);
            while (true) {
                st.client_connected = (WiFi.softAPgetStationNum() > 0);
                if (WiFi.softAPgetStationNum() == 0)
                    LOG_W("Force configuration, ssid[%s], timeout: %ds...", cfg.ap_ssid.c_str(), st.config_timeout);
                delay(1000);
            }
        }
        delay(1000);
    }

    LOG_I("------------------------------------");
    LOG_I("SSID     : %s ", WiFi.SSID().c_str());
    LOG_I("IP       : %s ", WiFi.localIP().toString().c_str());
    LOG_I("RSSI     : %d dBm", WiFi.RSSI());
    LOG_I("Channel  : %d", WiFi.channel());
    LOG_I("Gateway  : %s", WiFi.gatewayIP().toString().c_str());
    LOG_I("Subnet   : %s", WiFi.subnetMask().toString().c_str());
    LOG_I("MAC      : %s", WiFi.macAddress().c_str());
    LOG_I("Hostname : %s", WiFi.getHostname());
    LOG_I("------------------------------------");

    xEventGroupSetBits(ctx->init_evt, INIT_EVENT_WIFI_STA_CONNECTED);
    vTaskDelete(NULL);
}

// ── Power init: bring up rails, gate on fan/wifi/vbus, set vcore ────────────
//    Mirrors legacy power_init_thread_entry; board_sal_t* -> PowerCtx* (DI).
void power_init_thread_entry(void* args) {
    PowerCtx* ctx = static_cast<PowerCtx*>(args);
    AxePowerHal* power = ctx->power;
    const BoardSpecConfig& spec = *ctx->spec;

    power->set_vcore_range(spec.asic.min_vcore, spec.asic.max_vcore);
    LOG_D("Set vcore range to (%d~%d mV)", power->get_vcore_min(), power->get_vcore_max());

    // detect power plug or pd plug
    if (power->is_dc_pluged()) LOG_I("DC plug detected...");
    else                       LOG_D("USB plug detected...");
    delay(100);
    power->init();
    // set vdd_1v8 and pll_0v8 power
    power->set_pll_0v8(PWR_ON);
    power->set_vdd_1v8(PWR_ON);
    delay(100); // wait for power stable
    xEventGroupSetBits(ctx->init_evt, INIT_EVENT_VDD_VPLL_READY);

    // Do not block early boot on low Vbus: USB-only power must still be able to
    // continue through WiFi / ASIC count so the loading page can show chip detect.
    // Vcore bring-up itself remains gated on sufficient Vbus.
    const EventBits_t vcore_gate_bits = INIT_EVENT_FAN_READY | INIT_EVENT_WIFI_STA_CONNECTED;
    uint32_t last_vbus_warn_ms = 0;
    while (true) {
        EventBits_t gate = xEventGroupWaitBits(ctx->init_evt,
                                               vcore_gate_bits,
                                               pdFALSE, pdTRUE,
                                               pdMS_TO_TICKS(100));
        bool vbus_ready = power->get_vbus() >= spec.pwr.vbus_min_required;
        if (vbus_ready) {
            xEventGroupSetBits(ctx->init_evt, INIT_EVENT_VBUS_READY);
        } else {
            xEventGroupClearBits(ctx->init_evt, INIT_EVENT_VBUS_READY);
            uint32_t now = millis();
            if (last_vbus_warn_ms == 0 || now - last_vbus_warn_ms >= 1000) {
                LOG_W("Vbus is %.2fV , at least %.2fV required...",
                      power->get_vbus() / 1000.0, spec.pwr.vbus_min_required / 1000.0);
                last_vbus_warn_ms = now;
            }
        }

        if (((gate & vcore_gate_bits) == vcore_gate_bits) && vbus_ready) {
            break;
        }
    }

    // wait for fan ready, wifi connected, and sufficient Vbus before setting
    // vcore voltage, to avoid too high temperature without proper cooling or
    // network connection for error reporting.
    // set vcore voltage to required voltage
    power->set_vcore_voltage(spec.asic.req_vcore);
    power->set_vcore_status(PWR_ON);
    while (!power->is_vcore_ready()) {
        delay(500);
        LOG_W("Waiting for vcore power setup...");
    }
    xEventGroupSetBits(ctx->init_evt, INIT_EVENT_VCORE_READY);
    delay(100);

    LOG_D("Vcore ready at %dmV/%dmV", power->get_vcore(), spec.asic.req_vcore);

    vTaskDelete(NULL);
}

// ── Power loop: OC/OT protection + vcore closed-loop regulation ─────────────
//    Mirrors legacy power_loop_thread_entry; board_sal_t* -> PowerCtx* (DI).
void power_loop_thread_entry(void* args) {
    PowerCtx* ctx = static_cast<PowerCtx*>(args);
    AxePowerHal* power = ctx->power;
    const BoardSpecConfig& spec = *ctx->spec;

    xEventGroupWaitBits(ctx->init_evt, INIT_EVENT_VCORE_READY, pdFALSE, pdTRUE, portMAX_DELAY);

    while (true) {
        delay(50);

        const bool ota_running = (ctx->ota_running && *ctx->ota_running);
        if (!ota_running) {
            bool oc_warn  = power->is_oc_warn();
            bool oc_fault = power->is_oc_fault();
            bool ot_warn  = power->is_ot_warn();
            bool ot_fault = power->is_ot_fault();
            if (oc_fault) {
                LOG_W("Overcurrent FAULT detected! Taking safety actions...");
                // Immediate safety action: shut down ASIC power
                power->set_vcore_status(PWR_OFF);
                power->set_vdd_1v8(PWR_OFF);
                power->set_pll_0v8(PWR_OFF);
                // Pre-stamp the real root cause so the ASIC-frozen watchdog (3 min later)
                // sees this intent and does not overwrite it.
                reboot_intent_set(REBOOT_INTENT_OVERCURRENT_FAULT,
                                  "power overcurrent fault, Vcore/VDD/PLL shut down");
                xEventGroupSetBits(ctx->sys_evt, SYS_EVENT_POWER_OC_FAULT);
                // Park this thread — display/UI thread keeps running for user interaction
                while (true) {
                    delay(1000);
                    LOG_E("ASIC powered down due to Power OverCurrent. Please check your ASICs OverClocking settings, and cooling.");
                }
            } else if (oc_warn) {
                static uint32_t last = millis();
                if (millis() - last >= 3000) {
                    LOG_W("Overcurrent WARNING detected...");
                    last = millis();
                }
            } else if (ot_fault) {
                LOG_W("Overtemperature FAULT detected! Taking safety actions...");
                power->set_vcore_status(PWR_OFF);
                power->set_vdd_1v8(PWR_OFF);
                power->set_pll_0v8(PWR_OFF);
                reboot_intent_set(REBOOT_INTENT_OVERHEAT_VCORE,
                                  "power overtemperature fault, Vcore/VDD/PLL shut down");
                xEventGroupSetBits(ctx->sys_evt, SYS_EVENT_POWER_OT_FAULT);
                while (true) {
                    delay(1000);
                    LOG_E("ASIC powered down due to OverTemperature. Please check your cooling system and ambient temperature.");
                }
            } else if (ot_warn) {
                static uint32_t last = millis();
                if (millis() - last >= 3000) {
                    LOG_W("Overtemperature WARNING detected...");
                    last = millis();
                }
            }
        }

        // skip vcore regulation while the miner is intentionally idle
        if (ctx->mining && ctx->mining->is_controlled_idle()) {
            continue;
        }

        uint32_t vcore_measure = power->get_vcore();
        int32_t err = vcore_measure - spec.asic.req_vcore;
        if (abs(err) <= 5) {
            LOG_D("Vcore %d/%dmV, error %d mV, Vcore within acceptable range",
                  vcore_measure, spec.asic.req_vcore, err);
            continue;
        }
        LOG_D("Vcore %d/%dmV, error %d mV, Adjust vcore for error correction %d mV",
              vcore_measure, spec.asic.req_vcore, err, err / 5);
        static uint32_t vcore_set = spec.asic.req_vcore;
        vcore_set -= err / 2; // half error correction
        vcore_set = (vcore_set < power->get_vcore_min()) ? power->get_vcore_min() : vcore_set;
        vcore_set = (vcore_set > power->get_vcore_max()) ? power->get_vcore_max() : vcore_set;
        power->set_vcore_voltage(vcore_set);
    }
    // exit
    vTaskDelete(NULL);
}

// ── Fan: TMP102 self-test, polarity detect, self-test, PID speed control ────
//    Mirrors legacy fan_thread_entry; board_sal_t* -> FanCtx* (DI).
void fan_thread_entry(void* args) {
    FanCtx* ctx = static_cast<FanCtx*>(args);
    BoardSpecConfig& spec = *ctx->spec;
    std::vector<fan_status_t>& fan_list = *ctx->status_list;

    const size_t fan_n = spec.fans.size();
    std::vector<int16_t>  now_count(fan_n, 0), last_count(fan_n, 0);
    std::vector<uint32_t> start_ms(fan_n, 0);
    delay(100);

    // Initialize TMP102 temperature sensor
    tmp102_init();

    // TMP102 self-test: retry until both sensors pass (3-sample average each attempt)
    while (true) {
        float vcore_sum = 0, asic_sum = 0;
        int   vcore_cnt = 0, asic_cnt = 0;
        for (int i = 0; i < 3; i++) {
            float t;
            t = temp_hal_get_vcore();
            if (!isnan(t)) { vcore_sum += t; vcore_cnt++; }
            t = temp_hal_get_asic();
            if (!isnan(t)) { asic_sum += t; asic_cnt++; }
            delay(50);
        }
        float vcore_avg = (vcore_cnt > 0) ? vcore_sum / vcore_cnt : NAN;
        float asic_avg  = (asic_cnt  > 0) ? asic_sum  / asic_cnt  : NAN;
        bool vcore_ok = !isnan(vcore_avg);
        bool asic_ok  = !isnan(asic_avg);
        if (vcore_ok) LOG_D("TMP102 VRM : OK %.1fC", vcore_avg);
        else          LOG_W("TMP102 VRM : FAIL (no response)");
        if (asic_ok)  LOG_D("TMP102 ASIC: OK %.1fC", asic_avg);
        else          LOG_W("TMP102 ASIC: FAIL (no response)");
        if (vcore_ok && asic_ok) break;
        LOG_E("TMP102 self test failed, retrying...");
        delay(500);
    }
    xEventGroupSetBits(ctx->init_evt, INIT_EVENT_TMP_READY);

    // fan init
    for (auto& fan : spec.fans) {
        fan_drv_init(fan.init);
        LOG_D("Fan[%d] initialized with torch pin %d, pwm pin %d",
              fan.id, fan.init.torch.pulse_gpio_num, fan.init.pwm.pin);
    }

    // Local helper types for parallel fan init — local structs + captureless lambdas
    // decay to TaskFunction_t
    struct PolarityArg {
        fan_init_t        init_param;   // copied — each fan owns independent LEDC ch + PCNT unit
        bool*             out_polarity;
        SemaphoreHandle_t done;
    };
    struct SelfTestArg {
        fan_init_t        init_param;
        bool              fan_invert;
        fan_status_t*     fan_status;
        uint16_t          rpm_thr;
        SemaphoreHandle_t done;
    };

    // polarity detection — all fans probed in parallel
    {
        auto polarity_task = [](void* pv) {
            auto* a = static_cast<PolarityArg*>(pv);
            *a->out_polarity = guess_fan_polarity(a->init_param);
            xSemaphoreGive(a->done);
            vTaskDelete(NULL);
        };
        size_t n = spec.fans.size();
        SemaphoreHandle_t pol_done = xSemaphoreCreateCounting(n, 0);
        std::vector<PolarityArg> pol_args(n);
        size_t i = 0;
        for (auto& fan : spec.fans) {
            pol_args[i].init_param   = fan.init;
            pol_args[i].out_polarity = &fan.polarity;
            pol_args[i].done         = pol_done;
            xTaskCreate(polarity_task, "fan_pol", 4096, &pol_args[i], uxTaskPriorityGet(NULL), NULL);
            i++;
        }
        for (size_t j = 0; j < n; j++) xSemaphoreTake(pol_done, portMAX_DELAY);
        vSemaphoreDelete(pol_done);
        for (auto& fan : spec.fans)
            LOG_I("Guess fan[%d] polarity :[%s]", fan.id, fan.polarity ? "inverted" : "normal");
    }
    xEventGroupSetBits(ctx->init_evt, INIT_EVENT_FAN_POLARITY_DETECT);

    // Helper lambdas to find fan config/status by id
    auto get_fan_config = [&](uint8_t fan_id) -> fan_config_t* {
        for (auto& fan_cfg : spec.fans)
            if (fan_cfg.id == fan_id) return &fan_cfg;
        return nullptr;
    };
    auto get_fan_status = [&](uint8_t fan_id) -> fan_status_t* {
        for (auto& fs : fan_list)
            if (fs.id == fan_id) return &fs;
        return nullptr;
    };

    // fan self test — all fans tested in parallel
    while (true) {
        auto selftest_task = [](void* pv) {
            auto* a = static_cast<SelfTestArg*>(pv);
            uint16_t rpm = 0;
            for (uint8_t i = 0; i < 3; i++) {
                rpm = measure_fan_rpm_for_duration(a->init_param, 1.0f, 1000, a->fan_invert);
                a->fan_status->rpm = rpm; // live update so loading page shows real-time RPM
            }
            a->fan_status->self_test = (rpm > a->rpm_thr);
            LOG_W("Fan[%d] self test result: %s, measured rpm: %d, threshold rpm: %d",
                  a->fan_status->id, a->fan_status->self_test ? "OK" : "FAIL", rpm, a->rpm_thr);
            xSemaphoreGive(a->done);
            vTaskDelete(NULL);
        };
        size_t n = fan_list.size();
        SemaphoreHandle_t st_done = xSemaphoreCreateCounting(n, 0);
        std::vector<SelfTestArg> st_args(n);
        size_t i = 0;
        for (auto& fan : fan_list) {
            fan_config_t* fan_cfg = get_fan_config(fan.id);
            if (fan_cfg == nullptr) { xSemaphoreGive(st_done); i++; continue; }
            st_args[i].init_param = fan_cfg->init;
            st_args[i].fan_invert = fan_cfg->polarity;
            st_args[i].fan_status = &fan;
            st_args[i].rpm_thr    = fan_cfg->init.self_test_rpm_thr;
            st_args[i].done       = st_done;
            xTaskCreate(selftest_task, "fan_st", 4096, &st_args[i], uxTaskPriorityGet(NULL), NULL);
            i++;
        }
        for (size_t j = 0; j < n; j++) xSemaphoreTake(st_done, portMAX_DELAY);
        vSemaphoreDelete(st_done);

        bool all_fan_ok = true;
        for (auto& fan : fan_list) all_fan_ok = all_fan_ok && fan.self_test;

        if (all_fan_ok) { LOG_W("All fans passed self test."); break; }
        else LOG_W("Some fans failed self test, retrying...");
    }
    xEventGroupSetBits(ctx->init_evt, INIT_EVENT_FAN_READY);
    delay(2000); // let user see the self-test result on loading page before fan speed changes

    // fan control loop
    while (true) {
        uint8_t fan_id = 0; // default fan 0; extend with separate events for multi-fan
        EventBits_t bits = xEventGroupWaitBits(ctx->sys_evt,
            SYS_EVENT_MINER_VCORE_TEMP_UPDATE | SYS_EVENT_MINER_ASIC_TEMP_UPDATE,
            pdFALSE, pdFALSE, portMAX_DELAY);
        if ((bits & SYS_EVENT_MINER_ASIC_TEMP_UPDATE) == SYS_EVENT_MINER_ASIC_TEMP_UPDATE) {
            fan_id = 0; // fan 0 responds to ASIC temp updates
            xEventGroupClearBits(ctx->sys_evt, SYS_EVENT_MINER_ASIC_TEMP_UPDATE);
        }
        if ((bits & SYS_EVENT_MINER_VCORE_TEMP_UPDATE) == SYS_EVENT_MINER_VCORE_TEMP_UPDATE) {
            fan_id = 1; // fan 1 responds to Vcore temp updates
            xEventGroupClearBits(ctx->sys_evt, SYS_EVENT_MINER_VCORE_TEMP_UPDATE);
        }

        fan_config_t* fan_cfg    = get_fan_config(fan_id);
        fan_status_t* fan_status = get_fan_status(fan_id);
        if (fan_cfg == nullptr || fan_status == nullptr) continue;

        bool fan_invert = fan_cfg->polarity;
        fan_init_t init_param = fan_cfg->init;
        // Calculate fan RPM
        if (millis() - start_ms[fan_id] >= 1000) {
            uint32_t delta_time = millis() - start_ms[fan_id];
            pcnt_get_counter_value(init_param.torch.unit, &now_count[fan_id]);
            uint16_t delta_pcnt = 0;
            if (now_count[fan_id] < last_count[fan_id])
                delta_pcnt = (init_param.torch.counter_h_lim - last_count[fan_id]) + now_count[fan_id];
            else
                delta_pcnt = now_count[fan_id] - last_count[fan_id];
            fan_status->rpm = calculate_rpm(delta_pcnt, delta_time / 1000.0);
            last_count[fan_id] = now_count[fan_id];
            start_ms[fan_id] = millis();
        }

        // Adjust fan speed
        if (fan_cfg->auto_speed && fan_status->self_test) {
            static uint32_t pid_start[2] = {0};
            float dt = (millis() - pid_start[fan_id]) / 1000.0f;
            float target   = fan_cfg->target_temp;
            float measured = (fan_id == 0) ? ctx->temp->asic : ctx->temp->vcore; // fan0=ASIC, fan1=Vcore
            fan_status->speed = (uint16_t)pid_compute(&fan_cfg->pid, target, measured, dt);
            pid_start[fan_id] = millis();
        }
        fan_set_speed(init_param, fan_status->speed / 100.0, fan_invert);
    }
}

// ── Monitor: NTP/time sync, telemetry, hashrate calc, safety watchdogs ──────
//    Mirrors legacy monitor_thread_entry; board_sal_t* -> MonitorCtx* (DI).
//    UI page-rolling and backlight control are owned by the reflect UI
//    framework, so those legacy branches are intentionally omitted here.
void monitor_thread_entry(void* args) {
    MonitorCtx* ctx = static_cast<MonitorCtx*>(args);
    AxePowerHal*     power = ctx->power;
    BoardSpecConfig& spec  = *ctx->spec;
    MinerStatus&     st    = *ctx->status;
    LOG_I("(monitor) thread started on core %d...", xPortGetCoreID());

    // NTP server list — rotated on consecutive failures; global coverage.
    static const char* const NTP_SERVERS[] = {
        "pool.ntp.org", "time.cloudflare.com", "time.apple.com", "time.windows.com",
        "ntp.aliyun.com", "ntp.tencent.com", "ntp.ntsc.ac.cn", "cn.pool.ntp.org",
        "asia.pool.ntp.org", "europe.pool.ntp.org", "ntp.ubuntu.com",
        "north-america.pool.ntp.org", "time.google.com",
        "south-america.pool.ntp.org", "a.ntp.br", "africa.pool.ntp.org",
        "oceania.pool.ntp.org", "au.pool.ntp.org",
    };
    static const uint8_t  NTP_SERVER_COUNT  = sizeof(NTP_SERVERS) / sizeof(NTP_SERVERS[0]);
    static const uint8_t  NTP_MAX_FAIL      = 3;
    static const uint32_t NTP_SYNC_INTERVAL = 3600;

    uint8_t  ntp_server_idx  = 0;
    uint8_t  ntp_fail_cnt    = 0;
    bool     ntp_ever_synced = false;
    uint64_t last_ntp_sync   = 0;

    WiFiUDP   udpNtpClient;
    NTPClient* ntpClient = new NTPClient(udpNtpClient, NTP_SERVERS[ntp_server_idx]);
    uint64_t  last_nvs_save_time = st.uptime_session;

    ntpClient->begin();
    ntpClient->setTimeOffset(0); // UTC, no timezone offset
    LOG_I("NTP client started, primary server: %s", NTP_SERVERS[ntp_server_idx]);

    while (true) {
        delay(1000);

        // --- NTP sync ---
        bool wifi_up     = (WL_CONNECTED == *ctx->wifi_status);
        bool first_sync  = (!ntp_ever_synced && wifi_up);
        bool interval_ok = (st.uptime_session - last_ntp_sync >= NTP_SYNC_INTERVAL);

        if (wifi_up && (first_sync || interval_ok)) {
            LOG_I("NTP sync attempt [%d/%d] using server: %s ...", ntp_fail_cnt + 1, NTP_MAX_FAIL, NTP_SERVERS[ntp_server_idx]);
            bool sync_ok = ntpClient->forceUpdate();

            if (sync_ok) {
                struct timeval tv;
                tv.tv_sec  = ntpClient->getEpochTime();
                tv.tv_usec = 0;
                settimeofday(&tv, NULL);
                *ctx->utc = tv.tv_sec;

                float tz_offset = ctx->tz->toFloat();
                int tz_hour = (int)tz_offset;
                int tz_min  = (int)((fabs(tz_offset) - abs(tz_hour)) * 60 + 0.5f);

                char tz_buf[16] = {0};
                if (tz_offset >= 0) {
                    if (tz_min == 0) sprintf(tz_buf, "UTC-%d",     tz_hour);
                    else             sprintf(tz_buf, "UTC-%d:%02d", tz_hour, tz_min);
                } else {
                    if (tz_min == 0) sprintf(tz_buf, "UTC+%d",     abs(tz_hour));
                    else             sprintf(tz_buf, "UTC+%d:%02d", abs(tz_hour), tz_min);
                }
                setenv("TZ", tz_buf, 1);
                tzset();

                String time_local = convert_time_to_local((uint32_t)*ctx->utc);
                LOG_W("NTP sync OK  [server: %s] UTC[%llu] local[%s] tz[%s] env[%s]",
                      NTP_SERVERS[ntp_server_idx], (unsigned long long)*ctx->utc,
                      time_local.c_str(), ctx->tz->c_str(), tz_buf);

                ntp_fail_cnt    = 0;
                ntp_ever_synced = true;
                last_ntp_sync   = st.uptime_session;
            } else {
                ntp_fail_cnt++;
                LOG_W("NTP sync FAIL [server: %s] consecutive_fail=%d/%d  ever_synced=%s  last_sync=%llus ago",
                      NTP_SERVERS[ntp_server_idx], ntp_fail_cnt, NTP_MAX_FAIL,
                      ntp_ever_synced ? "yes" : "no",
                      (unsigned long long)(ntp_ever_synced ? (st.uptime_session - last_ntp_sync) : 0));

                if (ntp_fail_cnt >= NTP_MAX_FAIL) {
                    uint8_t old_idx = ntp_server_idx;
                    ntp_server_idx  = (ntp_server_idx + 1) % NTP_SERVER_COUNT;
                    ntp_fail_cnt    = 0;
                    ntpClient->end();
                    delete ntpClient;
                    ntpClient = new NTPClient(udpNtpClient, NTP_SERVERS[ntp_server_idx]);
                    ntpClient->begin();
                    ntpClient->setTimeOffset(0);
                    LOG_W("NTP server rotated: [%s] -> [%s]", NTP_SERVERS[old_idx], NTP_SERVERS[ntp_server_idx]);
                }

                time_t now;
                time(&now);
                *ctx->utc = now;
            }
        } else {
            time_t now;
            time(&now);
            *ctx->utc = now;
        }

        st.uptime_ever++;
        st.uptime_session++;

        // --- per-second telemetry, hashrate recalc, efficiency, distribution ---
        {
            ctx->pwr->vbus  = power->get_vbus();
            ctx->pwr->ibus  = power->get_ibus();
            ctx->pwr->vcore = power->get_vcore();
            if (ctx->wifi_rssi) *ctx->wifi_rssi = WiFi.RSSI();
            if (st.hashrate._3m > 0)
                st.efficiency = (ctx->pwr->vbus * ctx->pwr->ibus / 1e6) / (st.hashrate._3m / 1e12); // J/TH
            ctx->miner->calculate_hashrate(&st.hashrate);
            xSemaphoreGive(st.update_xsem);

            // hashrate distribution histogram (PSRAM-backed counts)
            spec.ui.hashrate_dist_page.time = st.uptime_session;
            {
                static uint16_t SCALE = (spec.ui.hashrate_dist_page.max_x_hr / spec.ui.hashrate_dist_page.max_x_bars);
                static uint64_t* counts = NULL;
                if (counts == NULL) {
                    counts = (uint64_t*)heap_caps_malloc(spec.ui.hashrate_dist_page.max_x_bars * sizeof(uint64_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (counts == NULL) {
                        LOG_E("Failed to allocate hashrate distribution counts in PSRAM");
                        continue;
                    }
                    memset(counts, 0, spec.ui.hashrate_dist_page.max_x_bars * sizeof(uint64_t));
                }
                double hr_now = st.hashrate._3m;
                int index = (int)(hr_now / 1000 / 1000 / 1000 / SCALE); // GH/s, scaled
                index = (index >= (int)spec.ui.hashrate_dist_page.max_x_bars) ? spec.ui.hashrate_dist_page.max_x_bars - 1 : index;
                counts[index]++;
                spec.ui.hashrate_dist_page.count++;
                for (int i = 0; i < (int)spec.ui.hashrate_dist_page.max_x_bars; i++) {
                    uint8_t y = (uint8_t)(100 * (float)counts[i] / (float)spec.ui.hashrate_dist_page.count);
                    spec.ui.hashrate_dist_page.dist_map[i] = y;
                }
            }
        }

        // --- safety status checks (skipped entirely in benchmark mode) ---
        if (st.uptime_session % 2 == 0) {
            if (*ctx->bm_mode == 0) {
            if (!(ctx->ota_running && *ctx->ota_running)) {
                static uint8_t  vcore_err_cnt = 0;
                static uint8_t  asic_err_cnt  = 0;
                static uint16_t fan_err_cnt   = 0;
                static uint16_t pwr_err_cnt   = 0;
                static uint16_t hr_err_cnt    = 0;
                bool suppress_activity_checks = st.suppress_activity_checks(millis());
                if (suppress_activity_checks) {
                    fan_err_cnt = 0;
                    pwr_err_cnt = 0;
                    hr_err_cnt  = 0;
                    st.asic_update = millis();
                }

                // vcore temperature
                if (ctx->temp->vcore > spec.pwr.temp_limit.high) {
                    uint16_t vcore_req_last = spec.asic.req_vcore;
                    uint8_t step = 5;
                    if (spec.asic.req_vcore >= spec.asic.min_vcore + step) spec.asic.req_vcore -= step;
                    else                                                   spec.asic.req_vcore = spec.asic.min_vcore;
                    LOG_W("Vcore part temp reach danger (vcore: %.1fC), decrease vcore from %d to %d mV", ctx->temp->vcore, vcore_req_last, spec.asic.req_vcore);
                    if (++vcore_err_cnt > 10) {
                        LOG_W("Vcore part temp keep danger, restart miner...");
                        reboot_intent_set(REBOOT_INTENT_OVERHEAT_VCORE, "Vcore=%.1fC > limit=%dC for >10s", ctx->temp->vcore, (int)spec.pwr.temp_limit.high);
                        xSemaphoreGive(ctx->reboot_xsem);
                    }
                } else vcore_err_cnt = 0;

                // asic temperature
                if (ctx->temp->asic > spec.asic.temp_limit.high) {
                    uint16_t vcore_req_last = spec.asic.req_vcore;
                    uint8_t step = 5;
                    if (spec.asic.req_vcore >= spec.asic.min_vcore + step) spec.asic.req_vcore -= step;
                    else                                                   spec.asic.req_vcore = spec.asic.min_vcore;
                    LOG_W("ASIC temp reach danger (asic: %.1fC), decrease vcore from %d to %d mV", ctx->temp->asic, vcore_req_last, spec.asic.req_vcore);
                    if (++asic_err_cnt > 10) {
                        LOG_W("ASIC temp keep danger, restart miner...");
                        reboot_intent_set(REBOOT_INTENT_OVERHEAT_ASIC, "ASIC=%.1fC > limit=%dC for >10s", ctx->temp->asic, (int)spec.asic.temp_limit.high);
                        xSemaphoreGive(ctx->reboot_xsem);
                    }
                } else asic_err_cnt = 0;

                // fan status
                if (!suppress_activity_checks && ctx->temp->asic > spec.asic.temp_limit.low) {
                    for (auto& fan : *ctx->fan_status) {
                        if (fan.rpm < spec.fans[fan.id].init.danger_rpm_thr) {
                            fan_err_cnt++;
                            if (fan_err_cnt > 20) {
                                LOG_W("Fan rpm is too low, restart miner...");
                                reboot_intent_set(REBOOT_INTENT_FAN_STALL, "fan#%d rpm=%d < danger=%d for >20s", fan.id, fan.rpm, spec.fans[fan.id].init.danger_rpm_thr);
                                xSemaphoreGive(ctx->reboot_xsem);
                            }
                        } else fan_err_cnt = 0;
                    }
                }

                // power status
                bool miner_ready = ctx->init_evt &&
                                   ((xEventGroupGetBits(ctx->init_evt) & INIT_EVENT_MINER_READY) != 0);
                if (!suppress_activity_checks &&
                    miner_ready &&
                    (ctx->pwr->vbus * ctx->pwr->ibus / 1000.0 / 1000.0) < spec.pwr.power_low_threshold) {
                    LOG_W("Power %0.1fW is too low...", ctx->pwr->vbus * ctx->pwr->ibus / 1000.0 / 1000.0);
                    if (++pwr_err_cnt > 120) {
                        LOG_W("Power is too low, restart miner...");
                        reboot_intent_set(REBOOT_INTENT_POWER_LOW, "input %.1fW < threshold=%.1fW for >120s", ctx->pwr->vbus * ctx->pwr->ibus / 1000.0 / 1000.0, (double)spec.pwr.power_low_threshold);
                        xSemaphoreGive(ctx->reboot_xsem);
                    }
                } else pwr_err_cnt = 0;

                // hashrate
                if (!suppress_activity_checks && st.hashrate._3m <= 1) {
                    if (++hr_err_cnt > 60) {
                        LOG_W("Hashrate is too low, restart miner...");
                        reboot_intent_set(REBOOT_INTENT_LOW_HASHRATE, "3m hashrate %.2f <= 1 for >60s", (double)st.hashrate._3m);
                        xSemaphoreGive(ctx->reboot_xsem);
                    }
                } else hr_err_cnt = 0;
            } // end !ota.running
            } // end bm_mode == 0
        }

        // save status to NVS (interval-driven request)
        if (st.uptime_session - last_nvs_save_time > BOARD_NVS_SAVE_INTERVAL) {
            xSemaphoreGive(ctx->nvs_save_xsem);
        }

        // update miner status history queue
        if (st.uptime_session % MINER_HISTORY_SAMPLE_INTERVAL == 0) {
            history_node_t node;
            node.hashrate   = (float)(st.hashrate._3m / 1e9);  // GH/s
            node.asic_temp  = ctx->temp->asic;
            node.vcore_temp = ctx->temp->vcore;
            node.pbus       = (ctx->pwr->vbus * ctx->pwr->ibus / 1000.0f / 1000.0f); // W
            node.vbus       = (ctx->pwr->vbus / 1000.0f); // V
            node.ibus       = (ctx->pwr->ibus / 1000.0f); // A
            node.vcore      = ctx->pwr->vcore;            // mV
            node.fanspeed   = ctx->fan_status->empty() ? 0 : (*ctx->fan_status)[0].speed;
            node.fanrpm     = ctx->fan_status->empty() ? 0 : (*ctx->fan_status)[0].rpm;
            node.wifi_rssi  = ctx->wifi_rssi ? *ctx->wifi_rssi : 0;
            node.free_ram   = ESP.getFreeHeap() / 1024;
            node.free_psram = ESP.getFreePsram() / 1024;
            node.epoch      = *ctx->utc * 1000ULL;
            node.latency    = st.latency;

            if (xSemaphoreTake(st.status_history.mutex, portMAX_DELAY) == pdTRUE) {
                st.status_history.deque.push_back(node);
                while (st.status_history.deque.size() > MINER_HISTORY_MAX_SIZE) {
                    st.status_history.deque.pop_front();
                }
                uint64_t current_time_ms = *ctx->utc * 1000ULL;
                while (!st.status_history.deque.empty()) {
                    uint64_t oldest_time_ms = st.status_history.deque.front().epoch;
                    if (current_time_ms - oldest_time_ms > MINER_HISTORY_SAMPLE_DEEPTH) {
                        st.status_history.deque.pop_front();
                    } else break;
                }
                xSemaphoreGive(st.status_history.mutex);
            }
        }

        // force config if user long-press boot button
        if (xSemaphoreTake(ctx->force_config_xsem, 0) == pdTRUE) {
            LOG_W("Force configuration triggered, starting wifi in AP mode when next reboot...");
            nvs_config_set_u8(NVS_CONFIG_FORCE_CONFIG, true);
            reboot_intent_set(REBOOT_INTENT_FORCE_CONFIG, "long-press boot button -> AP mode on next boot");
            xSemaphoreGive(ctx->reboot_xsem);
        }

        // persist some status to NVS on request
        if (xSemaphoreTake(ctx->nvs_save_xsem, 0) == pdTRUE) {
            nvs_config_set_string(NVS_CONFIG_BEST_EVER, String(st.diff.best_ever).c_str());
            nvs_config_set_u16(NVS_CONFIG_BLOCK_HITS, st.hits);
            nvs_config_set_u64(NVS_CONFIG_UPTIME, st.uptime_ever);
            last_nvs_save_time = st.uptime_session;
            LOG_W("Save diff best ever [%s], block hits [%d], uptime [%s]",
                  formatNumber(st.diff.best_ever, 4).c_str(), st.hits,
                  convert_uptime_to_string(st.uptime_ever).c_str());
        }
    }
}

// ── Daemon: reboot orchestration, factory reset, stratum/ASIC watchdogs ─────
//    Mirrors legacy daemon_thread_entry; board_sal_t* -> DaemonCtx* (DI).
void daemon_thread_entry(void* args) {
    DaemonCtx* ctx = static_cast<DaemonCtx*>(args);
    MinerStatus& st = *ctx->status;
    LOG_I("(daemon) thread started on core %d...", xPortGetCoreID());

    while (true) {
        delay(1000);
        // Mirror uptime / min free heap into RTC for post-crash forensics.
        reboot_log_tick();

        // check reboot request
        if (xSemaphoreTake(ctx->reboot_xsem, 0) == pdTRUE) {
            delay(1500); // keep running=true so display can render 100% before reboot
            if (ctx->ota_running) *ctx->ota_running = false;
            delay(300);  // brief pause for any final rendering
            reboot_intent_set_if_unset(REBOOT_INTENT_DAEMON_GENERIC,
                                       "reboot_xsem fired without explicit intent");
            ESP.restart();
        }

        // avoid restart when ota running
        if (ctx->ota_running && *ctx->ota_running) continue;

        // recover factory if user long press user button
        if (xSemaphoreTake(ctx->recover_factory_xsem, 0) == pdTRUE) {
            LOG_W("Factory reset triggered, erasing config (benchmark results preserved) and restart...");

            char*    bm_result_save    = nvs_config_get_string(NVS_CONFIG_BM_RESULT, "[]");
            uint32_t bm_start_ts_save  = nvs_config_get_u32(NVS_CONFIG_BM_START_TS,  0);
            uint32_t bm_total_sec_save = nvs_config_get_u32(NVS_CONFIG_BM_TOTAL_SEC, 0);
            bool     bm_has_data       = bm_result_save
                                         && strcmp(bm_result_save, "[]") != 0
                                         && strlen(bm_result_save) > 2;
            LOG_I("Factory reset: benchmark saved (has_data=%d, start_ts=%lu, total_sec=%lu)",
                  (int)bm_has_data, (unsigned long)bm_start_ts_save, (unsigned long)bm_total_sec_save);

            if (erase_all_nvs()) {
                if (bm_has_data)            nvs_config_set_string(NVS_CONFIG_BM_RESULT,   bm_result_save);
                if (bm_start_ts_save  > 0)  nvs_config_set_u32(NVS_CONFIG_BM_START_TS,  bm_start_ts_save);
                if (bm_total_sec_save > 0)  nvs_config_set_u32(NVS_CONFIG_BM_TOTAL_SEC, bm_total_sec_save);
                LOG_I("Factory reset: benchmark data restored successfully");

                reboot_intent_set(REBOOT_INTENT_FACTORY_RESET, "user-triggered factory reset");
                xSemaphoreGive(ctx->reboot_xsem);
            } else {
                LOG_E("Factory reset failed!");
            }
            if (bm_result_save) free(bm_result_save);
        }

        // WiFi daemon
        if (xSemaphoreTake(ctx->wifi_reconnect_xsem, 0) == pdTRUE) {
            WiFi.begin(ctx->wifi_cfg->sta_ssid.c_str(), ctx->wifi_cfg->sta_pwd.c_str());
        }

        // skip further checks if wifi not connected
        if (*ctx->wifi_status != WL_CONNECTED) continue;

        // Stratum daemon
        if (millis() - st.stratum_update > MINER_STRATUM_ALIVE_TIMEOUT) {
            LOG_W("Stratum connection seems frozen, restarting...");
            reboot_intent_set(REBOOT_INTENT_POOL_TIMEOUT,
                              "no stratum traffic for %lums", (unsigned long)MINER_STRATUM_ALIVE_TIMEOUT);
            xSemaphoreGive(ctx->reboot_xsem);
        }
        // ASIC daemon — skipped in benchmark mode (benchmark thread handles zero-HR abort)
        if (*ctx->bm_mode == 0 && !st.suppress_activity_checks(millis())) {
            if (millis() - st.asic_update > MINER_ASIC_ALIVE_TIMEOUT) {
                LOG_W("ASIC seems frozen, restarting...");
                reboot_intent_set_if_unset(REBOOT_INTENT_ASIC_FROZEN,
                                  "no ASIC reply for %lums", (unsigned long)MINER_ASIC_ALIVE_TIMEOUT);
                xSemaphoreGive(ctx->reboot_xsem);
            }
        }
    }
    LOG_W("Daemon thread exiting...");
    delay(1000);
    vTaskDelete(NULL);
}

// ── Button: page navigation + force-config / factory-reset long-press ───────
//    Mirrors legacy button_thread_entry; board_sal_t* -> ButtonCtx* (DI).
//    UI navigation routes through injected nav hooks (decoupled from UI fw).
void button_thread_entry(void* args) {
    ButtonCtx* ctx = static_cast<ButtonCtx*>(args);
    const BoardSpecConfig& spec = *ctx->spec;

    OneButton* boot_btn = nullptr;
    OneButton* user_btn = nullptr;

    if (spec.btn.boot_pin != -1) boot_btn = new OneButton(spec.btn.boot_pin, true);
    if (spec.btn.user_pin != -1) user_btn = new OneButton(spec.btn.user_pin, true);

    const EventBits_t OVERLAY_BITS = SYS_EVENT_SCREEN_SAVER_TRIGGERED | SYS_EVENT_FIND_NEIGHBOR_TRIGGERED;
    const EventBits_t CLEAR_BITS   = SYS_EVENT_MINER_BLOCK_HIT | SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED |
                                     SYS_EVENT_SCREEN_SAVER_TRIGGERED | SYS_EVENT_FIND_NEIGHBOR_TRIGGERED;

    if (boot_btn != nullptr) {
        auto click_wrapper = [](void* param) {
            ButtonCtx* c = static_cast<ButtonCtx*>(param);
            bool no_page_switch = (xEventGroupGetBits(c->sys_evt) &
                (SYS_EVENT_SCREEN_SAVER_TRIGGERED | SYS_EVENT_FIND_NEIGHBOR_TRIGGERED)) != 0;
            xEventGroupWaitBits(c->init_evt, INIT_EVENT_MINER_READY, pdFALSE, pdTRUE, portMAX_DELAY);
            xEventGroupClearBits(c->sys_evt, SYS_EVENT_MINER_BLOCK_HIT | SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED |
                                 SYS_EVENT_SCREEN_SAVER_TRIGGERED | SYS_EVENT_FIND_NEIGHBOR_TRIGGERED);
            if (c->on_activity) c->on_activity();
            if (no_page_switch) return;
            if (c->on_next_page) c->on_next_page();
        };
        auto double_click_wrapper = [](void* param) {
            ButtonCtx* c = static_cast<ButtonCtx*>(param);
            bool no_page_switch = (xEventGroupGetBits(c->sys_evt) &
                (SYS_EVENT_SCREEN_SAVER_TRIGGERED | SYS_EVENT_FIND_NEIGHBOR_TRIGGERED)) != 0;
            xEventGroupWaitBits(c->init_evt, INIT_EVENT_MINER_READY, pdFALSE, pdTRUE, portMAX_DELAY);
            xEventGroupClearBits(c->sys_evt, SYS_EVENT_MINER_BLOCK_HIT | SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED |
                                 SYS_EVENT_SCREEN_SAVER_TRIGGERED | SYS_EVENT_FIND_NEIGHBOR_TRIGGERED);
            if (c->on_activity) c->on_activity();
            if (no_page_switch) return;
            if (c->on_prev_page) c->on_prev_page();
        };
        auto long_press_wrapper = [](void* param) {
            ButtonCtx* c = static_cast<ButtonCtx*>(param);
            xSemaphoreGive(c->force_config_xsem);
        };
        boot_btn->attachClick(click_wrapper, ctx);
        boot_btn->attachDoubleClick(double_click_wrapper, ctx);
        boot_btn->attachLongPressStart(long_press_wrapper, ctx);
        boot_btn->attachLongPressStop(NULL);
        boot_btn->attachDuringLongPress(NULL);
    }

    if (user_btn != nullptr) {
        auto click_wrapper = [](void* param) {
            ButtonCtx* c = static_cast<ButtonCtx*>(param);
            xEventGroupWaitBits(c->init_evt, INIT_EVENT_MINER_READY, pdFALSE, pdTRUE, portMAX_DELAY);
            xEventGroupClearBits(c->sys_evt, SYS_EVENT_MINER_BLOCK_HIT | SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED |
                                 SYS_EVENT_SCREEN_SAVER_TRIGGERED | SYS_EVENT_FIND_NEIGHBOR_TRIGGERED);
            if (c->on_activity) c->on_activity();
        };
        auto double_click_wrapper = [](void* param) {
            ButtonCtx* c = static_cast<ButtonCtx*>(param);
            xEventGroupWaitBits(c->init_evt, INIT_EVENT_MINER_READY, pdFALSE, pdTRUE, portMAX_DELAY);
            xEventGroupClearBits(c->sys_evt, SYS_EVENT_MINER_BLOCK_HIT | SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED |
                                 SYS_EVENT_SCREEN_SAVER_TRIGGERED | SYS_EVENT_FIND_NEIGHBOR_TRIGGERED);
            if (c->on_activity) c->on_activity();
        };
        auto long_press_wrapper = [](void* param) {
            ButtonCtx* c = static_cast<ButtonCtx*>(param);
            xSemaphoreGive(c->recover_factory_xsem);
        };
        user_btn->attachClick(click_wrapper, ctx);
        user_btn->attachDoubleClick(double_click_wrapper, ctx);
        user_btn->attachLongPressStart(long_press_wrapper, ctx);
        user_btn->attachLongPressStop(NULL);
        user_btn->attachDuringLongPress(NULL);
    }

    (void)OVERLAY_BITS; (void)CLEAR_BITS;
    while (true) {
        delay(20);
        if (ctx->ota_running && *ctx->ota_running) continue;
        if (boot_btn != nullptr) boot_btn->tick();
        if (user_btn != nullptr) user_btn->tick();
    }
}

// ── LED: WiFi/pool/sys status LEDs (NMAXE/Gamma) or NeoPixel show (NMQAXE++) ─
//    Mirrors legacy led_thread_entry; board_sal_t* -> LedCtx* (DI).
void led_thread_entry(void* args) {
    LedCtx* ctx = static_cast<LedCtx*>(args);
    const BoardSpecConfig& spec = *ctx->spec;
    PreferenceState& pref = *ctx->pref;

    const int pwmChannel = 3;
    const int freq = 5 * 1000;
    const int resolution = 8;

    Adafruit_NeoPixel* strip = nullptr;

    if (spec.led.wifi_pin != -1) {
        pinMode(spec.led.wifi_pin, OUTPUT);
        digitalWrite(spec.led.wifi_pin, HIGH);
    }
    if (spec.led.pool_pin != -1) {
        pinMode(spec.led.pool_pin, OUTPUT);
        digitalWrite(spec.led.pool_pin, HIGH);
    }
    if (spec.led.sys_pin != -1) {
        if (spec.name == BOARD_NMAXE_NAME || spec.name == BOARD_NMAXE_GAMMA_NAME) {
            pinMode(spec.led.sys_pin, OUTPUT);
            ledcSetup(pwmChannel, freq, resolution);
            ledcAttachPin(spec.led.sys_pin, pwmChannel);
            ledcWrite(pwmChannel, 255); // off
        } else if (spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME) {
            strip = new Adafruit_NeoPixel(8, spec.led.sys_pin, NEO_GRB + NEO_KHZ800);
            while (!strip) {
                LOG_E("Failed to create NeoPixel instance for SYS LED");
                delay(1000);
            }
            strip->begin();
            strip->show();
            strip->setBrightness(100);
        } else {
            LOG_W("Unsupported board type for SYS LED control");
        }
    }

    uint64_t led_cnt = 0;
    const uint8_t dot = 20;
    while (true) {
        delay(10);
        if (spec.name == BOARD_NMAXE_NAME || spec.name == BOARD_NMAXE_GAMMA_NAME) {
            if (pref.led.sleep || !pref.led.enable) {
                if (spec.led.wifi_pin != -1) digitalWrite(spec.led.wifi_pin, HIGH);
                if (spec.led.pool_pin != -1) digitalWrite(spec.led.pool_pin, HIGH);
                if (spec.led.sys_pin != -1)  ledcWrite(pwmChannel, 255);
                continue;
            }
            if (ctx->ota_running && *ctx->ota_running) {
                if (spec.led.wifi_pin != -1) digitalWrite(spec.led.wifi_pin, HIGH);
                if (spec.led.pool_pin != -1) digitalWrite(spec.led.pool_pin, HIGH);
                if (spec.led.sys_pin != -1)  ledcWrite(pwmChannel, 255);
                continue;
            }

            uint8_t pattern_idx = (led_cnt % 201) / dot;
            if (pattern_idx > 0 && pattern_idx <= 10) {
                pattern_idx--;
                bool wifi_connected = (*ctx->wifi_status == WL_CONNECTED);
                bool pool_connected = ctx->stratum->is_subscribed();
                bool wifi_state = wifi_connected ? (pattern_idx == 0) : (pattern_idx % 2 == 1);
                digitalWrite(spec.led.wifi_pin, wifi_state ? LOW : HIGH);
                bool pool_state = pool_connected ? (pattern_idx == 0) : (pattern_idx % 2 == 1);
                digitalWrite(spec.led.pool_pin, pool_state ? LOW : HIGH);
            } else {
                if (spec.led.wifi_pin != -1) digitalWrite(spec.led.wifi_pin, HIGH);
                if (spec.led.pool_pin != -1) digitalWrite(spec.led.pool_pin, HIGH);
            }

            uint8_t speed = (ctx->status->hashrate._3m > 0) ? 1 : 20;
            ledcWrite(pwmChannel, (uint32_t)((1 + sin(speed * led_cnt / 100.0f)) * (1 << resolution - 1)));
            led_cnt++;
        } else if (spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME) {
            if (pref.led.sleep || !pref.led.enable) {
                for (int i = 0; i < strip->numPixels(); i++) strip->setPixelColor(i, strip->Color(0, 0, 0));
                strip->show();
                continue;
            }

            const uint8_t n = strip->numPixels();
            static bool ota_was_active = false;

            if (ctx->ota_running && *ctx->ota_running) {
                static int      last_ota_progress = -1;
                static uint32_t last_ota_show_ms  = 0;
                if (!ota_was_active) {
                    for (int i = 0; i < n; i++) strip->setPixelColor(i, strip->Color(0, 0, 0));
                    strip->show();
                    last_ota_progress = -1;
                    last_ota_show_ms  = 0;
                    ota_was_active    = true;
                    delay(10);
                }
                int progress = ctx->ota_progress ? *ctx->ota_progress : 0;
                progress = (progress < 0) ? 0 : (progress > 100 ? 100 : progress);
                uint32_t now_ms = millis();
                if (progress != last_ota_progress || (now_ms - last_ota_show_ms) >= 500) {
                    last_ota_progress = progress;
                    last_ota_show_ms  = now_ms;
                    int lit = (progress * n + 99) / 100;
                    uint8_t r = 0, g, b;
                    if (progress <= 50) {
                        float t = progress / 50.0f;
                        g = (uint8_t)(200 * t);
                        b = (uint8_t)(200 * (1.0f - t * 0.5f));
                    } else {
                        float t = (progress - 50) / 50.0f;
                        g = 200;
                        b = (uint8_t)(100 * (1.0f - t));
                    }
                    for (int i = 0; i < n; i++)
                        strip->setPixelColor(i, (i < lit) ? strip->Color(r, g, b) : strip->Color(0, 0, 0));
                    strip->show();
                }
                continue;
            }

            static bool post_ota_reset = false;
            if (ota_was_active && !(ctx->ota_running && *ctx->ota_running)) {
                ota_was_active = false;
                post_ota_reset = true;
                for (int i = 0; i < n; i++) strip->setPixelColor(i, strip->Color(0, 0, 0));
                strip->show();
            }

            const uint8_t  NUM_EFFECTS        = 6;
            const uint32_t EFFECT_DURATION_MS = 30000UL;
            static uint8_t  cur_effect   = 0;
            static uint32_t eff_start_ms = 0;
            static uint32_t tick         = 0;

            uint32_t now_ms = millis();
            if (eff_start_ms == 0 || post_ota_reset) {
                eff_start_ms   = now_ms;
                tick           = 0;
                cur_effect     = 0;
                post_ota_reset = false;
            }
            if (now_ms - eff_start_ms >= EFFECT_DURATION_MS) {
                cur_effect   = (cur_effect + 1) % NUM_EFFECTS;
                eff_start_ms = now_ms;
                tick         = 0;
                for (int i = 0; i < strip->numPixels(); i++) strip->setPixelColor(i, strip->Color(0, 0, 0));
                strip->show();
            }

            auto wheel = [&](uint8_t pos) -> uint32_t {
                pos = 255 - pos;
                if (pos < 85)  return strip->Color(255 - pos * 3, 0, pos * 3);
                if (pos < 170) { pos -= 85;  return strip->Color(0, pos * 3, 255 - pos * 3); }
                pos -= 170;    return strip->Color(pos * 3, 255 - pos * 3, 0);
            };

            switch (cur_effect) {
                case 0: {
                    uint8_t offset = (uint8_t)(tick * 2);
                    for (int i = 0; i < n; i++) strip->setPixelColor(i, wheel((i * 256 / n + offset) & 0xFF));
                    strip->show();
                    break;
                }
                case 1: {
                    float bri = (sinf(tick * 0.05f) + 1.0f) / 2.0f;
                    static uint8_t color_idx = 0;
                    static bool    was_dark  = false;
                    if (bri < 0.02f) {
                        if (!was_dark) { color_idx = (color_idx + 1) % 3; was_dark = true; }
                    } else was_dark = false;
                    uint8_t r = (color_idx == 0) ? (uint8_t)(255 * bri) : 0;
                    uint8_t g = (color_idx == 1) ? (uint8_t)(255 * bri) : 0;
                    uint8_t b = (color_idx == 2) ? (uint8_t)(255 * bri) : 0;
                    for (int i = 0; i < n; i++) strip->setPixelColor(i, strip->Color(r, g, b));
                    strip->show();
                    break;
                }
                case 2: {
                    for (int i = 0; i < n; i++) strip->setPixelColor(i, strip->Color(0, 0, 0));
                    uint8_t phase = (tick / 5) % 3;
                    uint32_t chase_color = wheel((uint8_t)(tick * 3));
                    for (int i = phase; i < n; i += 3) strip->setPixelColor(i, chase_color);
                    strip->show();
                    break;
                }
                case 3: {
                    const uint8_t METEOR_SIZE = 3;
                    const float   TRAIL_DECAY = 0.75f;
                    for (int i = 0; i < n; i++) {
                        uint32_t c = strip->getPixelColor(i);
                        uint8_t r2 = (uint8_t)(((c >> 16) & 0xFF) * TRAIL_DECAY);
                        uint8_t g2 = (uint8_t)(((c >>  8) & 0xFF) * TRAIL_DECAY);
                        uint8_t b2 = (uint8_t)(((c      ) & 0xFF) * TRAIL_DECAY);
                        strip->setPixelColor(i, strip->Color(r2, g2, b2));
                    }
                    int meteor_pos = (tick / 2) % (n + METEOR_SIZE);
                    for (int j = 0; j < METEOR_SIZE; j++) {
                        int pos = meteor_pos - j;
                        if (pos >= 0 && pos < n) {
                            uint8_t bright = (j == 0) ? 255 : (j == 1 ? 160 : 80);
                            strip->setPixelColor(pos, strip->Color(bright, bright, bright));
                        }
                    }
                    strip->show();
                    break;
                }
                case 4: {
                    for (int i = 0; i < n; i++) {
                        uint32_t c = strip->getPixelColor(i);
                        uint8_t r2 = (uint8_t)(((c >> 16) & 0xFF) * 0.80f);
                        uint8_t g2 = (uint8_t)(((c >>  8) & 0xFF) * 0.80f);
                        uint8_t b2 = (uint8_t)(((c      ) & 0xFF) * 0.80f);
                        strip->setPixelColor(i, strip->Color(r2, g2, b2));
                    }
                    if (tick % 3 == 0) {
                        int pos = random(0, n);
                        strip->setPixelColor(pos, wheel((uint8_t)random(0, 256)));
                    }
                    strip->show();
                    break;
                }
                case 5: {
                    static const uint32_t WIPE_COLORS[] = { 0xFF0000UL, 0x00FF00UL, 0x0000FFUL, 0xFF8800UL };
                    const uint8_t NUM_WIPE = 4;
                    uint32_t phase_len  = (uint32_t)n * 10;
                    uint32_t color_idx  = (tick / phase_len) % NUM_WIPE;
                    uint32_t phase_tick = tick % phase_len;
                    uint32_t col = WIPE_COLORS[color_idx];
                    uint8_t wr = (col >> 16) & 0xFF, wg = (col >> 8) & 0xFF, wb = (col) & 0xFF;
                    if (phase_tick < (uint32_t)n * 5) {
                        int fill_to = (int)(phase_tick / 5);
                        for (int i = 0; i < n; i++)
                            strip->setPixelColor(i, i <= fill_to ? strip->Color(wr, wg, wb) : strip->Color(0, 0, 0));
                    } else {
                        int clear_to = (int)((phase_tick - (uint32_t)n * 5) / 5);
                        for (int i = 0; i < n; i++)
                            strip->setPixelColor(i, i <= clear_to ? strip->Color(0, 0, 0) : strip->Color(wr, wg, wb));
                    }
                    strip->show();
                    break;
                }
                default: break;
            }
            tick++;
        }
    }
    LOG_I("led thread exit...");
    vTaskDelete(NULL);
}

void webserver_thread_entry(void* args) {
    WebCtx* ctx = static_cast<WebCtx*>(args);
    g_web = ctx;   // publish context for the free-function HTTP handlers

    bool spiffs_ok = file_system_init();
    if (!spiffs_ok) {
        LOG_W("SPIFFS mount failed — entering recovery mode (firmware-embedded recovery page)");
    }

    // wait for sta or ap ready
    xEventGroupWaitBits(ctx->init_evt, INIT_EVENT_WIFI_STA_CONNECTED | INIT_EVENT_WIFI_AP_READY, pdFALSE, pdFALSE, portMAX_DELAY);
    delay(100);

    if (!spiffs_ok) {
        webServer.on("/api/system/info",  HTTP_GET, get_system_info);
        // ── Recovery mode: wakeup endpoint so the swarm batch-upgrade flow (which calls
        // /api/wakeup before streaming the binary) does not fail immediately on devices
        // in recovery mode. Without this the wakeup GET falls through to the catch-all,
        // returns HTML, Angular httpClient fails to parse JSON → OTA never starts.
        webServer.on("/api/wakeup", HTTP_GET, [](AsyncWebServerRequest *request){
            AsyncWebServerResponse *r = request->beginResponse(200, "application/json", "{\"ok\":true}");
            r->addHeader("Access-Control-Allow-Origin", "*");
            request->send(r);
        });
        // ── Find me: also available in recovery mode so the swarm panel can locate
        // a device that is stuck in recovery (SPIFFS failed) by blinking its screen.
        webServer.on("/api/swarm/find", HTTP_POST, [ctx](AsyncWebServerRequest* request) {
            xEventGroupSetBits(ctx->sys_evt, SYS_EVENT_FIND_NEIGHBOR_TRIGGERED);
            AsyncWebServerResponse *r = request->beginResponse(200, "application/json", "{\"status\":\"ok\"}");
            r->addHeader("Access-Control-Allow-Origin", "*");
            request->send(r);
        });
        // ── Recovery mode: SPIFFS is unavailable ─────────────────────────────
        // Serve the firmware-embedded recovery page for every GET request so
        // the user can re-flash SPIFFS via a browser, then restart the device.
        webServer.on("/api/system/restart", HTTP_POST, post_restart);
        // OTA upload endpoints (both canonical and legacy aliases) so the
        // recovery page upload button works regardless of browser-cached URL.
        webServer.on("/api/update/firmware", HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler);
        webServer.on("/api/update/spiffs",   HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler);
        webServer.on("/api/system/OTA",      HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler);
        webServer.on("/api/system/OTAWWW",   HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler);
        // API probe endpoint: checkNewApiSupport() on other devices calls this to
        // decide which OTA URL to use. Returning a valid timeZone JSON lets the
        // swarm panel correctly select /api/update/firmware & /api/update/spiffs
        // instead of the legacy /api/system/OTA & /api/system/OTAWWW aliases.
        webServer.on("/api/setting/time", HTTP_GET, [](AsyncWebServerRequest *request){
            AsyncWebServerResponse *r = request->beginResponse(200, "application/json",
                "{\"timeZone\":\"UTC\",\"ntpServer\":\"pool.ntp.org\"}");
            r->addHeader("Access-Control-Allow-Origin", "*");
            request->send(r);
        });
        // for miner probe endpoint, swarm panel calls this to get hashrate and difficulty for swarm mining display , Keep this endpoint lightweight and fast.
        webServer.on("/probe", HTTP_GET, [ctx](AsyncWebServerRequest* request) {
            // Snapshot double fields once: double is non-atomic on 32-bit ESP32,
            // reading directly in printf could yield a torn value.
            double hr  = ctx->status->hashrate._3m;
            double sbd = ctx->status->diff.best_session;
            double ebd = ctx->status->diff.best_ever;
            if (!isfinite(hr)  || hr  < 0.0) hr  = 0.0;
            if (!isfinite(sbd) || sbd < 0.0) sbd = 0.0;
            if (!isfinite(ebd) || ebd < 0.0) ebd = 0.0;
            AsyncResponseStream* resp = request->beginResponseStream("application/json");
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->print("{");
            resp->printf("\"model\":\"%s\",",    ctx->spec->name.c_str());  // board model name
            resp->printf("\"hostname\":\"%s\",",    ctx->wifi_cfg->hostname.c_str()); // hostname
            resp->printf("\"ver\":\"%s\",", ctx->fw_version.c_str()); // firmware version
            resp->printf("\"sw\":%d,",           ctx->spec->tft.width);   // TFT width from board spec
            resp->printf("\"sh\":%d,",           ctx->spec->tft.height);  // TFT height from board spec
            resp->printf("\"hr\":%.0f,",       hr);  // hashrate in H/s, 3m average
            resp->printf("\"sbd\":%.0f,",      sbd); // best session difficulty
            resp->printf("\"ebd\":%.0f,",      ebd); // best ever difficulty
            resp->printf("\"ut\":%d",      ctx->status->uptime_session);   // uptime in seconds for current session
            resp->print("}");
            request->send(resp);
        });
        // Catch-all GET: serve recovery page from flash (no SPIFFS needed).
        webServer.on("/*", HTTP_GET, [](AsyncWebServerRequest *request){
            AsyncWebServerResponse *r = request->beginResponse(200, "text/html", recovery_page);
            r->addHeader("Access-Control-Allow-Origin", "*");
            request->send(r);
        });
        // CORS preflight handler — CRITICAL for swarm cross-origin upgrades.
        webServer.on("/*", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
            AsyncWebServerResponse *r = request->beginResponse(200);
            r->addHeader("Access-Control-Allow-Origin",  "*");
            r->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE");
            r->addHeader("Access-Control-Allow-Headers", "Accept,Content-Type");
            request->send(r);
        });
        webServer.begin();
        while (true) { delay(250); }
    }

    // Register AsyncWebSocket handler on webServer (same port 80, path /ws)
    webSocket.onEvent(webSocketEvent);
    webServer.addHandler(&webSocket);
    webServer.on("/api/system/info", HTTP_GET, get_system_info);
    webServer.on("/api/system/restart",HTTP_POST, post_restart);
    webServer.on("/api/system/clearhits", HTTP_POST, post_reset_block_hits);
    webServer.on("/api/mining/state", HTTP_PATCH, [](AsyncWebServerRequest *request){}, NULL, patch_mining_state);
    // ── Reboot history (planned vs crash, persisted across boots) ───────────
    webServer.on("/api/reboot/last", HTTP_GET,    get_reboot_last);
    webServer.on("/api/reboot/list", HTTP_GET,    get_reboot_list);
    webServer.on("/api/reboot/list", HTTP_DELETE, delete_reboot_list);
    // ── Coredump (post-mortem summary in flash, cleared on demand) ─────────
    webServer.on("/api/coredump/info", HTTP_GET,    get_coredump_info);
    webServer.on("/api/coredump",      HTTP_DELETE, delete_coredump);
    // ── Wakeup: any caller (local or cross-origin swarm panel) can wake this device's screensaver.
    webServer.on("/api/wakeup", HTTP_GET, [ctx](AsyncWebServerRequest *request){
        xEventGroupClearBits(ctx->sys_evt, SYS_EVENT_SCREEN_SAVER_TRIGGERED);
        AsyncWebServerResponse *r = request->beginResponse(200, "application/json", "{\"ok\":true}");
        r->addHeader("Access-Control-Allow-Origin", "*");
        request->send(r);
    });
    // ── Dashboard data endpoints ──────────────────────────────────────────────
    webServer.on("/api/dashboard/hr/dist",        HTTP_GET, get_hr_distribution);
    webServer.on("/api/dashboard/gauge/limits",   HTTP_GET, get_gauge_limits);
    webServer.on("/api/dashboard/chart/history",  HTTP_GET, get_status_history);
    webServer.on("/api/dashboard/chart/realtime", HTTP_GET, get_status_realtime);
    webServer.on("/api/dashboard/luck/history",   HTTP_GET, get_lucky_history);
    // ── Swarm endpoints ───────────────────────────────────────────────────────
    webServer.on("/api/swarm/scan", HTTP_POST, [ctx](AsyncWebServerRequest* request) {
        if (ctx->neighbor->scan_required){
            xSemaphoreGive(ctx->neighbor->scan_required);
            LOG_I("Triggered alive IP scan by swarm request");
        }
        AsyncWebServerResponse *r = request->beginResponse(200, "application/json", "{\"status\":\"ok\"}");
        r->addHeader("Access-Control-Allow-Origin", "*");
        request->send(r);
    }); // trigger alive_ip_scan_thread immediately
    // ── Find me: blink screen to help user locate a specific device ───────────
    webServer.on("/api/swarm/find", HTTP_POST, [ctx](AsyncWebServerRequest* request) {
        xEventGroupSetBits(ctx->sys_evt, SYS_EVENT_FIND_NEIGHBOR_TRIGGERED);
        AsyncWebServerResponse *r = request->beginResponse(200, "application/json", "{\"status\":\"ok\"}");
        r->addHeader("Access-Control-Allow-Origin", "*");
        request->send(r);
    });
    // ── Logging and echo endpoints ───────────────────────────────────────────────
    webServer.on("/api/log", HTTP_GET, echo_handler);
    // ── OTA update endpoints ──────────────────────────────────────────────────
    webServer.on("/api/update/progress", HTTP_GET,  get_ota_progress);                                          // progress poll
    webServer.on("/api/update/last-result", HTTP_GET, get_ota_last_result);                                     // last OTA result snapshot
    webServer.on("/api/update/firmware", HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler); // canonical
    webServer.on("/api/update/spiffs",   HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler); // canonical
    webServer.on("/api/system/OTA",      HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler); // compat alias
    webServer.on("/api/system/OTAWWW",   HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler); // compat alias
    webServer.on("/api/update/screensaver",HTTP_POST,  [](AsyncWebServerRequest *request){}, file_upload_handler);
    // ── Theme endpoints (OPTIONS covered by wildcard handler below) ───────────
    webServer.on("/api/theme", HTTP_GET,  get_theme_handler);
    webServer.on("/api/theme", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, post_theme_handler);
    // ── Settings per-card endpoints ───────────────────────────────────────────
    webServer.on("/api/setting/network",    HTTP_GET,   get_setting_network);
    webServer.on("/api/setting/network",    HTTP_PATCH, [](AsyncWebServerRequest *request){}, NULL, patch_setting_network);
    webServer.on("/api/setting/time",       HTTP_GET,   get_setting_time);
    webServer.on("/api/setting/time",       HTTP_PATCH, [](AsyncWebServerRequest *request){}, NULL, patch_setting_time);
    webServer.on("/api/setting/mining",     HTTP_GET,   get_setting_mining);
    webServer.on("/api/setting/mining",     HTTP_PATCH, [](AsyncWebServerRequest *request){}, NULL, patch_setting_mining);
    webServer.on("/api/setting/market",     HTTP_GET,   get_setting_market);
    webServer.on("/api/setting/market",     HTTP_PATCH, [](AsyncWebServerRequest *request){}, NULL, patch_setting_market);
    webServer.on("/api/setting/preference", HTTP_GET,   get_setting_preference);
    webServer.on("/api/setting/preference", HTTP_PATCH, [](AsyncWebServerRequest *request){}, NULL, patch_setting_preference);
    // ── Benchmark endpoints ────────────────────────────────────────────────────
    webServer.on("/api/benchmark",         HTTP_GET,    get_benchmark);
    webServer.on("/api/benchmark",         HTTP_PATCH,  [](AsyncWebServerRequest *request){}, NULL, patch_benchmark);
    webServer.on("/api/benchmark/start",   HTTP_POST,   [](AsyncWebServerRequest *request){}, NULL, post_benchmark_start);
    webServer.on("/api/benchmark/stop",    HTTP_POST,   post_benchmark_stop);
    webServer.on("/api/benchmark/results", HTTP_DELETE, delete_benchmark_results);
    webServer.on("/api/benchmark/reset",   HTTP_POST,   post_benchmark_reset);
    webServer.on("/api/benchmark/apply",   HTTP_POST,   [](AsyncWebServerRequest *request){}, NULL, post_benchmark_apply);

    // for miner probe endpoint, swarm panel calls this to get hashrate and difficulty for swarm mining display , Keep this endpoint lightweight and fast.
    webServer.on("/probe", HTTP_GET, [ctx](AsyncWebServerRequest* request) {
        // Return 503 during OTA — avoid competing with the flash write for TCP/lwIP resources.
        if (ctx->ota->running) {
            request->send(503, "text/plain", "OTA in progress");
            return;
        }
        double hr  = ctx->status->hashrate._3m;
        double sbd = ctx->status->diff.best_session;
        double ebd = ctx->status->diff.best_ever;
        if (!isfinite(hr)  || hr  < 0.0) hr  = 0.0;
        if (!isfinite(sbd) || sbd < 0.0) sbd = 0.0;
        if (!isfinite(ebd) || ebd < 0.0) ebd = 0.0;
        AsyncResponseStream* resp = request->beginResponseStream("application/json");
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->print("{");
        resp->printf("\"model\":\"%s\",",    ctx->spec->name.c_str());  // board model name
        resp->printf("\"hostname\":\"%s\",",    ctx->wifi_cfg->hostname.c_str()); // hostname
        resp->printf("\"ver\":\"%s\",", ctx->fw_version.c_str()); // firmware version
        resp->printf("\"sw\":%d,",           ctx->spec->tft.width);   // TFT width from board spec
        resp->printf("\"sh\":%d,",           ctx->spec->tft.height);  // TFT height from board spec
        resp->printf("\"hr\":%.0f,",       hr);  // hashrate in H/s, 3m average
        resp->printf("\"sbd\":%.0f,",      sbd); // best session difficulty
        resp->printf("\"ebd\":%.0f,",      ebd); // best ever difficulty
        resp->printf("\"ut\":%d",      ctx->status->uptime_session);   // uptime in seconds for current session
        resp->print("}");
        request->send(resp);
    });
    // alive ip return from miner, just alive ip, maybe phone, pc, or miner itself, for miner probe endpoint, AxeOS probe one by one .
    webServer.on("/alive", HTTP_GET, [ctx](AsyncWebServerRequest* request) {
        // Return 503 during OTA — avoid competing with the flash write for TCP/lwIP resources.
        if (ctx->ota->running) {
            request->send(503, "text/plain", "OTA in progress");
            return;
        }
        String self_ip = WiFi.localIP().toString();
        AsyncResponseStream* resp = request->beginResponseStream("application/json");
        resp->addHeader("Access-Control-Allow-Origin", "*");
        bool scanning   = ctx->neighbor->is_scanning;
        uint16_t prog   = ctx->neighbor->scan_progress;
        uint32_t lms    = 0;
        resp->printf("{\"self\":\"%s\",\"scanning\":%s,\"progress\":%u,\"total\":254",
            self_ip.c_str(), scanning ? "true" : "false", (unsigned)prog);
        if (xSemaphoreTake(ctx->neighbor->mutex, pdMS_TO_TICKS(300)) == pdTRUE) {
            lms = ctx->neighbor->last_scan_ms;
            xSemaphoreGive(ctx->neighbor->mutex);
        }
        uint32_t next_in = 0;
        if (!scanning && lms > 0) {
            uint32_t elapsed_s = (millis() - lms) / 1000;
            next_in = (elapsed_s < 300) ? (300 - elapsed_s) : 0;
        }
        resp->printf(",\"next_scan_in\":%u", (unsigned)next_in);
        resp->printf(",\"ips\":[\"%s\"", self_ip.c_str());
        if (xSemaphoreTake(ctx->neighbor->mutex, pdMS_TO_TICKS(300)) == pdTRUE) {
            auto neighbor_ip_to_cstr = [](neighbor_ip_t address, char *buffer, size_t buffer_size) {
                snprintf(buffer, buffer_size, "%u.%u.%u.%u",
                         (unsigned)((address >> 24) & 0xFF),
                         (unsigned)((address >> 16) & 0xFF),
                         (unsigned)((address >> 8) & 0xFF),
                         (unsigned)(address & 0xFF));
            };
            char ip_buf[16];
            for (const auto& ip : ctx->neighbor->alive_ips) {
                neighbor_ip_to_cstr(ip, ip_buf, sizeof(ip_buf));
                resp->printf(",\"%s\"", ip_buf);
            }
            xSemaphoreGive(ctx->neighbor->mutex);
        }
        resp->print("]}");
        request->send(resp);
    });
    webServer.on("/api-doc", HTTP_GET, [](AsyncWebServerRequest *request){
        request->redirect("/api-doc.html");
    });
    webServer.on("/*", HTTP_GET, [ctx](AsyncWebServerRequest *request){
        // Wake screensaver when the user loads or refreshes any page on this device.
        if(xEventGroupGetBits(ctx->sys_evt) & SYS_EVENT_SCREEN_SAVER_TRIGGERED) {
            xEventGroupClearBits(ctx->sys_evt, SYS_EVENT_SCREEN_SAVER_TRIGGERED);
        }
        rest_common_get_handler(request);
    });
    webServer.on("/*", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(200);
        response->addHeader("Access-Control-Allow-Origin", "*");
        response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE");
        response->addHeader("Access-Control-Allow-Headers", "Accept,Content-Type");
        request->send(response);
    });
    webServer.begin();
    // cleanupClients() must be called periodically so AsyncWebSocket can detect
    // dropped connections and fire WS_EVT_DISCONNECT for stale clients.
    while (true) {
        delay(250);
        webSocket.cleanupClients();

        // OTA stall watchdog: runs independently of the upload callback.
        if (ctx->ota->running && ctx->ota->last_progress_ms != 0) {
            if (millis() - ctx->ota->last_progress_ms > 60*1000UL) {
                LOG_E("[OTA watchdog] upload stalled >60s at %d%%, triggering reboot", ctx->ota->progress);
                reboot_intent_set(REBOOT_INTENT_OTA_STALL,
                                  "upload stalled at %d%% for >60s",
                                  ctx->ota->progress);
                xSemaphoreGive(ctx->reboot_xsem);
            }
        }
    }
}

// ── Scan: LAN /24 ICMP alive-IP discovery ───────────────────────────────────
//    Mirrors legacy alive_ip_scan_thread_entry; board_sal_t* -> SwarmCtx* (DI).
void scan_thread_entry(void* args) {
    SwarmCtx* ctx = static_cast<SwarmCtx*>(args);
    NeighborState& nbr = *ctx->neighbor;

    constexpr uint32_t PING_TIMEOUT_MS = 500; // wait 500ms per IP

    struct PingPkt {
        struct icmp_echo_hdr hdr;
        uint8_t              data[32];
    };

    auto neighbor_ip_from_octets = [](uint8_t o0, uint8_t o1, uint8_t o2, uint8_t o3) -> neighbor_ip_t {
        return ((neighbor_ip_t)o0 << 24) | ((neighbor_ip_t)o1 << 16) | ((neighbor_ip_t)o2 << 8) | (neighbor_ip_t)o3;
    };

    // Avoid lwip resource contention during startup; wait until miner is ready.
    xEventGroupWaitBits(ctx->init_evt, INIT_EVENT_MINER_READY, pdFALSE, pdTRUE, portMAX_DELAY);
    delay(2000);

    // ── ARP pre-resolve to avoid lwIP assert(q==NULL) on full ARP table ──
    auto arp_resolve = [](ip4_addr_t* ipaddr, uint32_t timeout_ms) -> bool {
        struct netif* nif = netif_default;
        if (!nif) return false;
        LOCK_TCPIP_CORE();
        etharp_request(nif, ipaddr);
        UNLOCK_TCPIP_CORE();
        uint32_t t0 = millis();
        while ((millis() - t0) < timeout_ms) {
            struct eth_addr* eth_ret = nullptr;
            const ip4_addr_t* ip_ret = nullptr;
            LOCK_TCPIP_CORE();
            int8_t idx = etharp_find_addr(nif, ipaddr, &eth_ret, &ip_ret);
            UNLOCK_TCPIP_CORE();
            if (idx >= 0) return true;
            delay(20);
        }
        return false;
    };

    auto ping_one = [&](int sock, uint8_t o0, uint8_t o1, uint8_t o2, int last, uint16_t seq) -> bool {
        ip4_addr_t target_ip;
        IP4_ADDR(&target_ip, o0, o1, o2, last);
        if (!arp_resolve(&target_ip, PING_TIMEOUT_MS)) {
            LOG_D("(scan) %u.%u.%u.%u ARP timeout, skipping", o0, o1, o2, last);
            return false;
        }

        PingPkt pkt = {};
        ICMPH_TYPE_SET(&pkt.hdr, ICMP_ECHO);
        ICMPH_CODE_SET(&pkt.hdr, 0);
        pkt.hdr.id     = htons(0xBEEF);
        pkt.hdr.seqno  = htons(seq);
        pkt.hdr.chksum = inet_chksum(&pkt, sizeof(pkt));

        struct sockaddr_in dest = {};
        dest.sin_family      = AF_INET;
        dest.sin_addr.s_addr = htonl(
            ((uint32_t)o0 << 24) | ((uint32_t)o1 << 16) |
            ((uint32_t)o2 <<  8) |  (uint32_t)last);

        uint32_t t0 = millis();
        if (::sendto(sock, &pkt, sizeof(pkt), 0, (struct sockaddr*)&dest, sizeof(dest)) < 0) {
            LOG_D("(scan) %u.%u.%u.%u sendto failed: errno=%d (%s)", o0, o1, o2, last, errno, strerror(errno));
            return false;
        }

        uint8_t rbuf[64];
        struct sockaddr_in from = {};
        socklen_t fromlen = sizeof(from);
        int n = ::recvfrom(sock, rbuf, sizeof(rbuf), 0, (struct sockaddr*)&from, &fromlen);
        if (n > 0) {
            struct icmp_echo_hdr* reply = (struct icmp_echo_hdr*)(rbuf + 20);
            if (ICMPH_TYPE(reply) == ICMP_ER) {
                LOG_D("(scan) %u.%u.%u.%u alive, ping %lums", o0, o1, o2, last, (unsigned long)(millis() - t0));
                return true;
            }
            LOG_D("(scan) %u.%u.%u.%u unexpected ICMP type=%u code=%u", o0, o1, o2, last, ICMPH_TYPE(reply), ICMPH_CODE(reply));
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOG_D("(scan) %u.%u.%u.%u no reply (timeout %ums)", o0, o1, o2, last, PING_TIMEOUT_MS);
            } else {
                LOG_D("(scan) %u.%u.%u.%u recvfrom error: errno=%d (%s)", o0, o1, o2, last, errno, strerror(errno));
            }
        }
        return false;
    };

    while (true) {
        delay(1000);
        if (WiFi.status() != WL_CONNECTED) continue;
        if (*ctx->ota_running)             continue;
        if (xEventGroupGetBits(ctx->sys_evt) & SYS_EVENT_SCREEN_SAVER_TRIGGERED) {
            LOG_D("(scan) screensaver active, skipping alive IP scan to yield resources");
            continue;
        }

        int sock = ::socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        if (sock < 0) {
            LOG_E("(scan) raw socket create failed: %d", errno);
            delay(1000 * 10);
            continue;
        }
        struct timeval tv = { 0, (long)(PING_TIMEOUT_MS * 1000L) };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        uint32_t scan_start = millis();

        IPAddress local = WiFi.localIP();
        uint8_t o0 = local[0], o1 = local[1], o2 = local[2], self_last = local[3];
        LOG_D("(scan) start ping %u.%u.%u.0/24, generation=%u", o0, o1, o2, nbr.scan_generation);

        neighbor_ip_vector_t found;
        found.reserve(16);

        nbr.is_scanning   = true;
        nbr.scan_progress = 0;

        uint16_t seq = 0, MAX_SCAN = 254;
        for (int last = 1; last <= MAX_SCAN; last++) {
            if (last == self_last) continue;

            if (*ctx->ota_running) {
                LOG_D("(scan) OTA started mid-scan, aborting at %u.%u.%u.%u", o0, o1, o2, last);
                break;
            }
            if (xEventGroupGetBits(ctx->sys_evt) & SYS_EVENT_SCREEN_SAVER_TRIGGERED) {
                LOG_D("(scan) screensaver activated mid-scan, aborting at %u.%u.%u.%u", o0, o1, o2, last);
                break;
            }

            nbr.scan_progress = (uint16_t)last;

            if (xSemaphoreTake(nbr.scan_required, 0) == pdTRUE) {
                if (xSemaphoreTake(nbr.mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                    nbr.alive_ips.clear();
                    xSemaphoreGive(nbr.mutex);
                    LOG_D("(scan) page refresh: reset scan progress, alive_ips cleared (gen unchanged)");
                }
                last = 1;
                seq  = 0;
                found.clear();
                nbr.scan_progress = 0;
            }

            if (ping_one(sock, o0, o1, o2, last, ++seq)) {
                found.push_back(neighbor_ip_from_octets(o0, o1, o2, (uint8_t)last));
            }

            if ((seq % 10 == 0) || (last == MAX_SCAN)) {
                bool is_last = (last == MAX_SCAN);
                if (xSemaphoreTake(nbr.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    if (is_last) nbr.alive_ips = std::move(found);
                    else         nbr.alive_ips = found;
                    nbr.last_scan_ms = millis();
                    xSemaphoreGive(nbr.mutex);
                }
                LOG_D("(scan) progress to %u.%u.%u.%u, found %u alive hosts", o0, o1, o2, last, (unsigned)nbr.alive_ips.size());
                delay(1100);
            }
            delay(5);
        }
        ::close(sock);

        uint32_t elapsed = (millis() - scan_start) / 1000;
        LOG_D("(scan) done in %lus, %u alive on %u.%u.%u.0/24", (uint32_t)elapsed, (uint16_t)nbr.alive_ips.size(), o0, o1, o2);
        nbr.is_scanning   = false;
        nbr.scan_progress = 254;
        if (xSemaphoreTake(nbr.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            nbr.scan_generation++;
            nbr.last_scan_ms = millis();
            xSemaphoreGive(nbr.mutex);
        }
        xSemaphoreTake(nbr.scan_required, 5 * 60 * 1000);
    }
}

// ── Swarm: probe / aggregate neighbor miner stats via /probe + /alive gossip ─
//    Mirrors legacy swarm_thread_entry; board_sal_t* -> SwarmCtx* (DI).
void swarm_thread_entry(void* args) {
    SwarmCtx* sctx = static_cast<SwarmCtx*>(args);
    SwarmState&    ctx = *sctx->swarm;
    NeighborState& nbr = *sctx->neighbor;
    MinerStatus&   st  = *sctx->status;

    auto neighbor_ip_from_octets = [](uint8_t o0, uint8_t o1, uint8_t o2, uint8_t o3) -> neighbor_ip_t {
        return ((neighbor_ip_t)o0 << 24) | ((neighbor_ip_t)o1 << 16) | ((neighbor_ip_t)o2 << 8) | (neighbor_ip_t)o3;
    };
    auto neighbor_ip_from_ipaddress = [&neighbor_ip_from_octets](const IPAddress& address) -> neighbor_ip_t {
        return neighbor_ip_from_octets(address[0], address[1], address[2], address[3]);
    };
    auto neighbor_ip_to_cstr = [](neighbor_ip_t address, char* buffer, size_t buffer_size) {
        snprintf(buffer, buffer_size, "%u.%u.%u.%u",
                 (unsigned)((address >> 24) & 0xFF), (unsigned)((address >> 16) & 0xFF),
                 (unsigned)((address >> 8) & 0xFF), (unsigned)(address & 0xFF));
    };
    auto neighbor_ip_from_string = [&neighbor_ip_from_ipaddress](const char* text, neighbor_ip_t* address) -> bool {
        if (!text || !address) return false;
        IPAddress parsed;
        if (!parsed.fromString(text)) return false;
        *address = neighbor_ip_from_ipaddress(parsed);
        return true;
    };

    while (true) {
        delay(30 * 1000);

        if (WiFi.status() != WL_CONNECTED) continue;
        if (*sctx->ota_running)            continue;
        if (xEventGroupGetBits(sctx->sys_evt) & SYS_EVENT_SCREEN_SAVER_TRIGGERED) {
            LOG_D("(swarm) screensaver active, skipping swarm probe to yield resources");
            continue;
        }

        // ── Snapshot current scan generation and alive list ──
        neighbor_ip_vector_t alive;
        uint32_t cur_gen = 0;
        if (xSemaphoreTake(nbr.mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            alive   = nbr.alive_ips;
            cur_gen = nbr.scan_generation;
            xSemaphoreGive(nbr.mutex);
        } else {
            LOG_W("(swarm) WARNING: failed to acquire nbr.mutex in 200ms");
        }

        const uint8_t MAX_PROBE_FAIL = 3;
        if (cur_gen != ctx.last_scan_gen) {
            ctx.last_scan_gen = cur_gen;
            if (xSemaphoreTake(ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                ctx.probe_blacklist.clear();
                xSemaphoreGive(ctx.mutex);
            }
            LOG_W("(swarm) new scan gen=%u, blacklist reset; confirmed=%u gossip=%u (kept)",
                  cur_gen, (uint32_t)ctx.confirmed_ips.size(), (uint32_t)ctx.gossip_union.size());
        }

        // ── Build probe targets: confirmed ∪ local_alive ∪ gossip ──
        const neighbor_ip_t selfIP = neighbor_ip_from_ipaddress(WiFi.localIP());
        neighbor_ip_vector_t targets;
        neighbor_ip_set_t    seen;
        auto add_target = [&](neighbor_ip_t ip) {
            if (ip == selfIP) return;
            if (seen.count(ip)) return;
            bool bl = false;
            if (xSemaphoreTake(ctx.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                bl = ctx.probe_blacklist.count(ip) > 0;
                xSemaphoreGive(ctx.mutex);
            }
            if (bl) return;
            seen.insert(ip);
            targets.push_back(ip);
        };
        for (const auto& ip : ctx.confirmed_ips) add_target(ip);
        for (const auto& ip : alive)             add_target(ip);
        for (const auto& ip : ctx.gossip_union)  add_target(ip);

        uint32_t workers         = 0;
        double   total_hr        = 0.0;
        double   best_session_bd = 0.0;
        double   best_ever_bd    = 0.0;

        LOG_D("(swarm) targets(%u): confirmed=%u alive=%u gossip=%u",
              (uint32_t)targets.size(), (uint32_t)ctx.confirmed_ips.size(),
              (uint32_t)alive.size(), (uint32_t)ctx.gossip_union.size());

        for (const auto& ip : targets) {
            uint32_t t0 = millis();
            char ip_buf[16];
            neighbor_ip_to_cstr(ip, ip_buf, sizeof(ip_buf));
            LOG_D("(swarm) >>> probing %s", ip_buf);

            WiFiClient wclient;
            int code = -1;
            String body;
            if (wclient.connect(ip_buf, 80, 1000)) {
                wclient.print("GET /probe HTTP/1.0\r\nHost: ");
                wclient.print(ip_buf);
                wclient.print("\r\nConnection: close\r\n\r\n");
                String resp;
                resp.reserve(512);
                uint32_t read_dl = millis() + 1500;
                while ((wclient.connected() || wclient.available()) && millis() < read_dl) {
                    while (wclient.available()) {
                        resp += (char)wclient.read();
                        if (resp.length() > 1024) goto probe_read_done;
                    }
                    delay(1);
                }
                probe_read_done:
                wclient.stop();
                int sp = resp.indexOf(' ');
                if (sp >= 0 && resp.startsWith("HTTP/") && (int)resp.length() > sp + 3) {
                    code = resp.substring(sp + 1, sp + 4).toInt();
                }
                int bi = resp.indexOf("\r\n\r\n");
                if (bi >= 0) body = resp.substring(bi + 4);
            }

            LOG_D("(swarm) <<< probe %s => code=%d (%lums)", ip_buf, code, (unsigned long)(millis() - t0));

            if (code == 200) {
                StaticJsonDocument<256> doc;
                LOG_D("(swarm) %s: %s", ip_buf, body.c_str());
                if (!deserializeJson(doc, body)) {
                    bool isNM = doc.containsKey("hr") && doc.containsKey("ver");
                    if (isNM) {
                        workers++;
                        double hr  = doc["hr"].as<double>();
                        double sbd = doc["sbd"].as<double>();
                        double ebd = doc["ebd"].as<double>();
                        if (!isfinite(hr) || hr < 0.0 || hr > 1e14) {
                            LOG_W("(swarm) %s INVALID hr=%.0f, skipped (torn read?)", ip_buf, hr);
                        } else {
                            total_hr += hr;
                        }
                        if (!isfinite(sbd) || sbd < 0.0) sbd = 0.0;
                        if (!isfinite(ebd) || ebd < 0.0) ebd = 0.0;
                        if (sbd > best_session_bd) best_session_bd = sbd;
                        if (ebd > best_ever_bd)    best_ever_bd    = ebd;
                        ctx.confirmed_ips.insert(ip);
                        ctx.probe_fail_cnt[ip] = 0;
                        LOG_D("(swarm) %s NM device hr=%.0f sbd=%.0f ebd=%.0f", ip_buf, hr, sbd, ebd);
                    } else {
                        if (xSemaphoreTake(ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                            ctx.probe_blacklist.insert(ip);
                            xSemaphoreGive(ctx.mutex);
                        }
                        ctx.confirmed_ips.erase(ip);
                        ctx.probe_fail_cnt.erase(ip);
                        LOG_D("(swarm) %s not NM device, blacklisted", ip_buf);
                    }
                }
            } else {
                bool is_confirmed = ctx.confirmed_ips.count(ip) > 0;
                if (is_confirmed) {
                    uint8_t& fc = ctx.probe_fail_cnt[ip];
                    if (fc < 0xFF) fc++;
                    if (fc >= MAX_PROBE_FAIL) {
                        ctx.confirmed_ips.erase(ip);
                        ctx.probe_fail_cnt.erase(ip);
                        LOG_W("(swarm) %s removed from confirmed after %u failures", ip_buf, MAX_PROBE_FAIL);
                    } else {
                        LOG_D("(swarm) probe %s => %d, fail_cnt=%u (kept)", ip_buf, code, fc);
                    }
                } else {
                    if (xSemaphoreTake(ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        ctx.probe_blacklist.insert(ip);
                        xSemaphoreGive(ctx.mutex);
                    }
                    LOG_D("(swarm) probe %s => %d, blacklisted", ip_buf, code);
                }
            }
            delay(50);
        }

        // ── Gossip: ask each confirmed NM peer for its /alive list ──
        size_t gossip_added = 0;
        BasicJsonDocument<PsramJsonAllocator> gdoc(2048);
        for (const auto& ip : ctx.confirmed_ips) {
            if (*sctx->ota_running) break;
            if (xEventGroupGetBits(sctx->sys_evt) & SYS_EVENT_SCREEN_SAVER_TRIGGERED) break;

            WiFiClient wclient;
            char ip_buf[16];
            neighbor_ip_to_cstr(ip, ip_buf, sizeof(ip_buf));
            if (!wclient.connect(ip_buf, 80, 1000)) continue;
            wclient.print("GET /alive?src=swarm HTTP/1.0\r\nHost: ");
            wclient.print(ip_buf);
            wclient.print("\r\nConnection: close\r\n\r\n");
            String resp;
            resp.reserve(1024);
            uint32_t read_dl = millis() + 1500;
            while ((wclient.connected() || wclient.available()) && millis() < read_dl) {
                while (wclient.available()) {
                    resp += (char)wclient.read();
                    if (resp.length() > 4096) goto gossip_read_done;
                }
                delay(1);
            }
            gossip_read_done:
            wclient.stop();
            int bi = resp.indexOf("\r\n\r\n");
            if (bi < 0) continue;
            String gbody = resp.substring(bi + 4);
            gdoc.clear();
            if (deserializeJson(gdoc, gbody)) continue;
            JsonArray arr = gdoc["ips"].as<JsonArray>();
            for (JsonVariant v : arr) {
                neighbor_ip_t candidate;
                if (!neighbor_ip_from_string(v.as<const char*>(), &candidate)) continue;
                if (candidate == selfIP) continue;
                if (ctx.confirmed_ips.count(candidate)) continue;
                bool bl = false;
                if (xSemaphoreTake(ctx.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    bl = ctx.probe_blacklist.count(candidate) > 0;
                    xSemaphoreGive(ctx.mutex);
                }
                if (bl) continue;
                if (ctx.gossip_union.insert(candidate).second) gossip_added++;
            }
            delay(50);
        }
        if (gossip_added) {
            LOG_W("(swarm) gossip added %u new IPs from %u peers, union=%u",
                  (uint32_t)gossip_added, (uint32_t)ctx.confirmed_ips.size(), (uint32_t)ctx.gossip_union.size());
        }

        // ── Write aggregated results ──
        double self_hr = st.hashrate._3m;
        if (!isfinite(self_hr) || self_hr < 0.0 || self_hr > 1e14) {
            LOG_W("(swarm) INVALID self hr=%.0f, using 0 (torn read?)", self_hr);
            self_hr = 0.0;
        }
        if (xSemaphoreTake(ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            ctx.total_workers   = (uint32_t)ctx.confirmed_ips.size() + 1;
            ctx.total_hr        = total_hr + self_hr;
            ctx.best_session_bd = (best_session_bd > st.diff.best_session) ? best_session_bd : st.diff.best_session;
            ctx.best_ever_bd    = (best_ever_bd > st.diff.best_ever) ? best_ever_bd : st.diff.best_ever;
            xSemaphoreGive(ctx.mutex);
        }
        LOG_D("(swarm) gen=%u probed=%u(+self) workers=%u hr=%sH/s sbd=%s ebd=%s",
              ctx.last_scan_gen, (uint32_t)targets.size(), ctx.total_workers,
              formatNumber(ctx.total_hr, 3).c_str(),
              formatNumber(ctx.best_session_bd, 3).c_str(),
              formatNumber(ctx.best_ever_bd, 3).c_str());
    }
}

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

    // Wait for WiFi, then print all available USDT trading pairs once at startup.
    while (*ctx->wifi_status != WL_CONNECTED) delay(1000);
    LOG_I("Fetching available USDT trading pairs from Binance...");
    market->fetch_available_usdt_pairs();

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
            bool fetched = false;
            for (uint8_t attempt = 1; attempt <= MARKET_MAX_RETRIES; attempt++) {
                if (market->refresh_main_pair(ctx->coin_price)) {
                    fetched = true;
                    break;
                }
                LOG_D("Market data fetch failed (attempt %d/%d) for symbol [%sUSDT]",
                      attempt, MARKET_MAX_RETRIES, ctx->coin_price.c_str());
                if (attempt < MARKET_MAX_RETRIES) {
                    delay(MARKET_RETRY_DELAY_MS);
                }
            }
            if (!fetched) {
                LOG_E("Market data fetch failed after %d attempts. Please verify that the Binance API is accessible in your country.", MARKET_MAX_RETRIES);
            }

            // Fetch watchlist pairs, then sort by price descending for display
            market->refresh_watchlist(ctx->coin_watchlist);
            market->sort_watchlist_by_price();
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

void lvgl_thread_entry(void* args) {
    (void)args;
    LOG_D("(lvgl) placeholder");
    while (true) { delay(5); }
}

void ui_thread_entry(void* args) {
    (void)args;
    LOG_D("(ui) placeholder");
    while (true) { delay(100); }
}

// ── Benchmark: auto frequency/vcore sweep (NVS-driven, reboot per round) ────
//    Mirrors legacy benchmark_thread_entry; board_sal_t* -> BenchmarkCtx* (DI).
void benchmark_thread_entry(void* args) {
    BenchmarkCtx* ctx = static_cast<BenchmarkCtx*>(args);

    // Exit immediately in Normal mode — zero overhead on normal boots.
    if (*ctx->bm_mode != 1) {
        LOG_D("[BM] Normal mode, benchmark thread exiting.");
        vTaskDelete(NULL);
        return;
    }

    BenchmarkState& bm = *ctx->bm;

    uint16_t freq_min   = nvs_config_get_u16(NVS_CONFIG_BM_FREQ_MIN,   400);
    uint16_t freq_max   = nvs_config_get_u16(NVS_CONFIG_BM_FREQ_MAX,   625);
    uint16_t freq_step  = nvs_config_get_u16(NVS_CONFIG_BM_FREQ_STEP,  50);
    uint16_t vcore_min  = nvs_config_get_u16(NVS_CONFIG_BM_VCORE_MIN,  1000);
    uint16_t vcore_max  = nvs_config_get_u16(NVS_CONFIG_BM_VCORE_MAX,  1300);
    uint16_t vcore_step = nvs_config_get_u16(NVS_CONFIG_BM_VCORE_STEP, 25);
    uint8_t  smp_intv   = nvs_config_get_u8 (NVS_CONFIG_BM_SAMPLE_INTV, 10);
    uint16_t bm_time    = nvs_config_get_u16(NVS_CONFIG_BM_TIME,        180);
    uint16_t stab_time  = nvs_config_get_u16(NVS_CONFIG_BM_STAB_TIME,   120);

    uint16_t cur_freq   = nvs_config_get_u16(NVS_CONFIG_BM_CUR_FREQ,  freq_min);
    uint16_t cur_vcore  = nvs_config_get_u16(NVS_CONFIG_BM_CUR_VCORE, vcore_min);

    LOG_W("[BM] === Benchmark started: freq=%d vcore=%d ===", cur_freq, cur_vcore);
    LOG_W("[BM] Range freq %d~%dMHz step=%d, vcore %d~%dmV step=%d",
          freq_min, freq_max, freq_step, vcore_min, vcore_max, vcore_step);
    LOG_W("[BM] stab=%ds bm=%ds smp=%ds", stab_time, bm_time, smp_intv);

    xEventGroupWaitBits(ctx->init_evt, INIT_EVENT_MINER_READY, pdFALSE, pdTRUE, portMAX_DELAY);

    bm.cur_freq     = cur_freq;
    bm.cur_vcore    = cur_vcore;
    bm.freq_min     = freq_min;
    bm.freq_max     = freq_max;
    bm.freq_step    = freq_step;
    bm.vcore_min    = vcore_min;
    bm.vcore_max    = vcore_max;
    bm.vcore_step   = vcore_step;
    bm.phase_total  = stab_time;
    bm.phase_elapsed = 0;
    bm.in_stab      = true;
    bm.avg_hr_ghs   = 0.0f;
    bm.asic_temp    = 0.0f;
    bm.vcore_temp   = 0.0f;
    bm.stab_total   = stab_time;
    bm.bm_total     = (uint16_t)bm_time;
    {
        uint32_t sc = ctx->miner ? ctx->miner->get_asic_small_cores() : 0;
        uint8_t  ac = ctx->miner ? ctx->miner->get_asic_count()       : 0;
        bm.exp_hr_ghs = (float)((double)cur_freq * ((double)(sc * ac) / 1000.0));
    }
    bm.active = true;

    uint16_t bm_freq_total = (freq_step > 0) ? ((freq_max - freq_min) / freq_step + 1) : 1;
    uint16_t bm_freq_idx   = (freq_step > 0) ? ((cur_freq  - freq_min) / freq_step + 1) : 1;
    uint32_t bm_per_round  = (uint32_t)stab_time + (uint32_t)bm_time;
    uint32_t bm_vc_rounds_this   = (vcore_step > 0 && vcore_max >= cur_vcore)
                                    ? ((vcore_max - cur_vcore) / vcore_step + 1) : 1;
    uint32_t bm_vc_rounds_future = bm_vc_rounds_this + 1;
    uint32_t bm_freq_remaining   = (bm_freq_total > bm_freq_idx) ? (bm_freq_total - bm_freq_idx) : 0;
    uint32_t bm_future_rounds    = (bm_vc_rounds_this > 1 ? bm_vc_rounds_this - 1 : 0) * bm_per_round
                                    + bm_freq_remaining * bm_vc_rounds_future * bm_per_round;
    bm.eta_sec = (uint32_t)stab_time + (uint32_t)bm_time + bm_future_rounds;

    LOG_W("[BM] Stabilizing for %ds at freq=%dMHz vcore=%dmV ...", stab_time, cur_freq, cur_vcore);
    for (uint16_t s = 0; s < stab_time; s++) {
        bm.phase_elapsed = s;
        bm.asic_temp     = ctx->temp->asic;
        bm.vcore_temp    = ctx->temp->vcore;
        uint32_t stab_rem = (stab_time > s) ? (stab_time - s) : 0;
        bm.eta_sec = stab_rem + (uint32_t)bm_time + bm_future_rounds;
        delay(1000);
    }

    uint32_t total_samples = bm_time / smp_intv;
    if (total_samples == 0) total_samples = 1;

    double hr_sum = 0, eff_sum = 0, pwr_sum = 0, at_sum = 0, vt_sum = 0;
    uint32_t sample_cnt = 0;
    uint8_t  zero_hr_cnt = 0;

    bm.in_stab       = false;
    bm.phase_total   = bm_time;
    bm.phase_elapsed = 0;
    bm.eta_sec       = (uint32_t)bm_time + bm_future_rounds;

    uint32_t small_cores = ctx->miner ? ctx->miner->get_asic_small_cores() : 0;
    uint8_t  asic_count  = ctx->miner ? ctx->miner->get_asic_count()       : 0;
    double   exp_hr_ghs  = (double)cur_freq * ((double)(small_cores * asic_count) / 1000.0);

    LOG_W("[BM] Expected HR: %.1f GH/s (freq=%d small_cores=%d asic_cnt=%d)",
          exp_hr_ghs, cur_freq, small_cores, asic_count);

    for (uint32_t i = 0; i < total_samples; i++) {
        for (uint8_t s = 0; s < smp_intv; s++) delay(1000);

        double hr_ghs = ctx->status->hashrate._3m / 1e9;
        double vbus   = ctx->pwr->vbus / 1000.0;
        double ibus   = ctx->pwr->ibus / 1000.0;
        double pwr_w  = vbus * ibus;
        double at     = ctx->temp->asic;

        bm.phase_elapsed = (i + 1) * smp_intv;
        bm.asic_temp     = (float)at;
        bm.vcore_temp    = ctx->temp->vcore;
        bm.avg_hr_ghs    = (sample_cnt > 0) ? (float)((hr_sum + hr_ghs) / (sample_cnt + 1)) : (float)hr_ghs;
        {
            uint32_t samp_elapsed = (i + 1) * (uint32_t)smp_intv;
            uint32_t samp_rem = (samp_elapsed < (uint32_t)bm_time) ? ((uint32_t)bm_time - samp_elapsed) : 0;
            bm.eta_sec = samp_rem + bm_future_rounds;
        }

        if (hr_ghs == 0.0) zero_hr_cnt++;
        else               zero_hr_cnt = 0;

        if (zero_hr_cnt >= 5) {
            LOG_W("[BM] Zero hashrate for 5 samples, aborting round.");
            break;
        }

        hr_sum  += hr_ghs;
        eff_sum += (hr_ghs > 0 && pwr_w > 0) ? (pwr_w / (hr_ghs / 1000.0)) : 0;
        pwr_sum += pwr_w;
        at_sum  += at;
        vt_sum  += ctx->temp->vcore;
        sample_cnt++;

        double hr_avg = hr_sum / sample_cnt;
        uint32_t remaining = (total_samples - i - 1) * smp_intv;
        LOG_W("[BM] [%3lus] %2.0f%% | HR:%.1fGH/s EXP:%.0fGH/s | AT:%.1fC VT:%.1fC | Vcore:%dmV | Pwr:%.1fW",
              (unsigned long)remaining, 100.0 * (i + 1) / total_samples,
              hr_ghs, exp_hr_ghs, at, ctx->temp->vcore, ctx->pwr->vcore, pwr_w);

        if (sample_cnt >= total_samples / 2 && exp_hr_ghs > 0 && hr_avg < exp_hr_ghs * 0.5) {
            LOG_W("[BM] Avg HR too low (%.1f < 50%% of %.1f), aborting round early.", hr_avg, exp_hr_ghs);
            break;
        }
    }

    double hr_avg  = (sample_cnt > 0) ? (hr_sum  / sample_cnt) : 0;
    double eff_avg = (sample_cnt > 0) ? (eff_sum / sample_cnt) : 0;
    double pwr_avg = (sample_cnt > 0) ? (pwr_sum / sample_cnt) : 0;
    double at_avg  = (sample_cnt > 0) ? (at_sum  / sample_cnt) : 0;
    double vt_avg  = (sample_cnt > 0) ? (vt_sum  / sample_cnt) : 0;

    bool stable = (exp_hr_ghs > 0) && (hr_avg >= exp_hr_ghs * 0.98);
    LOG_W("[BM] Round %s | avg HR:%.1fGH/s exp:%.1fGH/s | eff:%.3fJ/TH | pwr:%.2fW | asicT:%.1fC vcoreT:%.1fC",
          stable ? "STABLE" : "UNSTABLE", hr_avg, exp_hr_ghs, eff_avg, pwr_avg, at_avg, vt_avg);

    // Lambda: select best avgHR result from NVS and apply to Normal mode, then reboot.
    auto finish_and_reboot = [&](const char* reason) {
        bm.active = false;
        nvs_config_set_u8(NVS_CONFIG_BM_MODE, 0);
        nvs_config_set_u16(NVS_CONFIG_BM_CUR_FREQ,  freq_min);
        nvs_config_set_u16(NVS_CONFIG_BM_CUR_VCORE, vcore_min);
        {
            char* res_str = nvs_config_get_string(NVS_CONFIG_BM_RESULT, "[]");
            BasicJsonDocument<PsramJsonAllocator> rdoc(4096);
            DeserializationError rerr = deserializeJson(rdoc, res_str);
            free(res_str);
            uint16_t best_freq = 0, best_vcore = 0;
            float    best_avgHR = -1.0f;
            if (!rerr && rdoc.is<JsonArray>()) {
                for (JsonObject e : rdoc.as<JsonArray>()) {
                    float avgHR = e["avgHR"] | 0.0f;
                    if (avgHR > best_avgHR) {
                        best_avgHR = avgHR;
                        best_freq  = e["freq"]  | (uint16_t)0;
                        best_vcore = e["vcore"] | (uint16_t)0;
                    }
                }
            }
            if (best_freq > 0 && best_vcore > 0) {
                LOG_W("[BM] Best result: freq=%dMHz vcore=%dmV avgHR=%.1fGH/s — applying to Normal mode.",
                      best_freq, best_vcore, best_avgHR);
                nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ,    best_freq);
                nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, best_vcore);
            } else {
                LOG_W("[BM] No stable results found, keeping current Normal mode settings.");
            }
        }
        {
            uint32_t _start = nvs_config_get_u32(NVS_CONFIG_BM_START_TS, 0);
            if (_start > 0) nvs_config_set_u32(NVS_CONFIG_BM_TOTAL_SEC, (uint32_t)time(nullptr) - _start);
        }
        reboot_intent_set(REBOOT_INTENT_DAEMON_GENERIC, reason);
        xSemaphoreGive(ctx->reboot_xsem);
        vTaskDelete(NULL);
    };

    if (stable) {
        char* existing = nvs_config_get_string(NVS_CONFIG_BM_RESULT, "[]");
        String results(existing);
        free(existing);

        results.trim();
        if (results.endsWith("]")) results.remove(results.length() - 1);
        if (!results.endsWith("[")) results += ",";

        char entry[300];
        snprintf(entry, sizeof(entry),
            "{\"freq\":%d,\"vcore\":%d,\"expHR\":%.1f,\"avgHR\":%.1f,\"avgAsicTemp\":%.1f,\"avgVcoreTemp\":%.1f,\"effJTH\":%.3f,\"avgPwr\":%.2f,\"ts\":%ld}",
            cur_freq, cur_vcore, exp_hr_ghs, hr_avg, at_avg, vt_avg, eff_avg, pwr_avg, (long)time(nullptr));
        results += entry;
        results += "]";

        esp_err_t nvs_write_err;
        do {
            nvs_write_err = nvs_config_try_set_string(NVS_CONFIG_BM_RESULT, results.c_str());
            if (nvs_write_err == ESP_OK) break;
            if (nvs_write_err != ESP_ERR_NVS_NOT_ENOUGH_SPACE &&
                nvs_write_err != ESP_ERR_NVS_VALUE_TOO_LONG) {
                LOG_E("[BM] NVS write failed (%s), result not saved.", esp_err_to_name(nvs_write_err));
                break;
            }
            int obj_start = results.indexOf('{');
            if (obj_start < 0) { nvs_write_err = ESP_FAIL; break; }
            int obj_end = results.indexOf('}', obj_start);
            if (obj_end < 0) { nvs_write_err = ESP_FAIL; break; }
            int remove_len = obj_end - obj_start + 1;
            if ((size_t)(obj_end + 1) < results.length() && results[obj_end + 1] == ',')
                remove_len++;
            results.remove(obj_start, remove_len);
            LOG_W("[BM] NVS full, evicted oldest entry (%u chars remaining).", (unsigned)results.length());
        } while (results.length() > 2);

        if (nvs_write_err == ESP_OK) LOG_W("[BM] Result saved: %s", entry);

        cur_freq  += freq_step;
        cur_vcore  = (cur_vcore > vcore_min + vcore_step) ? (cur_vcore - vcore_step) : vcore_min;

        if (cur_freq > freq_max) {
            LOG_W("[BM] All frequencies tested — benchmark complete, applying best result.");
            finish_and_reboot("benchmark complete, switching to Normal mode with best params");
            return;
        }
    } else {
        cur_vcore += vcore_step;
        if (cur_vcore > vcore_max) {
            LOG_W("[BM] freq=%dMHz unstable at all vcores, skipping to next freq.", cur_freq);
            cur_freq  += freq_step;
            cur_vcore  = vcore_max;
            if (cur_freq > freq_max) {
                LOG_W("[BM] All frequencies exhausted — benchmark complete, applying best result.");
                finish_and_reboot("benchmark complete (all unstable), switching to Normal mode");
                return;
            }
        }
    }

    nvs_config_set_u16(NVS_CONFIG_BM_CUR_FREQ,  cur_freq);
    nvs_config_set_u16(NVS_CONFIG_BM_CUR_VCORE, cur_vcore);
    LOG_W("[BM] Next round: freq=%dMHz vcore=%dmV, rebooting...", cur_freq, cur_vcore);
    reboot_intent_set(REBOOT_INTENT_DAEMON_GENERIC, "benchmark round complete, rebooting for next (freq,vcore)");
    xSemaphoreGive(ctx->reboot_xsem);
    vTaskDelete(NULL);
}

// ── Aphorism: fetch motivational quotes from zenquotes.io (screensaver content)
//    Mirrors legacy aphorism_thread_entry; board_sal_t* -> AphorismCtx* (DI).
//    Producer-only: appends keyword-matched quotes into a bounded pool; the UI
//    screensaver pops from the front. Logic/timing/keywords preserved verbatim.
void aphorism_thread_entry(void* args) {
    AphorismCtx* ctx = static_cast<AphorismCtx*>(args);
    AphorismState& aphorism = *ctx->state;
    auto& pool = aphorism.pool;

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
