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
    wait_for_wifi_sta_connected(ctx->init_evt, "(monitor)");

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
    // Tunable (NVS, default == historical hardcoded value 3600).
    static const uint32_t NTP_SYNC_INTERVAL = nvs_config_get_u32(NVS_CONFIG_NTP_SYNC_INTERVAL, 3600);

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

    // Apply a numeric timezone offset (e.g. "8.0", "-5.5") to the C library TZ
    // env so localtime_r()/strftime() render local time. POSIX TZ sign is
    // inverted vs the offset (UTC+8 -> "UTC-8"). Shared by the NTP-sync path and
    // the SYS_EVENT_TZ_CHANGED hot-reload path.
    auto apply_tz = [](const String& tz_str) {
        float tz_offset = tz_str.toFloat();
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
        return String(tz_buf);
    };

    while (true) {
        delay(1000);

        // Hot-reload timezone: the web UI saved a new offset (SYS_EVENT_TZ_CHANGED).
        // Re-read NVS into a local copy (avoids racing the web thread's write to
        // the shared *g_web->tz String) and re-apply immediately so local time
        // updates within ~1s instead of waiting for the next hourly NTP sync.
        if (ctx->sys_evt && (xEventGroupGetBits(ctx->sys_evt) & SYS_EVENT_TZ_CHANGED)) {
            xEventGroupClearBits(ctx->sys_evt, SYS_EVENT_TZ_CHANGED);
            String tz_nvs = nvs_config_get_string_value(NVS_CONFIG_TIMEZONE, "8.0");
            String applied = apply_tz(tz_nvs);
            LOG_W("(monitor) timezone hot-reload: tz[%s] env[%s]", tz_nvs.c_str(), applied.c_str());
        }

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

                String tz_buf = apply_tz(*ctx->tz);

                String time_local = convert_time_to_local((uint32_t)*ctx->utc);
                LOG_W("NTP sync OK  [server: %s] UTC[%llu] local[%s] tz[%s] env[%s]",
                      NTP_SERVERS[ntp_server_idx], (unsigned long long)*ctx->utc,
                      time_local.c_str(), ctx->tz->c_str(), tz_buf);

                ntp_fail_cnt    = 0;
                ntp_ever_synced = true;
                last_ntp_sync   = st.uptime_session;
                // Reconstruct boot epoch from NTP time minus session uptime.
                // This allows UI to derive precise uptime from RTC (time(NULL))
                // independently of the monitor thread's delay(1000) drift.
                if (st.boot_epoch == 0) {
                    st.boot_epoch = tv.tv_sec - (time_t)st.uptime_session;
                }
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

            hashrate_t nonce_hr = {0.0, 0.0, 0.0};
            hashrate_diag_t nonce_ring = {};
            bool nonce_ok = ctx->miner->calculate_hashrate(&nonce_hr, &nonce_ring);

            static double hcn_total_hs = 0.0;
            static uint8_t hcn_active_ch = 0;
            // inline HCN total-hashrate aggregator (single call-site in monitor)
            const float hcn_total_limit = spec.asic.num_req * spec.asic.hcn_max_ghs_per_ch;
            auto hcn_get_total_hs = [&]() -> bool {
                if (!hcn_cache_ensure_inited()) return false;
                if (xSemaphoreTake(g_hcn_cache.mutex, pdMS_TO_TICKS(5)) != pdTRUE) return false;
                const uint32_t now_ms = millis();
                double total_ghs = 0.0;
                uint8_t active = 0;
                for (uint8_t i = 0; i < HCN_MAX_ASIC_CHANNELS; i++) {
                    if (!g_hcn_cache.seen[i]) continue;
                    if ((now_ms - g_hcn_cache.gh_ms[i]) > HCN_STALE_MS) continue;
                    total_ghs += g_hcn_cache.gh_s[i];
                    active++;
                }
                if (total_ghs > hcn_total_limit) {
                    total_ghs = 0.0;
                    active = 0;
                }
                xSemaphoreGive(g_hcn_cache.mutex);
                hcn_total_hs = total_ghs * 1e9;
                hcn_active_ch = active;
                return active > 0;
            };
            bool hcn_ok = hcn_get_total_hs();
            bool use_hcn = hcn_ok && hcn_active_ch > 0;

            static bool use_hcn_last = false;
            const bool source_switched = (use_hcn != use_hcn_last);
            const bool switched_to_hcn = source_switched && use_hcn;
            if (source_switched) {
                LOG_W("Hashrate source switched to %s", use_hcn ? "HCN(0x90)" : "NONCE-fallback");
                use_hcn_last = use_hcn;
            }

            if (use_hcn) {
                // Window EMA with staged fast-start so UI converges within ~10-20s.
                static double  ema_3m  = 0.0;
                static double  ema_30m = 0.0;
                static double  ema_1h  = 0.0;
                static bool    ema_inited = false;
                static uint16_t ema_samples = 0;

                // Reset on nonce->HCN switch so stale state doesn't pollute new HCN startup.
                if (switched_to_hcn) {
                    ema_inited = false;
                    ema_3m = ema_30m = ema_1h = 0.0;
                    ema_samples = 0;
                }

                // Stage 1 (<=10 samples): aggressive lock-in; Stage 2 (<=30): settle;
                // Stage 3: steady smoothing.
                auto alpha_for = [&](double a1, double a2, double a3) -> double {
                    if (ema_samples <= 10) return a1;
                    if (ema_samples <= 30) return a2;
                    return a3;
                };

                const double A_3M  = alpha_for(0.55, 0.22, 0.10);
                const double A_30M = alpha_for(0.40, 0.14, 0.05);
                const double A_1H  = alpha_for(0.30, 0.10, 0.035);

                if (ema_inited) {
                    ema_3m  = ema_3m  + A_3M  * (hcn_total_hs - ema_3m );
                    ema_30m = ema_30m + A_30M * (hcn_total_hs - ema_30m);
                    ema_1h  = ema_1h  + A_1H  * (hcn_total_hs - ema_1h );
                } else {
                    ema_3m = ema_30m = ema_1h = hcn_total_hs;
                    ema_inited = true;
                }
                if (ema_samples < 0xFFFFu) ema_samples++;
                st.hashrate._3m  = ema_3m;
                st.hashrate._30m = ema_30m;
                st.hashrate._1h  = ema_1h;
            } else if (nonce_ok) {
                st.hashrate = nonce_hr;
            } else {
                st.hashrate = {0.0, 0.0, 0.0};
            }

            {
                static uint32_t nonce_ring_last_log_ms = 0;
                const uint32_t now_ms = millis();
                if (now_ms - nonce_ring_last_log_ms >= 60000u) {
                    nonce_ring_last_log_ms = now_ms;
                    LOG_D("[NONCE-RING] source=%s hcn_ch=%u asic_diff=%.0f size=%u samples_3m=%u samples_30m=%u sum_3m=%.0f sum_30m=%.0f sum_60m=%.0f hr_3m=%.3fTH/s hr_30m=%.3fTH/s hr_1h=%.3fTH/s",
                          use_hcn ? "HCN" : "NONCE", hcn_active_ch, ctx->miner->get_asic_diff(),
                          nonce_ring.ring_size, nonce_ring.samples_3m, nonce_ring.samples_30m,
                          nonce_ring.sum_3m, nonce_ring.sum_30m, nonce_ring.sum_60m,
                          nonce_hr._3m / 1e12, nonce_hr._30m / 1e12, nonce_hr._1h / 1e12);
                }
            }

            if (st.hashrate._3m > 0)
                st.efficiency = (ctx->pwr->vbus * ctx->pwr->ibus / 1e6) / (st.hashrate._3m / 1e12); // J/TH
            xSemaphoreGive(st.update_xsem);

            // Diagnostics only (no auto-recovery): flag HCN telemetry blackout and hashrate collapse
            // so the exact onset time is visible in the log the next time this reproduces.
            {
                static uint32_t hcn_dead_streak_s = 0;
                static bool     hcn_dead_logged   = false;
                // Don't start the "HCN telemetry lost" watchdog until the ASIC is fully
                // initialized; the frequency ramp in miner->begin() has no HCN polls
                // yet, so counting it would false-positive on every boot/resume.
                const bool miner_ready = ctx->init_evt &&
                                         ((xEventGroupGetBits(ctx->init_evt) & INIT_EVENT_MINER_READY) != 0);
                if (!miner_ready) {
                    hcn_dead_streak_s = 0;
                    hcn_dead_logged   = false;
                } else if (hcn_active_ch == 0) {
                    hcn_dead_streak_s++;
                } else {
                    hcn_dead_streak_s = 0;
                    hcn_dead_logged   = false;
                }
                if (hcn_dead_streak_s == HR_DIAG_HCN_DEAD_SEC && !hcn_dead_logged) {
                    hcn_dead_logged = true;
                    LOG_D("[DIAG] HCN(0x90) telemetry lost for %us straight, hashrate=%.2f GH/s (source=%s)",
                          HR_DIAG_HCN_DEAD_SEC, st.hashrate._3m / 1e9, use_hcn ? "HCN" : "NONCE-fallback");
                }

                static double hr_recent_peak_hs = 0.0;
                static bool   hr_halved_logged  = false;
                if (st.hashrate._3m > hr_recent_peak_hs) {
                    hr_recent_peak_hs = st.hashrate._3m;
                    hr_halved_logged  = false;
                } else if (hr_recent_peak_hs > 0.0 && st.hashrate._3m < hr_recent_peak_hs * HR_DIAG_DROP_RATIO && !hr_halved_logged) {
                    hr_halved_logged = true;
                    LOG_D("[DIAG] Hashrate dropped to %.2f GH/s, below %.0f%% of recent peak %.2f GH/s (HCN active ch=%u)",
                          st.hashrate._3m / 1e9, HR_DIAG_DROP_RATIO * 100.0f, hr_recent_peak_hs / 1e9, hcn_active_ch);
                }
                hr_recent_peak_hs *= HR_DIAG_PEAK_DECAY;
            }

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

        // --- HCN per-channel imbalance detector (1s cadence) --------------------
        // Looks for "one die hashing far below its sibling while total power is
        // unchanged" (series-stacked midpoint drift). On a sustained match it asks
        // the tx thread for a silent fast Vcore power-cycle + ASIC reinit
        // (MINER_RUNTIME_AUTO_REINITING). Chip-count agnostic (works for 2- and
        // 4-chip boards); on single-chip boards there is no sibling to compare
        // against, so the active >= 2 requirement makes it a natural no-op there.
        {
            static uint32_t ar_imbalance_streak = 0;
            static uint32_t ar_last_trigger_ms  = 0;
            static uint8_t  ar_attempts         = 0;
            static uint32_t ar_gave_up_ms       = 0;
            static uint32_t ar_post_check_due_ms = 0;

            const uint32_t now_ms = millis();
            bool ar_safety_ok = true;
            {
                const MinerRuntimeState rstate = st.runtime_state;
                if (rstate != MINER_RUNTIME_RUNNING)          ar_safety_ok = false;
                if (st.user_paused)                            ar_safety_ok = false;
                if (st.suppress_activity_checks(now_ms))       ar_safety_ok = false;
                const bool miner_ready = ctx->init_evt &&
                    ((xEventGroupGetBits(ctx->init_evt) & INIT_EVENT_MINER_READY) != 0);
                if (!miner_ready)                              ar_safety_ok = false;
                if (ctx->bm_mode && *ctx->bm_mode != 0)        ar_safety_ok = false;
                if (ctx->ota_running && *ctx->ota_running)     ar_safety_ok = false;
                if (ctx->miner && ctx->miner->is_asic_frequency_updating()) ar_safety_ok = false;
                if (ar_attempts >= AR_MAX_ATTEMPTS)            ar_safety_ok = false;
                if (ar_last_trigger_ms != 0 &&
                    (uint32_t)(now_ms - ar_last_trigger_ms) < AR_COOLDOWN_MS) ar_safety_ok = false;
            }

            // Post-reinit verdict: re-check channel balance once the new EMA has
            // had time to re-warm, so the log shows whether the power-cycle helped.
            if (ar_post_check_due_ms != 0 && (int32_t)(now_ms - ar_post_check_due_ms) >= 0) {
                ar_post_check_due_ms = 0;
                if (hcn_cache_ensure_inited() &&
                    xSemaphoreTake(g_hcn_cache.mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    float vmin = 0.0f, vmax = 0.0f;
                    bool  any  = false;
                    for (uint8_t i = 0; i < HCN_MAX_ASIC_CHANNELS; i++) {
                        if (!g_hcn_cache.seen[i]) continue;
                        if ((now_ms - g_hcn_cache.gh_ms[i]) > HCN_STALE_MS) continue;
                        if (g_hcn_cache.sample_count[i] < HCN_FASTSTART_SAMPLES) continue;
                        const float v = g_hcn_cache.gh_s[i];
                        if (!any) { vmin = vmax = v; any = true; }
                        else { if (v < vmin) vmin = v; if (v > vmax) vmax = v; }
                    }
                    xSemaphoreGive(g_hcn_cache.mutex);
                    if (any && vmax > 0.0f) {
                        LOG_W("[ASIC-REINIT] post-reinit check: min=%.0f GH/s max=%.0f GH/s (ratio=%.2f) — %s",
                              vmin, vmax, vmin / vmax,
                              (vmin >= AR_IMBALANCE_RATIO * vmax) ? "recovered" : "still imbalanced");
                    } else {
                        LOG_W("[ASIC-REINIT] post-reinit check: no HCN channels re-warmed yet");
                    }
                }
            }

            if (!ar_safety_ok) {
                ar_imbalance_streak = 0;
                if (ar_attempts >= AR_MAX_ATTEMPTS && ar_gave_up_ms == 0) {
                    ar_gave_up_ms = now_ms;
                    LOG_E("[ASIC-REINIT] giving up after %u attempts this session, hardware rebalance required",
                          AR_MAX_ATTEMPTS);
                }
            } else if (hcn_cache_ensure_inited() &&
                       xSemaphoreTake(g_hcn_cache.mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                float  min_ghs = 0.0f, max_ghs = 0.0f;
                uint8_t min_ch = 0, max_ch = 0, active = 0;
                for (uint8_t i = 0; i < HCN_MAX_ASIC_CHANNELS; i++) {
                    if (!g_hcn_cache.seen[i]) continue;
                    if ((now_ms - g_hcn_cache.gh_ms[i]) > HCN_STALE_MS) continue;
                    // Skip channels still in EMA fast-start so the convergence
                    // transient right after boot/reinit is not judged as imbalance.
                    if (g_hcn_cache.sample_count[i] < HCN_FASTSTART_SAMPLES) continue;
                    const float v = g_hcn_cache.gh_s[i];
                    if (active == 0) { min_ghs = max_ghs = v; min_ch = max_ch = i; }
                    else {
                        if (v < min_ghs) { min_ghs = v; min_ch = i; }
                        if (v > max_ghs) { max_ghs = v; max_ch = i; }
                    }
                    active++;
                }
                xSemaphoreGive(g_hcn_cache.mutex);

                const bool imbalanced = (active >= 2) && (active >= spec.asic.num_req) &&
                                        (max_ghs >= AR_MIN_MAX_GHS) &&
                                        (min_ghs < AR_IMBALANCE_RATIO * max_ghs);
                if (imbalanced) {
                    ar_imbalance_streak++;
                    if (ar_imbalance_streak == 10 || ar_imbalance_streak == 20) {
                        LOG_W("[ASIC-REINIT] imbalance building: ch%u=%.0f vs ch%u=%.0f GH/s, %lus/%lus",
                              min_ch, min_ghs, max_ch, max_ghs,
                              (unsigned long)ar_imbalance_streak, (unsigned long)AR_SUSTAIN_SEC);
                    }
                    if (ar_imbalance_streak >= AR_SUSTAIN_SEC) {
                        ar_imbalance_streak = 0;
                        ar_attempts++;
                        ar_last_trigger_ms = now_ms;
                        ar_post_check_due_ms = now_ms + AR_POST_CHECK_MS;
                        g_ar_request.min_ghs     = min_ghs;
                        g_ar_request.max_ghs     = max_ghs;
                        g_ar_request.min_ch      = min_ch;
                        g_ar_request.max_ch      = max_ch;
                        g_ar_request.sustain_sec = AR_SUSTAIN_SEC;
                        g_ar_request.attempt     = ar_attempts;
                        g_ar_request.pending     = true;
                        st.runtime_state = MINER_RUNTIME_AUTO_REINITING;
                        if (st.control_xsem != NULL) xSemaphoreGive(st.control_xsem);
                        xSemaphoreGive(st.update_xsem);
                    }
                } else {
                    ar_imbalance_streak = 0;  // hysteresis: any healthy tick resets
                }

                // ── Trigger 2: consecutive rejected 0x90 samples (fast path, ~6s) ──
                // Distinct from trigger 1's slow staleness wait: if every recent 0x90
                // poll produced an implausible rate (garbage/rolling counter), the chip
                // is in a bad state and a full reinit is the fastest way to re-arm the
                // counter. Streak resets on any accepted sample, so a single good read
                // prevents a spurious trigger.
                if (g_ar_request.pending == false &&
                    g_hcn_cache.reject_streak >= AR_REJECT_STREAK) {
                    ar_attempts++;
                    ar_last_trigger_ms = now_ms;
                    ar_post_check_due_ms = now_ms + AR_POST_CHECK_MS;
                    g_ar_request.min_ghs      = 0.0f;
                    g_ar_request.max_ghs      = 0.0f;
                    g_ar_request.min_ch       = g_hcn_cache.last_reject_ch;
                    g_ar_request.max_ch       = 0;
                    g_ar_request.sustain_sec  = 0;
                    g_ar_request.attempt      = ar_attempts;
                    g_ar_request.reason       = 1;
                    g_ar_request.last_raw_ghs = g_hcn_cache.last_reject_raw_ghs;
                    g_ar_request.pending      = true;
                    st.runtime_state = MINER_RUNTIME_AUTO_REINITING;
                    if (st.control_xsem != NULL) xSemaphoreGive(st.control_xsem);
                    xSemaphoreGive(st.update_xsem);
                }
            }
        }

        // --- Trigger 3: HCN(0x90) no-progress / channel-loss watchdog -> reinit -----
        // All supported chips (BM1366/BM1370/BM1373) answer the 0x90 poll by design,
        // so "no progress" or "fewer active channels than chips" always means the
        // chain is unhealthy. Deliberately NOT gated on bm_mode — a dead 0x90
        // channel in the middle of a benchmark round is exactly the case to recover
        // from. "Progress" = any accepted sample with d_cnt > 0; total silence,
        // stalled counters and garbage frames all look the same here. Polls must
        // actually be flowing (else the pool/tx side is stalled, which is not an
        // ASIC problem).
        {
            static uint32_t np_poll_cnt        = 0;   // last seen poll counter
            static uint32_t np_flow_ms         = 0;   // last time the poll counter moved
            static uint32_t np_first_flow_ms   = 0;   // first poll after entering RUNNING (warmup anchor)
            static uint32_t np_progress_seen   = 0;   // last observed last_progress_ms
            static uint8_t  np_streak          = 0;   // consecutive 1s ticks without any progress
            static uint8_t  pl_streak          = 0;   // consecutive 1s ticks with missing channels
            static uint8_t  np_attempts        = 0;
            static uint32_t np_last_trigger_ms = 0;

            const uint32_t now_ms = millis();
            // Re-arm the warmup window whenever mining leaves RUNNING (reinit, pause,
            // error), so freshly re-initialized channels get AR_PL_WARMUP_MS to appear
            // before the channel count is judged again.
            if (st.runtime_state != MINER_RUNTIME_RUNNING) np_first_flow_ms = 0;
            if (g_hcn_poll_sent_total != np_poll_cnt) {
                if (np_first_flow_ms == 0 && st.runtime_state == MINER_RUNTIME_RUNNING) {
                    np_first_flow_ms = now_ms;
                }
                np_poll_cnt = g_hcn_poll_sent_total;
                np_flow_ms  = now_ms;
            }

            uint32_t last_progress_ms = 0;
            uint8_t  active_ch = 0;
            if (hcn_cache_ensure_inited() &&
                xSemaphoreTake(g_hcn_cache.mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                last_progress_ms = g_hcn_cache.last_progress_ms;
                for (uint8_t i = 0; i < HCN_MAX_ASIC_CHANNELS; i++) {
                    if (!g_hcn_cache.seen[i]) continue;
                    if ((now_ms - g_hcn_cache.gh_ms[i]) > HCN_STALE_MS) continue;
                    active_ch++;
                }
                xSemaphoreGive(g_hcn_cache.mutex);
            }

            const bool np_gates_ok =
                st.runtime_state == MINER_RUNTIME_RUNNING &&
                !st.user_paused &&
                ctx->init_evt &&
                ((xEventGroupGetBits(ctx->init_evt) & INIT_EVENT_MINER_READY) != 0) &&
                !(ctx->ota_running && *ctx->ota_running) &&
                !(ctx->miner && ctx->miner->is_asic_frequency_updating()) &&
                np_attempts < AR_MAX_ATTEMPTS &&
                (np_last_trigger_ms == 0 ||
                 (uint32_t)(now_ms - np_last_trigger_ms) >= AR_COOLDOWN_MS) &&
                np_poll_cnt != 0 &&                             // polls started at some point
                (uint32_t)(now_ms - np_flow_ms) < 4000u;        // ... and are still flowing

            if (!np_gates_ok) {
                np_streak = 0;
                pl_streak = 0;
            } else if (last_progress_ms != np_progress_seen) {
                np_progress_seen = last_progress_ms;
                np_streak = 0;
            } else if (++np_streak >= AR_HCN_DEAD_SEC && !g_ar_request.pending) {
                np_streak = 0;
                np_attempts++;
                np_last_trigger_ms        = now_ms;
                g_ar_request.min_ghs      = 0.0f;
                g_ar_request.max_ghs      = 0.0f;
                g_ar_request.min_ch       = 0;
                g_ar_request.max_ch       = 0;
                g_ar_request.sustain_sec  = AR_HCN_DEAD_SEC;
                g_ar_request.attempt      = np_attempts;
                g_ar_request.reason       = 2;
                g_ar_request.last_raw_ghs = 0.0f;
                g_ar_request.pending      = true;
                st.runtime_state = MINER_RUNTIME_AUTO_REINITING;
                if (st.control_xsem != NULL) xSemaphoreGive(st.control_xsem);
                xSemaphoreGive(st.update_xsem);
                LOG_W("[ASIC-REINIT] HCN no progress: polls flowing but no advancing 0x90 sample for %us -> fast power-cycle reinit (attempt %u/%u)",
                      (unsigned)AR_HCN_DEAD_SEC, np_attempts, AR_MAX_ATTEMPTS);
            }

            // Partial channel loss: one chip dead while its sibling keeps hashing
            // satisfies neither trigger 1 (needs >= 2 active channels) nor the
            // no-progress streak above (the survivor keeps feeding it). Count the
            // expected chips that have no fresh accepted sample; if any are missing
            // for AR_PL_LOSS_TICKS straight, reinit. Warmup gate keeps boot/reinit
            // transients (channels need ~2 polls to produce their first rate) from
            // looking like a loss.
            const bool pl_warmup_done = np_first_flow_ms != 0 &&
                                        (uint32_t)(now_ms - np_first_flow_ms) >= AR_PL_WARMUP_MS;
            if (!np_gates_ok || !pl_warmup_done || active_ch >= spec.asic.num_req) {
                pl_streak = 0;
            } else if (++pl_streak >= AR_PL_LOSS_TICKS && !g_ar_request.pending) {
                pl_streak = 0;
                np_attempts++;
                np_last_trigger_ms        = now_ms;
                g_ar_request.min_ghs      = 0.0f;
                g_ar_request.max_ghs      = 0.0f;
                g_ar_request.min_ch       = active_ch;
                g_ar_request.max_ch       = spec.asic.num_req;
                g_ar_request.sustain_sec  = AR_PL_LOSS_TICKS;
                g_ar_request.attempt      = np_attempts;
                g_ar_request.reason       = 3;
                g_ar_request.last_raw_ghs = 0.0f;
                g_ar_request.pending      = true;
                st.runtime_state = MINER_RUNTIME_AUTO_REINITING;
                if (st.control_xsem != NULL) xSemaphoreGive(st.control_xsem);
                xSemaphoreGive(st.update_xsem);
                LOG_W("[ASIC-REINIT] HCN channels missing: only %u/%u active for %u consecutive ticks -> fast power-cycle reinit (attempt %u/%u)",
                      active_ch, (unsigned)spec.asic.num_req, (unsigned)AR_PL_LOSS_TICKS, np_attempts, AR_MAX_ATTEMPTS);
            }
        }

        // --- safety status checks (skipped entirely in benchmark mode) ---
        if (st.uptime_session % 2 == 0) {
            if (*ctx->bm_mode == 0) {
            if (!(ctx->ota_running && *ctx->ota_running)) {
                static uint8_t  vcore_err_cnt = 0;
                static uint8_t  asic_err_cnt  = 0;
                static std::vector<uint16_t> fan_err_cnt;
                static uint16_t pwr_err_cnt   = 0;
                static uint16_t hr_err_cnt    = 0;
                if (fan_err_cnt.size() != ctx->fan_status->size()) {
                    fan_err_cnt.assign(ctx->fan_status->size(), 0);
                }
                bool suppress_activity_checks = st.suppress_activity_checks(millis());
                if (suppress_activity_checks) {
                    std::fill(fan_err_cnt.begin(), fan_err_cnt.end(), 0);
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
                    for (size_t i = 0; i < ctx->fan_status->size(); i++) {
                        auto& fan = (*ctx->fan_status)[i];
                        if (fan.id >= spec.fans.size()) continue;
                        if (fan.rpm < spec.fans[fan.id].init.danger_rpm_thr) {
                            fan_err_cnt[i]++;
                            if (fan_err_cnt[i] > 20) {
                                LOG_W("Fan rpm is too low, restart miner...");
                                reboot_intent_set(REBOOT_INTENT_FAN_STALL, "fan#%d rpm=%d < danger=%d for >20s", fan.id, fan.rpm, spec.fans[fan.id].init.danger_rpm_thr);
                                xSemaphoreGive(ctx->reboot_xsem);
                            }
                        } else {
                            fan_err_cnt[i] = 0;
                        }
                    }
                } else {
                    std::fill(fan_err_cnt.begin(), fan_err_cnt.end(), 0);
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
            node.share_rate = st.share_rate;                   // nonces/s
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

            char*    bm_result_save    = bm_result_read_all();
            uint32_t bm_start_ts_save  = nvs_config_get_u32(NVS_CONFIG_BM_START_TS,  0);
            uint32_t bm_total_sec_save = nvs_config_get_u32(NVS_CONFIG_BM_TOTAL_SEC, 0);
            bool     bm_has_data       = bm_result_save
                                         && strcmp(bm_result_save, "[]") != 0
                                         && strlen(bm_result_save) > 2;
            LOG_I("Factory reset: benchmark saved (has_data=%d, start_ts=%lu, total_sec=%lu)",
                  (int)bm_has_data, (unsigned long)bm_start_ts_save, (unsigned long)bm_total_sec_save);

            if (erase_all_nvs()) {
                if (bm_has_data)            bm_result_restore_all(bm_result_save);
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
            bool ap_config_active = ctx->init_evt &&
                ((xEventGroupGetBits(ctx->init_evt) & INIT_EVENT_WIFI_AP_READY) != 0) &&
                (*ctx->wifi_status != WL_CONNECTED);
            if (!ap_config_active) {
                WiFi.begin(ctx->wifi_cfg->sta_ssid.c_str(), ctx->wifi_cfg->sta_pwd.c_str());
            }
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

        // UI daemon — LOG ONLY, no reboot action. A render stall is reported so
        // lock-starvation can be diagnosed from logs; the recovery policy is
        // intentionally unchanged (TWDT panic is not subscribed for UI tasks).
        if (st.ui_update != 0 && millis() - st.ui_update > MINER_UI_ALIVE_TIMEOUT) {
            LOG_W("UI render stalled for %lums (last heartbeat %lu)",
                  (unsigned long)(millis() - st.ui_update), (unsigned long)st.ui_update);
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
        auto long_press_start_wrapper = [](void* param) {
            ButtonCtx* c = static_cast<ButtonCtx*>(param);
            xEventGroupClearBits(c->sys_evt, SYS_EVENT_MINER_BLOCK_HIT | SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED |
                                 SYS_EVENT_SCREEN_SAVER_TRIGGERED | SYS_EVENT_FIND_NEIGHBOR_TRIGGERED);
            if (c->on_activity) c->on_activity();
            UIManager::instance().start_setup_countdown();
        };
        auto long_press_stop_wrapper = [](void* param) {
            ButtonCtx* c = static_cast<ButtonCtx*>(param);
            if (c->on_activity) c->on_activity();
            UIManager::instance().cancel_setup_countdown();
        };
        auto during_long_press_wrapper = [](void* param) {
            ButtonCtx* c = static_cast<ButtonCtx*>(param);
            if (c->on_activity) c->on_activity();
            UIManager::instance().tick_setup_countdown();
        };
        boot_btn->attachClick(click_wrapper, ctx);
        boot_btn->attachDoubleClick(double_click_wrapper, ctx);
        boot_btn->attachLongPressStart(long_press_start_wrapper, ctx);
        boot_btn->attachLongPressStop(long_press_stop_wrapper, ctx);
        boot_btn->attachDuringLongPress(during_long_press_wrapper, ctx);
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
        auto long_press_start_wrapper = [](void* param) {
            ButtonCtx* c = static_cast<ButtonCtx*>(param);
            xEventGroupClearBits(c->sys_evt, SYS_EVENT_MINER_BLOCK_HIT | SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED |
                                 SYS_EVENT_SCREEN_SAVER_TRIGGERED | SYS_EVENT_FIND_NEIGHBOR_TRIGGERED);
            if (c->on_activity) c->on_activity();
#if defined(BOARD_NMAXE) || defined(BOARD_NMAXE_GAMMA)
                UIManager::instance().start_factory_countdown();
                return;
#elif defined(BOARD_NMQAXE_PP) || defined(BOARD_NMQAXE_PP_REV61) || defined(BOARD_NMQAXE_PP_REV81) || defined(BOARD_NMQAXE_PP_NEXUS)
#else
                #error "No board model defined. Add -D BOARD_<model> in platformio.ini"
#endif
            xSemaphoreGive(c->recover_factory_xsem);
        };
        auto long_press_stop_wrapper = [](void* param) {
            ButtonCtx* c = static_cast<ButtonCtx*>(param);
            if (c->on_activity) c->on_activity();
#if defined(BOARD_NMAXE) || defined(BOARD_NMAXE_GAMMA)
                UIManager::instance().cancel_factory_countdown();
#elif defined(BOARD_NMQAXE_PP) || defined(BOARD_NMQAXE_PP_REV61) || defined(BOARD_NMQAXE_PP_REV81) || defined(BOARD_NMQAXE_PP_NEXUS)
#else
                #error "No board model defined. Add -D BOARD_<model> in platformio.ini"
#endif
        };
        auto during_long_press_wrapper = [](void* param) {
            ButtonCtx* c = static_cast<ButtonCtx*>(param);
            if (c->on_activity) c->on_activity();
#if defined(BOARD_NMAXE) || defined(BOARD_NMAXE_GAMMA)
                UIManager::instance().tick_factory_countdown();
#elif defined(BOARD_NMQAXE_PP) || defined(BOARD_NMQAXE_PP_REV61) || defined(BOARD_NMQAXE_PP_REV81) || defined(BOARD_NMQAXE_PP_NEXUS)
#else
                #error "No board model defined. Add -D BOARD_<model> in platformio.ini"
#endif
        };
        user_btn->attachClick(click_wrapper, ctx);
        user_btn->attachDoubleClick(double_click_wrapper, ctx);
        user_btn->attachLongPressStart(long_press_start_wrapper, ctx);
        user_btn->attachLongPressStop(long_press_stop_wrapper, ctx);
        user_btn->attachDuringLongPress(during_long_press_wrapper, ctx);
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
    const int wifiPwmChannel = 4;
    const int poolPwmChannel = 5;
    const int freq = 5 * 1000;
    const int resolution = 8;

    Adafruit_NeoPixel* strip = nullptr;

    if (spec.led.wifi_pin != -1) {
        pinMode(spec.led.wifi_pin, OUTPUT);
        digitalWrite(spec.led.wifi_pin, HIGH);
        // [new] ledcSetup(wifiPwmChannel, freq, resolution);
        // [new] ledcAttachPin(spec.led.wifi_pin, wifiPwmChannel);
        // [new] ledcWrite(wifiPwmChannel, 255); // off
    }
    if (spec.led.pool_pin != -1) {
        pinMode(spec.led.pool_pin, OUTPUT);
        digitalWrite(spec.led.pool_pin, HIGH);
        // [new] ledcSetup(poolPwmChannel, freq, resolution);
        // [new] ledcAttachPin(spec.led.pool_pin, poolPwmChannel);
        // [new] ledcWrite(poolPwmChannel, 255); // off
    }
    if (spec.led.sys_pin != -1) {
#if defined(BOARD_NMAXE) || defined(BOARD_NMAXE_GAMMA)
        pinMode(spec.led.sys_pin, OUTPUT);
        ledcSetup(pwmChannel, freq, resolution);
        ledcAttachPin(spec.led.sys_pin, pwmChannel);
        ledcWrite(pwmChannel, 255); // off
#elif defined(BOARD_NMQAXE_PP) || defined(BOARD_NMQAXE_PP_REV61) || defined(BOARD_NMQAXE_PP_REV81)  || defined(BOARD_NMQAXE_PP_NEXUS)
        strip = new Adafruit_NeoPixel(8, spec.led.sys_pin, NEO_GRB + NEO_KHZ800);
        while (!strip) {
            LOG_E("Failed to create NeoPixel instance for SYS LED");
            delay(1000);
        }
        strip->begin();
        strip->show();
        strip->setBrightness(100);
#else
        #error "No board model defined. Add -D BOARD_<model> in platformio.ini"
#endif
    }

    uint64_t led_cnt = 0;
    const uint8_t dot = 20;
    while (true) {
        delay(10);
#if defined(BOARD_NMAXE) || defined(BOARD_NMAXE_GAMMA)
            if (pref.led.sleep || !pref.led.enable) {
                if (spec.led.wifi_pin != -1) digitalWrite(spec.led.wifi_pin, HIGH);
                if (spec.led.pool_pin != -1) digitalWrite(spec.led.pool_pin, HIGH);
                if (spec.led.sys_pin != -1)  ledcWrite(pwmChannel, 255);
                // [new] if (spec.led.wifi_pin != -1) ledcWrite(wifiPwmChannel, 255);
                // [new] if (spec.led.pool_pin != -1) ledcWrite(poolPwmChannel, 255);
                continue;
            }
            if (ctx->ota_running && *ctx->ota_running) {
                if (spec.led.wifi_pin != -1) digitalWrite(spec.led.wifi_pin, HIGH);
                if (spec.led.pool_pin != -1) digitalWrite(spec.led.pool_pin, HIGH);
                if (spec.led.sys_pin != -1)  ledcWrite(pwmChannel, 255);
                // [new] if (spec.led.wifi_pin != -1) ledcWrite(wifiPwmChannel, 255);
                // [new] if (spec.led.pool_pin != -1) ledcWrite(poolPwmChannel, 255);
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
            // [new] uint32_t brightness = (uint32_t)((1 + sin(speed * led_cnt / 100.0f)) * (1 << resolution - 1));
            // [new] if (spec.led.wifi_pin != -1) ledcWrite(wifiPwmChannel, brightness);
            // [new] if (spec.led.pool_pin != -1) ledcWrite(poolPwmChannel, brightness);
            // [new] if (spec.led.sys_pin != -1)  ledcWrite(pwmChannel, brightness);
            led_cnt++;
#elif defined(BOARD_NMQAXE_PP) || defined(BOARD_NMQAXE_PP_REV61) || defined(BOARD_NMQAXE_PP_REV81)  || defined(BOARD_NMQAXE_PP_NEXUS)
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
#else
        #error "No board model defined. Add -D BOARD_<model> in platformio.ini"
#endif
    }
    LOG_I("led thread exit...");
    vTaskDelete(NULL);
}
