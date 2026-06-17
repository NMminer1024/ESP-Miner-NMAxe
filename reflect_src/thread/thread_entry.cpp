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
#include "../utils/helper.h"
#include "../utils/sha/csha256.h"
#include <ArduinoJson.h>
#include <WiFi.h>
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
                    LOG_W("Stratum notify, id : %d => %s", method.id, method.raw.c_str());
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

void wifi_connect_thread_entry(void* args) {
    (void)args;
    LOG_D("(wifi) placeholder");
    while (true) { delay(10000); }
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

    while (power->get_vbus() < spec.pwr.vbus_min_required) {
        LOG_W("Vbus is %.2fV , at least %.2fV required...",
              power->get_vbus() / 1000.0, spec.pwr.vbus_min_required / 1000.0);
        delay(1000);
    }
    xEventGroupSetBits(ctx->init_evt, INIT_EVENT_VBUS_READY);

    // wait for fan ready and wifi connected before setting vcore voltage, to avoid too high
    // temperature without proper cooling or network connection for error reporting
    xEventGroupWaitBits(ctx->init_evt,
                        INIT_EVENT_FAN_READY | INIT_EVENT_WIFI_STA_CONNECTED | INIT_EVENT_VBUS_READY,
                        pdFALSE, pdTRUE, portMAX_DELAY);
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

void led_thread_entry(void* args) {
    (void)args;
    LOG_D("(led) placeholder");
    while (true) { delay(10000); }
}

void webserver_thread_entry(void* args) {
    (void)args;
    LOG_D("(web) placeholder");
    while (true) { delay(10000); }
}

void scan_thread_entry(void* args) {
    (void)args;
    LOG_D("(scan) placeholder");
    while (true) { delay(10000); }
}

void swarm_thread_entry(void* args) {
    (void)args;
    LOG_D("(swarm) placeholder");
    while (true) { delay(10000); }
}

void market_thread_entry(void* args) {
    (void)args;
    LOG_D("(market) placeholder");
    while (true) { delay(10000); }
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

void benchmark_thread_entry(void* args) {
    (void)args;
    LOG_D("(benchmark) placeholder");
    while (true) { delay(10000); }
}
