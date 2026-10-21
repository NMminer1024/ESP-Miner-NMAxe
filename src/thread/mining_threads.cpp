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

// Ticket diff last applied to the chip; reset after every begin() since init() rewrites it.
static double asic_diff_applied = 0.0;

// Set when the last begin() used warmup_frq; the tx loop raises to req_frq once warm.
static volatile bool s_warmup_pending = false;

// Measured on BM1366: putting a cold die straight onto a high PLL frequency hangs a core
// group until the next re-init, so cold (re)starts go through spec.asic.warmup_frq first.
static uint16_t start_frequency(const MinerCtx* ctx) {
    const auto& a = ctx->spec->asic;
    s_warmup_pending = false;
    if (a.warmup_frq == 0 || a.req_frq <= a.warmup_frq || ctx->temp == nullptr) return a.req_frq;
    const float t = ctx->temp->asic;
    if (!(t < a.warmup_temp)) return a.req_frq;
    s_warmup_pending = true;
    LOG_W("ASIC cold (%.1fC < %.0fC): start at %uMHz, raise to %uMHz once warm",
          t, a.warmup_temp, a.warmup_frq, a.req_frq);
    return a.warmup_frq;
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

    // Cold start: reset → init at default baud → switch to work baud.
    if (!ctx->miner->begin(start_frequency(ctx), ctx->spec->asic.diff_thr_init,
                           ctx->spec->asic.com_baud_init, ctx->spec->asic.com_baud_work)) {
        while (true) {
            LOG_E("Miner ASIC init failed, retrying...");
            delay(1000);
        }
    }
    LOG_I("%s init completed, job interval set to %d ms",
          ctx->spec->asic.name.c_str(), ctx->spec->asic.job_interval_ms);
    xEventGroupSetBits(ctx->init_evt, INIT_EVENT_MINER_READY);
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

    auto raise_after_warmup = [&]() {
        if (!s_warmup_pending || st.runtime_state != MINER_RUNTIME_RUNNING) return;
        if (miner->is_asic_frequency_updating()) return;
        const float t = ctx->temp ? ctx->temp->asic : 0.0f;
        if (t < spec.asic.warmup_temp) {
            static uint32_t last_log_ms = 0;
            if (millis() - last_log_ms >= 60000) {
                LOG_W("ASIC warming up: %.1fC < %.0fC, holding %uMHz (target %uMHz)",
                      t, spec.asic.warmup_temp, miner->get_asic_frequency_current(), spec.asic.req_frq);
                last_log_ms = millis();
            }
            return;
        }
        s_warmup_pending = false;
        if (miner->get_asic_frequency_current() == spec.asic.req_frq) return;
        LOG_W("ASIC warm (%.1fC): raising PLL %uMHz -> %uMHz",
              t, miner->get_asic_frequency_current(), spec.asic.req_frq);
        miner->request_asic_frequency(spec.asic.req_frq);
    };

    auto clear_mining_runtime_caches = [&]() {
        if (miner != nullptr) {
            miner->clear_asic_job_cache();
            miner->reset_hashrate();
            miner->end();
        }
        hcn_cache_reset("pause/clear runtime caches");
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

        // Firmware-initiated fast reinit (HCN imbalance recovery). Compressed copy
        // of the RESUMING sequence: no user_paused (pause overlay stays hidden),
        // short fixed discharge, fast ready polling. On any failure we fall into
        // MINER_RUNTIME_ERROR, which is also controlled-idle: the daemon's
        // power-low / hashrate-low watchdogs stay suppressed and only the generic
        // ASIC-frozen path (if no nonce ever arrives) can escalate to a reboot.
        if (state == MINER_RUNTIME_AUTO_REINITING) {
            const float    req_min    = g_ar_request.min_ghs;
            const float    req_max    = g_ar_request.max_ghs;
            const uint8_t  req_min_ch = g_ar_request.min_ch;
            const uint8_t  req_max_ch = g_ar_request.max_ch;
            const uint32_t req_sus    = g_ar_request.sustain_sec;
            const uint8_t  req_att    = g_ar_request.attempt;
            const uint8_t  req_reason = g_ar_request.reason;
            const float    req_raw    = g_ar_request.last_raw_ghs;
            g_ar_request.pending = false;
            if (req_reason == 1) {
                LOG_W("[ASIC-REINIT] HCN reject streak: %u consecutive 0x90 samples failed sanity (last raw=%.0f GH/s, ch%u) -> fast power-cycle reinit (attempt %u/%u)",
                      AR_REJECT_STREAK, req_raw, req_min_ch, req_att, AR_MAX_ATTEMPTS);
            } else if (req_reason == 2) {
                LOG_W("[ASIC-REINIT] HCN no progress: %lus without an advancing 0x90 sample while polls were flowing -> fast power-cycle reinit (attempt %u/%u)",
                      (unsigned long)req_sus, req_att, AR_MAX_ATTEMPTS);
            } else if (req_reason == 3) {
                LOG_W("[ASIC-REINIT] HCN channels missing: only %u/%u channels active for %lus -> fast power-cycle reinit (attempt %u/%u)",
                      req_min_ch, req_max_ch, (unsigned long)req_sus, req_att, AR_MAX_ATTEMPTS);
            } else {
                LOG_W("[ASIC-REINIT] HCN imbalance: ch%u=%.0f GH/s vs ch%u=%.0f GH/s (min/max=%.2f < %.2f, sustained %lus) -> fast power-cycle reinit (attempt %u/%u)",
                      req_min_ch, req_min, req_max_ch, req_max,
                      (req_max > 0.0f) ? (req_min / req_max) : 0.0f, (double)AR_IMBALANCE_RATIO,
                      (unsigned long)req_sus, req_att, AR_MAX_ATTEMPTS);
            }

            refresh_mining_timeouts();
            clear_mining_runtime_caches();

            // Vcore off -> discharge -> on: the series midpoint re-divides on the
            // rising edge with both dies cold, which is the actual rebalance event.
            power->set_vcore_status(PWR_OFF);
            delay(AR_DISCHARGE_MS);
            power->clear_faults();
            power->set_vcore_voltage(spec.asic.req_vcore);
            power->set_vcore_status(PWR_ON);

            bool vcore_ready = false;
            uint32_t start_ms = millis();
            while (millis() - start_ms < AR_READY_TIMEOUT_MS) {
                if (power->is_vcore_ready()) { vcore_ready = true; break; }
                refresh_mining_timeouts();
                delay(AR_READY_POLL_MS);
            }

            if (!vcore_ready) {
                LOG_E("[ASIC-REINIT] Vcore not ready within %lums, giving up (state -> error)",
                      (unsigned long)AR_READY_TIMEOUT_MS);
                power->set_vcore_status(PWR_OFF);
                st.runtime_state = MINER_RUNTIME_ERROR;
                refresh_mining_timeouts();
                xSemaphoreGive(st.update_xsem);
                return true;
            }

            // Wait for the Nexus logic-level rail to rise and the ASIC to
            // stabilize before ESP32 talks to it. Mirrors the cold-start 1000ms wait.
            delay(1000);

            miner->clear_asic_job_cache();
            miner->reset_hashrate();
            hcn_cache_reset("auto-reinit before begin");
            power->clear_faults();
            st.hashrate = {0.0, 0.0, 0.0};
            // Hard reset drops the ASIC to its default baud, so begin() must redo the
            // full low-baud init -> ramp -> work-baud switch (same as cold start).
            if (!miner->begin(start_frequency(ctx), spec.asic.diff_thr_init,
                              spec.asic.com_baud_init, spec.asic.com_baud_work)) {
                LOG_E("[ASIC-REINIT] ASIC reinitialization failed (state -> error)");
                power->set_vcore_status(PWR_OFF);
                st.runtime_state = MINER_RUNTIME_ERROR;
                refresh_mining_timeouts();
                xSemaphoreGive(st.update_xsem);
                return true;
            }
            asic_diff_applied = 0.0;   // init() rewrote the chip's ticket mask

            refresh_mining_timeouts();
            st.resume_grace_until_ms = millis() + MINER_RESUME_GRACE_MS;
            st.runtime_state = MINER_RUNTIME_RUNNING;
            if (stratum != nullptr) xSemaphoreGive(stratum->new_job_xsem);
            xSemaphoreGive(st.update_xsem);
            LOG_W("[ASIC-REINIT] fast reinit done, mining resumed (grace %lums)",
                  (unsigned long)MINER_RESUME_GRACE_MS);
            return true;
        }

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
            power->clear_faults();
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

            // Same rail-settle wait as cold start (hw_threads) and the AUTO_REINITING path.
            delay(1000);
            LOG_W("Vcore ready at %dmV (req %dmV) before ASIC reinit",
                  power->get_vcore(), spec.asic.req_vcore);

            miner->clear_asic_job_cache();
            miner->reset_hashrate();
            hcn_cache_reset("resume before reinit");
            power->clear_faults();
            st.hashrate = {0.0, 0.0, 0.0};
            // Resume after Vcore restore: same sequence as cold start — reset, init at
            // default baud, then switch to work baud.
            if (!miner->begin(start_frequency(ctx), spec.asic.diff_thr_init,
                              spec.asic.com_baud_init, spec.asic.com_baud_work)) {
                LOG_E("Mining resume failed: ASIC reinitialization failed");
                power->set_vcore_status(PWR_OFF);
                st.runtime_state = MINER_RUNTIME_ERROR;
                st.user_paused = true;
                st.pause_started_ms = 0;
                refresh_mining_timeouts();
                xSemaphoreGive(st.update_xsem);
                return true;
            }
            asic_diff_applied = 0.0;   // init() rewrote the chip's ticket mask

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

    // Wait for ASIC hardware init to complete before sending any job
    LOG_I("(asic_tx) waiting for miner ready gate...");
    xEventGroupWaitBits(ctx->init_evt, INIT_EVENT_MINER_READY, pdFALSE, pdTRUE, portMAX_DELAY);
    LOG_I("(asic_tx) miner ready gate opened.");

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
        raise_after_warmup();

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
            // During ASIC init/ramp there may be multiple queued notify jobs.
            // Drain queue and keep the newest job to avoid submitting stale shares.
            while (stratum->get_job_cache_size() > 0) {
                pool_job_data_t next_job = stratum->pop_job_cache();
                if (next_job.id != "") miner->pool_job_now = next_job;
            }
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
            raise_after_warmup();

            // construct asic job and send to asic
            if (!miner->mining(&miner->pool_job_now)) {
                delay(10); // avoid tight spin loop tripping interrupt WDT when mining() fails
                continue;
            }
            // exit if pool disconnected
            if (!stratum->is_subscribed()) break;

            // Periodic HCN register poll for hashrate diagnostics.
            // Independent of job rate — poll interval configurable here.
            {
                uint32_t now = millis();
                // Broadcast zero-write to arm the 0x90 counter before the first poll of
                // every ASIC init cycle (a reference vendor driver does this; a cold or
                // re-inited chip's counter state is otherwise undefined). The pending
                // flag is re-armed by miner->begin(), so pause/resume and auto-reinit
                // cycles re-arm too — not just once per firmware lifetime.
                if (miner->consume_hcn_reset_pending()) {
                    miner->reset_hcn_register();
                }
                static uint32_t hcn_last_ms = 0;
                constexpr uint32_t HCN_POLL_MS = 2000;
                if (now - hcn_last_ms >= HCN_POLL_MS) {
                    miner->poll_hcn_register();
                    g_hcn_poll_sent_total++;
                    hcn_last_ms = now;
                }
            }

            // set asic diff as pool diff if pool diff < initial asic diff
            double target_diff = min(stratum->get_pool_difficulty(), (double)spec.asic.diff_thr_init);
            if (target_diff != asic_diff_applied) {
                uint32_t diff = miner->set_asic_diff(target_diff);
                LOG_W("Change asic diff from [%.1f] to [%d/%.1f] successfully", asic_diff_applied, diff, target_diff);
                asic_diff_applied = target_diff;
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

    const float hcn_max_ghs = ctx->spec->asic.hcn_max_ghs_per_ch;

    struct NonceDiag {
        uint32_t start_ms;
        uint32_t raw;
        uint32_t invalid_asic_id;
        uint32_t unsubscribed;
        uint32_t job_ok;
        uint32_t job_miss;
        uint32_t calc_invalid;
        uint32_t below_asic;
        uint32_t context_miss;
        uint32_t duplicate;
        uint32_t recorded;
        uint32_t below_pool;
        uint32_t submit_attempt;
        uint32_t submit_queued;
        uint32_t rx_timeout;
        uint32_t rx_invalid_size;
        uint32_t rx_invalid_response;
        uint32_t rx_other_error;
        uint32_t accepted_base;
        uint32_t rejected_base;
        double   recorded_diff_sum;
        uint32_t raw_ch[HCN_MAX_ASIC_CHANNELS];
        uint32_t job_miss_ch[HCN_MAX_ASIC_CHANNELS];
        uint32_t below_asic_ch[HCN_MAX_ASIC_CHANNELS];
        uint32_t context_miss_ch[HCN_MAX_ASIC_CHANNELS];
        uint32_t duplicate_ch[HCN_MAX_ASIC_CHANNELS];
        uint32_t recorded_ch[HCN_MAX_ASIC_CHANNELS];
    };

    NonceDiag nonce_diag = {};
    nonce_diag.start_ms = millis();
    nonce_diag.accepted_base = st.share_accepted;
    nonce_diag.rejected_base = st.share_rejected;

    auto nonce_diag_log_and_reset = [&]() {
        const uint32_t now_ms = millis();
        const uint32_t dt_ms = now_ms - nonce_diag.start_ms;
        if (dt_ms < 60000u) return;

        const double interval_hr_ths = dt_ms > 0
            ? nonce_diag.recorded_diff_sum * 4294967296.0 / ((double)dt_ms / 1000.0) / 1e12
            : 0.0;
        const uint32_t accepted_now = st.share_accepted;
        const uint32_t rejected_now = st.share_rejected;
        LOG_D("[NONCE-DIAG] dt=%lums asic_diff=%.0f pool_diff=%.3f raw=%u bad_id=%u unsub=%u job_ok=%u job_miss=%u calc_invalid=%u below_asic=%u ctx_miss=%u dup=%u recorded=%u below_pool=%u submit=%u queued=%u pool_ack=%u pool_reject=%u",
              (unsigned long)dt_ms, miner->get_asic_diff(), stratum->get_pool_difficulty(),
              nonce_diag.raw, nonce_diag.invalid_asic_id, nonce_diag.unsubscribed,
              nonce_diag.job_ok, nonce_diag.job_miss, nonce_diag.calc_invalid,
              nonce_diag.below_asic, nonce_diag.context_miss, nonce_diag.duplicate,
              nonce_diag.recorded, nonce_diag.below_pool, nonce_diag.submit_attempt,
              nonce_diag.submit_queued, accepted_now - nonce_diag.accepted_base,
              rejected_now - nonce_diag.rejected_base);
        LOG_D("[NONCE-RATE] dt=%lums recorded=%u diff_sum=%.0f interval_hr=%.3fTH/s rx_timeout=%u invalid_size=%u invalid_rsp=%u other_err=%u",
              (unsigned long)dt_ms, nonce_diag.recorded, nonce_diag.recorded_diff_sum,
              interval_hr_ths, nonce_diag.rx_timeout, nonce_diag.rx_invalid_size,
              nonce_diag.rx_invalid_response, nonce_diag.rx_other_error);
        for (uint8_t i = 0; i < HCN_MAX_ASIC_CHANNELS; i++) {
            if (nonce_diag.raw_ch[i] == 0 && nonce_diag.recorded_ch[i] == 0 &&
                nonce_diag.job_miss_ch[i] == 0 && nonce_diag.below_asic_ch[i] == 0 &&
                nonce_diag.context_miss_ch[i] == 0 && nonce_diag.duplicate_ch[i] == 0) continue;
            LOG_D("[NONCE-CH%u] raw=%u job_miss=%u below_asic=%u ctx_miss=%u dup=%u recorded=%u",
                  i, nonce_diag.raw_ch[i], nonce_diag.job_miss_ch[i],
                  nonce_diag.below_asic_ch[i], nonce_diag.context_miss_ch[i],
                  nonce_diag.duplicate_ch[i], nonce_diag.recorded_ch[i]);
        }

        nonce_diag = {};
        nonce_diag.start_ms = now_ms;
        nonce_diag.accepted_base = accepted_now;
        nonce_diag.rejected_base = rejected_now;
    };

    auto on_hcn_result = [hcn_max_ghs, ctx](const asic_hcn_result& hcn) {
        // lazy init
        if (!hcn_cache_ensure_inited()) return;
        if (hcn.asic_id >= HCN_MAX_ASIC_CHANNELS) return;
        if (xSemaphoreTake(g_hcn_cache.mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;

        const uint8_t asic_id = hcn.asic_id;
        const uint32_t now_ms = millis();

        if (g_hcn_cache.seen[asic_id]) {
            // Unsigned wraparound subtraction: correctly handles the register rolling
            // over past UINT32_MAX (legit at these rates only after weeks of uptime).
            // A real ASIC reset/reinit (counter jumps back near 0) instead yields a huge
            // d_cnt close to 2^32, which the raw_ghs sanity check below rejects anyway -
            // no separate "moved backwards = reset" special case needed.
            const uint32_t d_cnt = hcn.hash_count - g_hcn_cache.prev_cnt[asic_id];
            const uint32_t d_ms  = now_ms - g_hcn_cache.prev_ms[asic_id];
            if (d_ms >= HCN_MIN_DELTA_MS) {
                if (d_cnt == 0) {
                    // Counter not incrementing: the chip stopped hashing or its 0x90
                    // counter never started. Do NOT refresh gh_ms — a dead counter
                    // must not keep the channel "active" at 0 GH/s, otherwise it
                    // suppresses the nonce-fallback hashrate indefinitely. After
                    // HCN_STALL_DROP consecutive stalls, drop the channel outright
                    // (the monitor's no-progress watchdog escalates to a full reinit).
                    g_hcn_cache.diag_stall_cnt++;
                    if (g_hcn_cache.stall_cnt[asic_id] < 255) g_hcn_cache.stall_cnt[asic_id]++;
                    if (g_hcn_cache.stall_cnt[asic_id] >= HCN_STALL_DROP) {
                        g_hcn_cache.gh_s[asic_id]         = 0.0f;
                        g_hcn_cache.gh_ms[asic_id]        = 0;
                        g_hcn_cache.sample_count[asic_id] = 0;
                    }
                    g_hcn_cache.prev_cnt[asic_id] = hcn.hash_count;
                    g_hcn_cache.prev_ms[asic_id]  = now_ms;
                    xSemaphoreGive(g_hcn_cache.mutex);
                    return;
                }
                g_hcn_cache.stall_cnt[asic_id] = 0;
                const float raw_ghs = (float)((double)d_cnt * 4294967296.0 / ((double)d_ms * 1000000.0));
                if (!std::isfinite(raw_ghs) || raw_ghs < 0.0f || raw_ghs > hcn_max_ghs) {
                    // Implausible rate: either noise or a genuine ASIC reset/reinit.
                    // Reset this channel's baseline/EMA so the next sample re-warms clean.
                    g_hcn_cache.diag_reject_cnt++;
                    g_hcn_cache.diag_last_reject_raw_ghs = raw_ghs;
                    g_hcn_cache.diag_last_reject_d_cnt   = d_cnt;
                    g_hcn_cache.diag_last_reject_d_ms    = d_ms;
                    g_hcn_cache.prev_cnt[asic_id] = hcn.hash_count;
                    g_hcn_cache.prev_ms[asic_id]  = now_ms;
                    g_hcn_cache.gh_s[asic_id] = 0.0f;
                    g_hcn_cache.gh_ms[asic_id] = 0;
                    g_hcn_cache.sample_count[asic_id] = 0;
                    if (g_hcn_cache.reject_streak < 255) g_hcn_cache.reject_streak++;
                    g_hcn_cache.last_reject_raw_ghs = raw_ghs;
                    g_hcn_cache.last_reject_ch      = asic_id;
                    xSemaphoreGive(g_hcn_cache.mutex);
                    return;
                }
                g_hcn_cache.diag_ok_cnt++;
                g_hcn_cache.reject_streak = 0;   // any accepted sample heals the streak
                g_hcn_cache.last_progress_ms = now_ms;  // d_cnt > 0 here: hashing evidence

                const float alpha = (g_hcn_cache.sample_count[asic_id] < HCN_FASTSTART_SAMPLES)
                                    ? HCN_EMA_FASTSTART : HCN_EMA_ALPHA;
                if (g_hcn_cache.gh_s[asic_id] > 0.0f) {
                    g_hcn_cache.gh_s[asic_id] = g_hcn_cache.gh_s[asic_id] + alpha * (raw_ghs - g_hcn_cache.gh_s[asic_id]);
                } else {
                    g_hcn_cache.gh_s[asic_id] = raw_ghs;
                }
                if (g_hcn_cache.sample_count[asic_id] < 255) g_hcn_cache.sample_count[asic_id]++;
                g_hcn_cache.gh_ms[asic_id] = now_ms;
            } else {
                g_hcn_cache.diag_skip_cnt++;
            }
        }
        g_hcn_cache.prev_cnt[asic_id] = hcn.hash_count;
        g_hcn_cache.prev_ms[asic_id]  = now_ms;
        g_hcn_cache.seen[asic_id]     = true;

        if (now_ms - g_hcn_cache.last_log_ms >= HCN_LOG_INTERVAL_MS) {
            g_hcn_cache.last_log_ms = now_ms;
            char buf[256] = {0};
            int off = 0;
            double total = 0.0;
            bool first = true;
            off += snprintf(buf + off, sizeof(buf) - off, "(");
            for (uint8_t i = 0; i < HCN_MAX_ASIC_CHANNELS; i++) {
                if (!g_hcn_cache.seen[i]) continue;
                if ((now_ms - g_hcn_cache.gh_ms[i]) > HCN_STALE_MS) continue;
                total += g_hcn_cache.gh_s[i];
                if (!first) off += snprintf(buf + off, sizeof(buf) - off, ", ");
                off += snprintf(buf + off, sizeof(buf) - off, "%.0f", g_hcn_cache.gh_s[i]);
                first = false;
                if (off >= (int)sizeof(buf) - 16) break;
            }
            off += snprintf(buf + off, sizeof(buf) - off, ")");
            LOG_W("HCN: %s = %.2f GH/s", buf, total);
            LOG_D("[DIAG] HCN calc: ok=%u reject=%u skip=%u stall=%u | last_reject raw_ghs=%.2f d_cnt=%u d_ms=%u",
                  g_hcn_cache.diag_ok_cnt, g_hcn_cache.diag_reject_cnt, g_hcn_cache.diag_skip_cnt, g_hcn_cache.diag_stall_cnt,
                  g_hcn_cache.diag_last_reject_raw_ghs, g_hcn_cache.diag_last_reject_d_cnt, g_hcn_cache.diag_last_reject_d_ms);
            g_hcn_cache.diag_ok_cnt = 0;
            g_hcn_cache.diag_reject_cnt = 0;
            g_hcn_cache.diag_skip_cnt = 0;
            g_hcn_cache.diag_stall_cnt = 0;

            uint32_t tag_seen = 0, decoded_ok = 0;
            ctx->miner->get_hcn_diag_counts(&tag_seen, &decoded_ok);
            LOG_D("[DIAG] HCN poll_sent=%u frame_tag_seen(0x90)=%u frame_decoded_ok=%u",
                  g_hcn_poll_sent_total, tag_seen, decoded_ok);
        }

        xSemaphoreGive(g_hcn_cache.mutex);
    };

    const bool ota_running_default = false;
    uint32_t rx_idle_slices = 0;

    // Same ready gate as the tx thread: without it, an early stratum subscribe
    // makes this thread sit in listen_asic_rsp() while the init thread is still
    // probing/configuring the ASIC (chip enumeration, PLL readback), stealing
    // those command responses.
    LOG_I("(asic_rx) waiting for miner ready gate...");
    xEventGroupWaitBits(ctx->init_evt, INIT_EVENT_MINER_READY, pdFALSE, pdTRUE, portMAX_DELAY);
    LOG_I("(asic_rx) miner ready gate opened.");

    while (true) {
        if (st.is_controlled_idle()) { delay(100); continue; }
        if (!stratum->is_subscribed()) { delay(1000); continue; }
        if (miner->is_asic_frequency_updating()) { delay(50); continue; }
        if (ctx->ota_running ? *ctx->ota_running : ota_running_default) { delay(50); continue; }

        // Short slices: a long blocking read outlives pause/re-init and steals the
        // register replies begin() is waiting for.
        asic_rx_result rx = miner->listen_asic_rsp(1000);
        nonce_diag_log_and_reset();
        if (miner->is_asic_frequency_updating()) continue;
        if (rx.status != ASIC_RX_STATUS_TIMEOUT) rx_idle_slices = 0;
        if (rx.status == ASIC_RX_STATUS_OK) {
            if (rx.type == ASIC_RX_TYPE_HCN) {
                on_hcn_result(rx.data.hcn);
                continue;
            }

            if (rx.type != ASIC_RX_TYPE_NONCE) continue;

            result = rx.data.nonce;
            nonce_diag.raw++;
            const bool diag_asic_valid = result.asic_id < HCN_MAX_ASIC_CHANNELS;
            if (diag_asic_valid) nonce_diag.raw_ch[result.asic_id]++;
            else nonce_diag.invalid_asic_id++;
            if (!stratum->is_subscribed()) {
                nonce_diag.unsubscribed++;
                continue;
            }
            if (miner->find_job_by_asic_job_id(result.asic.job_id, &job)) {
                nonce_diag.job_ok++;
                st.asic_update = millis();
                // Constrain ASIC-returned version bits by current pool mask.
                // This avoids invalid rolling bits during early startup before
                // mining.configure/mining.set_version_mask fully settles.
                const uint32_t base_version = *(uint32_t*)job.version;
                const uint32_t version_mask = stratum->get_version_mask();
                const uint32_t version_bits = (reverse_uint16(result.asic.version) << 13) & version_mask; // logic from bitaxe
                const uint32_t version      = (base_version & ~version_mask) | version_bits;
                double diff = calculate_diff(version, job.prev_block_hash, job.merkle_root,
                                             *(uint32_t*)job.ntime, *(uint32_t*)job.nbits, result.asic.nonce);

                if ((diff <= std::numeric_limits<double>::epsilon()) || std::isnan(diff) || std::isinf(diff)) {
                    nonce_diag.calc_invalid++;
                    continue;
                }
                if (diff < miner->get_asic_diff()) {
                    nonce_diag.below_asic++;
                    if (diag_asic_valid) nonce_diag.below_asic_ch[result.asic_id]++;
                    continue;
                }

                // fetch job context first so dedup can run before hashrate is counted
                uint32_t version_submit = (version ^ base_version) & version_mask;
                String   pool_id_submit = miner->get_pool_job_id_by_asic_job_id(result.asic.job_id);
                String   extra2_submit  = miner->get_extranonce2_by_asic_job_id(result.asic.job_id);
                if (pool_id_submit.length() == 0 || extra2_submit.length() == 0) {
                    nonce_diag.context_miss++;
                    if (diag_asic_valid) nonce_diag.context_miss_ch[result.asic_id]++;
                    continue;
                }

                // Deduplicate nonces within the same (pool_job_id, extranonce2) scope.
                static std::unordered_set<uint32_t, std::hash<uint32_t>, std::equal_to<uint32_t>, PsramAllocator<uint32_t>> _submitted_nonces;
                static String _dedup_job_key = "";
                String _cur_job_key = pool_id_submit + extra2_submit;
                if (_cur_job_key != _dedup_job_key) {
                    _submitted_nonces.clear();
                    _dedup_job_key = _cur_job_key;
                }
                if (_submitted_nonces.count(result.asic.nonce)) {
                    nonce_diag.duplicate++;
                    if (diag_asic_valid) nonce_diag.duplicate_ch[result.asic_id]++;
                    LOG_W("Dup nonce 0x%08x skipped (pool_job=%s)", result.asic.nonce, pool_id_submit.c_str());
                    continue;
                }
                _submitted_nonces.insert(result.asic.nonce);

                // record nonce into hashrate ring; calculation driven by monitor thread
                const double recorded_diff = miner->get_asic_diff();
                miner->record_nonce();
                nonce_diag.recorded++;
                nonce_diag.recorded_diff_sum += recorded_diff;
                if (diag_asic_valid) nonce_diag.recorded_ch[result.asic_id]++;

                // per-asic share count
                st.asic_rsp_counter[result.asic_id]++;
                LOG_D("ASIC[%d] nonce 0x%08x", result.asic_id, result.asic.nonce);

                { // share-per-second counter
                    static uint32_t sps_count = 0, sps_last = 0;
                    sps_count++;
                    uint32_t now = millis();
                    uint32_t dt = now - sps_last;
                    if (dt >= 1000*10) {
                        st.share_rate = sps_count * 1000.0f / dt;
                        LOG_D("Share rate: %.1f/s", st.share_rate);
                        sps_count = 0;
                        sps_last = now;
                    }
                }

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

                LOG_I("| %d/%d  |%-6s|%-6s|%-7s|",
                      result.asic_id + 1, 
                      miner->get_asic_count(),
                      formatNumber(diff, 4).c_str(),
                      formatNumber(stratum->get_pool_difficulty(), 4).c_str(),
                      formatNumber(st.diff.network, 7).c_str());

                if (diff < stratum->get_pool_difficulty()) {
                    nonce_diag.below_pool++;
                    continue;
                }

                nonce_diag.submit_attempt++;
                bool res = miner->submit_job_share(pool_id_submit, extra2_submit, result.asic.nonce, *(uint32_t*)job.ntime, version_submit);
                if (!res) continue;
                nonce_diag.submit_queued++;

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
                st.diff.last = diff;
                const bool new_best_session = diff > st.diff.best_session;
                const bool new_best_ever = diff > st.diff.best_ever;
                if (new_best_session) st.diff.best_session = diff;
                if (new_best_ever)    st.diff.best_ever = diff;

                if (new_best_ever) {
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
            else{
                nonce_diag.job_miss++;
                if (diag_asic_valid) nonce_diag.job_miss_ch[result.asic_id]++;
                LOG_W("ASIC job ID %d not found in cache, skipping nonce 0x%08x", result.asic.job_id, result.asic.nonce);
            }
        } else if (rx.status == ASIC_RX_STATUS_INVALID_SIZE) {
            nonce_diag.rx_invalid_size++;
            LOG_W("Asic response size error.");
        } else if (rx.status == ASIC_RX_STATUS_TIMEOUT) {
            nonce_diag.rx_timeout++;
            if (++rx_idle_slices >= 30) {
                LOG_W("Asic response timeout.");
                rx_idle_slices = 0;
            }
        } else if (rx.status == ASIC_RX_STATUS_INVALID_RESPONSE) {
            nonce_diag.rx_invalid_response++;
            // LOG_W("Asic response header error.");
        } else {
            nonce_diag.rx_other_error++;
            LOG_W("Asic response error: status=%d type=%d", (int)rx.status, (int)rx.type);
        }
    }
}
