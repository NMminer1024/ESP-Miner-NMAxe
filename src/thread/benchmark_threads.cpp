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

    // Sweep defaults come from the board spec (same source as GET /api/benchmark);
    // NVS-stored user overrides win when present.
    const BoardSpecConfig* bspec = ctx->spec;
    uint16_t freq_min_def  = 400, freq_max_def = 625;
    if (bspec && !bspec->ui.setting_page.oc.empty()) {
        freq_min_def = bspec->ui.setting_page.oc.front().value;
        freq_max_def = bspec->ui.setting_page.oc.back().value;
    }
    uint16_t vcore_min_def = (bspec && bspec->asic.min_vcore)    ? bspec->asic.min_vcore    : 1000;
    // Sweep ceiling uses the user-facing vc-list max; asic.max_vcore is regulator
    // headroom for line-loss compensation and must stay out of the sweep.
    uint16_t vcore_max_def = (bspec && bspec->user_vcore_max())  ? bspec->user_vcore_max()  : 1300;
    uint16_t freq_step_def = (bspec && bspec->asic.bm_freq_step) ? bspec->asic.bm_freq_step : 25;
    uint16_t vcore_step_def= (bspec && bspec->asic.bm_vcore_step)? bspec->asic.bm_vcore_step: 25;

    uint16_t freq_min   = nvs_config_get_u16(NVS_CONFIG_BM_FREQ_MIN,   freq_min_def);
    uint16_t freq_max   = nvs_config_get_u16(NVS_CONFIG_BM_FREQ_MAX,   freq_max_def);
    uint16_t freq_step  = nvs_config_get_u16(NVS_CONFIG_BM_FREQ_STEP,  freq_step_def);
    uint16_t vcore_min  = nvs_config_get_u16(NVS_CONFIG_BM_VCORE_MIN,  vcore_min_def);
    uint16_t vcore_max  = nvs_config_get_u16(NVS_CONFIG_BM_VCORE_MAX,  vcore_max_def);
    uint16_t vcore_step = nvs_config_get_u16(NVS_CONFIG_BM_VCORE_STEP, vcore_step_def);
    uint8_t  smp_intv   = nvs_config_get_u8 (NVS_CONFIG_BM_SAMPLE_INTV, 2);
    uint16_t bm_time    = nvs_config_get_u16(NVS_CONFIG_BM_TIME,        120);
    uint16_t stab_time  = nvs_config_get_u16(NVS_CONFIG_BM_STAB_TIME,   30);

    uint16_t cur_freq   = nvs_config_get_u16(NVS_CONFIG_BM_CUR_FREQ,  freq_min);
    uint16_t cur_vcore  = nvs_config_get_u16(NVS_CONFIG_BM_CUR_VCORE, vcore_min);

    LOG_W("[BM] === Benchmark started: freq=%d vcore=%d ===", cur_freq, cur_vcore);
    LOG_W("[BM] Range freq %d~%dMHz step=%d, vcore %d~%dmV step=%d",
          freq_min, freq_max, freq_step, vcore_min, vcore_max, vcore_step);
    LOG_W("[BM] stab=%ds bm=%ds smp=%ds", stab_time, bm_time, smp_intv);

    // Reboot helper that yields to an in-flight OTA.
    //
    // The benchmark reboots by design (the new freq/vcore is applied at boot), and
    // an OTA also ends in a reboot. If both happen the benchmark's request lands
    // first and tears down the upload mid-transfer — the device then either keeps
    // running the old image or, worse, boots a partially written one. It also
    // overwrites the reboot reason, hiding that the restart came from OTA.
    //
    // All the state the next boot needs is already in NVS by the time we get here,
    // so the safe action is to let the OTA own the restart: wait for it, and only
    // raise our own if the OTA ended WITHOUT restarting (failed or cancelled).
    auto reboot_or_defer_to_ota = [&](const char* reason) {
        if (ctx->ota_running && *ctx->ota_running) {
            LOG_W("[BM] OTA in progress — state saved to NVS, deferring reboot to the OTA restart");
            // OTA success releases reboot_xsem itself; a failed/cancelled upload
            // clears the flag without restarting, which falls through to our reboot.
            uint32_t wait_start = millis();
            while (ctx->ota_running && *ctx->ota_running) {
                delay(500);
                // Safety valve: never block the benchmark thread indefinitely if the
                // flag is left set by an aborted upload.
                if (millis() - wait_start > 15 * 60 * 1000u) {
                    LOG_E("[BM] OTA still flagged running after 15 min, rebooting anyway");
                    break;
                }
            }
            LOG_W("[BM] OTA finished without restarting the device, rebooting now");
        }
        reboot_intent_set(REBOOT_INTENT_DAEMON_GENERIC, reason);
        if (ctx->reboot_xsem != nullptr) {
            xSemaphoreGive(ctx->reboot_xsem);
        }
    };

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
    bool oc_ot_abort = false;
    // Set ONLY when the regulator actually tripped (OC/OT), as opposed to an
    // unstable/zero-hashrate round. The distinction drives two behaviours:
    //   - the point is stored as an explicit failure (tombstone), and
    //   - the sweep skips the remaining vcores for this frequency instead of
    //     retrying higher (raising vcore after an OC trip feeds the fault).
    bool fault_tripped = false;
    for (uint16_t s = 0; s < stab_time; s++) {
        bm.phase_elapsed = s;
        bm.asic_temp     = ctx->temp->asic;
        bm.vcore_temp    = ctx->temp->vcore;
        uint32_t stab_rem = (stab_time > s) ? (stab_time - s) : 0;
        bm.eta_sec = stab_rem + (uint32_t)bm_time + bm_future_rounds;
        if (ctx->power && (ctx->power->is_oc_fault() || ctx->power->is_ot_fault())) {
            LOG_W("[BM] OC/OT fault during stabilization — aborting round.");
            oc_ot_abort = true;
            fault_tripped = true;
            break;
        }
        delay(1000);
    }

    uint32_t total_samples = bm_time / smp_intv;
    if (total_samples == 0) total_samples = 1;

    double hr_sum = 0, eff_sum = 0, pwr_sum = 0, at_sum = 0, vt_sum = 0;
    uint32_t sample_cnt = 0;
    uint8_t  zero_hr_cnt = 0;
    // Worst-case values seen during the accepted samples. Qualification MUST use
    // peaks, not averages: a point whose average sits inside the limit while its
    // peak crosses it is exactly the "it tripped even though the number looked
    // fine" case. NAN/absent telemetry leaves these at 0.
    double iout_max = 0, pwr_max = 0, at_max = 0, vt_max = 0;

    bm.in_stab       = false;
    bm.phase_total   = bm_time;
    bm.phase_elapsed = 0;
    bm.eta_sec       = (uint32_t)bm_time + bm_future_rounds;

    uint32_t small_cores = ctx->miner ? ctx->miner->get_asic_small_cores() : 0;
    uint8_t  asic_count  = ctx->miner ? ctx->miner->get_asic_count()       : 0;
    double   exp_hr_ghs  = (double)cur_freq * ((double)(small_cores * asic_count) / 1000.0);

    LOG_W("[BM] Expected HR: %.1f GH/s (freq=%d small_cores=%d asic_cnt=%d)",
          exp_hr_ghs, cur_freq, small_cores, asic_count);

    if (!oc_ot_abort) {
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

        // Immediate abort on OC/OT hardware fault — unsafe (freq, vcore) pair.
        if (ctx->power && (ctx->power->is_oc_fault() || ctx->power->is_ot_fault())) {
            LOG_W("[BM] OC/OT fault during sampling (sample %u/%u) — aborting round.",
                  (unsigned)(i + 1), (unsigned)total_samples);
            sample_cnt = 0; // discard all partial samples
            fault_tripped = true;
            break;
        }

        if (zero_hr_cnt >= 5) {
            LOG_W("[BM] Zero hashrate for 5 samples, aborting round.");
            break;
        }

        // Peak tracking (accepted samples only — the aborts above are excluded).
        if (pwr_w > pwr_max) pwr_max = pwr_w;
        if (at    > at_max)  at_max  = at;
        if (ctx->temp->vcore > vt_max) vt_max = ctx->temp->vcore;
        if (ctx->power) {
            float io = ctx->power->get_iout_amps();
            if (io == io && io > iout_max) iout_max = io;   // NAN-safe
        }

        hr_sum  += hr_ghs;
        eff_sum += (hr_ghs > 0 && pwr_w > 0) ? (pwr_w / (hr_ghs / 1000.0)) : 0;
        pwr_sum += pwr_w;
        at_sum  += at;
        vt_sum  += ctx->temp->vcore;
        sample_cnt++;

        double hr_avg = hr_sum / sample_cnt;
        uint32_t remaining = (total_samples - i - 1) * smp_intv;
        LOG_W("[BM] [%3lus] %2.0f%% | HR:%.1fGH/s EXP:%.0fGH/s | AT:%.1fC VT:%.1fC | Vcore:%dmV | F:%dMHz | Pwr:%.1fW",
              (unsigned long)remaining, 100.0 * (i + 1) / total_samples,
              hr_ghs, exp_hr_ghs, at, ctx->temp->vcore, ctx->pwr->vcore, cur_freq, pwr_w);

        if (sample_cnt >= total_samples / 2 && exp_hr_ghs > 0 && hr_avg < exp_hr_ghs * 0.5) {
            LOG_W("[BM] Avg HR too low (%.1f < 50%% of %.1f), aborting round early.", hr_avg, exp_hr_ghs);
            break;
        }
    }
    } // if (!oc_ot_abort)
    double hr_avg  = (sample_cnt > 0) ? (hr_sum  / sample_cnt) : 0;
    double eff_avg = (sample_cnt > 0) ? (eff_sum / sample_cnt) : 0;
    double pwr_avg = (sample_cnt > 0) ? (pwr_sum / sample_cnt) : 0;
    double at_avg  = (sample_cnt > 0) ? (at_sum  / sample_cnt) : 0;
    double vt_avg  = (sample_cnt > 0) ? (vt_sum  / sample_cnt) : 0;

    // Stability gate: measured hashrate must reach this fraction of the theoretical
    // value (freq x small_cores x asic_count). This used to be 0.99, which was too
    // tight in the field: spot checks on Gamma and QAxe++ showed many perfectly
    // healthy rounds landing at 98.6-98.9%, so they were thrown away and Turbo ended
    // up with no candidates at all ("ran a benchmark, still no Turbo"). 0.98 still
    // catches genuinely failed rounds - a hung core group shows up as a much larger
    // drop - while tolerating normal sampling noise on the 3-minute average.
    constexpr double BM_STABLE_RATIO = 0.98;
    bool stable = (exp_hr_ghs > 0) && (hr_avg >= exp_hr_ghs * BM_STABLE_RATIO);
    LOG_W("[BM] Round %s | avg HR:%.1fGH/s exp:%.1fGH/s (%.2f%%, gate %.0f%%) | eff:%.3fJ/TH | pwr:%.2fW | asicT:%.1fC vcoreT:%.1fC",
          stable ? "STABLE" : "UNSTABLE", hr_avg, exp_hr_ghs,
          (exp_hr_ghs > 0) ? (100.0 * hr_avg / exp_hr_ghs) : 0.0, BM_STABLE_RATIO * 100.0,
          eff_avg, pwr_avg, at_avg, vt_avg);

    // ── Safety-margin qualification ───────────────────────────────────────────
    // "Stable" only means the ASIC reached its expected hashrate; it says nothing
    // about electrical/thermal headroom. A point can be perfectly stable AND sit
    // right on top of the regulator trip point — that is the Turbo complaint.
    //
    // The rule is deliberately capability-based, because the boards do not all
    // expose the same telemetry:
    //   - TPS546D24A (Nexus) / TPS53647 (QAxe++): real PMBus OC status + READ_IOUT
    //     -> require peak current below the chip's OC_WARN threshold.
    //   - TPS53355 (NMAxe / NMAxeGamma): no PMBus status at all (is_oc_fault() is a
    //     base-class false), so current margin CANNOT be evaluated. Fall back to
    //     peak input power against the board's power ceiling. Temperature is the
    //     only shared signal, so it applies everywhere.
    constexpr float BM_TEMP_MARGIN_C = 5.0f;
    bool  qualified = true;
    const char* q_reason = "ok";
    if (fault_tripped) {
        // A round that tripped has no valid measurements (sampling is discarded),
        // so evaluating the margin rule against zeros would report "qualified=1"
        // right before the tombstone says FAILED. The trip IS the verdict.
        qualified = false;
        q_reason = "protection tripped";
    } else {
        float oc_warn = ctx->power ? ctx->power->get_oc_warn_limit() : NAN;
        if (oc_warn == oc_warn && oc_warn > 0.0f) {
            if (iout_max > oc_warn) {
                qualified = false; q_reason = "peak current above OC warn limit";
            }
        } else if (bspec) {
            float pwr_ceiling = bspec->ui.dashboard_page.power.power.max;
            if (pwr_ceiling > 1.0f && pwr_max > pwr_ceiling) {
                qualified = false; q_reason = "peak power above board ceiling";
            }
        }
        if (qualified && bspec && bspec->asic.temp_limit.high > 0.0f &&
            at_max > (double)bspec->asic.temp_limit.high - BM_TEMP_MARGIN_C) {
            qualified = false; q_reason = "ASIC temperature within margin of limit";
        }
        // Only evaluated when the board actually reports a Vcore temperature;
        // boards without that sensor leave vt_max at 0.
        if (qualified && bspec && bspec->pwr.temp_limit.high > 0.0f && vt_max > 0.0 &&
            vt_max > (double)bspec->pwr.temp_limit.high - BM_TEMP_MARGIN_C) {
            qualified = false; q_reason = "Vcore temperature within margin of limit";
        }
    }
    if (fault_tripped) {
        LOG_W("[BM] Round tripped protection — no valid peak data, verdict=FAILED.");
    } else {
        LOG_W("[BM] Peaks: IOUTmax=%.2fA PWRmax=%.2fW ATmax=%.1fC VTmax=%.1fC -> qualified=%d (%s)",
              iout_max, pwr_max, at_max, vt_max, (int)qualified, q_reason);
    }

    // A tripped point must INVALIDATE the stored result for this frequency, not
    // merely skip writing. Results are upserted by freq alone, so skipping leaves
    // an older "stable" entry alive — and that entry can still be the highest
    // avgHR in the table, i.e. it would be picked as Turbo even though this exact
    // point now trips. Write an explicit tombstone instead.
    if (fault_tripped) {
        char tomb[512];
        snprintf(tomb, sizeof(tomb),
            "{\"freq\":%d,\"vcore\":%d,\"expHR\":%.1f,\"avgHR\":0.0,\"avgAsicTemp\":%.1f,\"avgVcoreTemp\":%.1f,"
            "\"effJTH\":0.0,\"avgPwr\":0.0,\"maxIout\":%.2f,\"maxPwr\":%.2f,\"maxAsicTemp\":%.1f,\"maxVcoreTemp\":%.1f,"
            "\"q\":0,\"fail\":1,\"ts\":%ld}",
            cur_freq, cur_vcore, exp_hr_ghs, at_avg, vt_avg,
            iout_max, pwr_max, at_max, vt_max, (long)time(nullptr));
        esp_err_t terr = bm_result_append(tomb);
        LOG_W("[BM] Protection tripped at %dMHz/%dmV — stored as FAILED (invalidates any earlier result)%s",
              cur_freq, cur_vcore, (terr == ESP_OK) ? "." : " (NVS write failed)");
    }

    // Lambda: select best avgHR result from NVS and apply to Normal mode, then reboot.
    auto finish_and_reboot = [&](const char* reason) {
        bm.active = false;
        nvs_config_set_u8(NVS_CONFIG_BM_MODE, 0);
        nvs_config_set_u16(NVS_CONFIG_BM_CUR_FREQ,  freq_min);
        nvs_config_set_u16(NVS_CONFIG_BM_CUR_VCORE, vcore_min);
        {
            char* res_str = bm_result_read_all();
            BasicJsonDocument<PsramJsonAllocator> rdoc(32768);  // ring cap is 80 entries (~11KB JSON)
            DeserializationError rerr = deserializeJson(rdoc, res_str);
            free(res_str);
            uint16_t best_freq = 0, best_vcore = 0;
            float    best_avgHR = -1.0f;
            if (!rerr && rdoc.is<JsonArray>()) {
                for (JsonObject e : rdoc.as<JsonArray>()) {
                    // Only points that passed the safety margin may be auto-applied.
                    // Entries without "q" predate the margin rule and are treated as
                    // NOT qualified — assuming an unverified point is safe is the
                    // failure mode this whole check exists to prevent.
                    uint8_t fail = e["fail"] | (uint8_t)0;
                    int     q    = e["q"]    | 0;
                    if (fail || q != 1) continue;
                    float avgHR = e["avgHR"] | 0.0f;
                    if (avgHR > best_avgHR) {
                        best_avgHR = avgHR;
                        best_freq  = e["freq"]  | (uint16_t)0;
                        best_vcore = e["vcore"] | (uint16_t)0;
                    }
                }
            }
            if (best_freq > 0 && best_vcore > 0) {
                LOG_W("[BM] Best qualified result: freq=%dMHz vcore=%dmV avgHR=%.1fGH/s — applying.",
                      best_freq, best_vcore, best_avgHR);
                nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ,    best_freq);
                nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, best_vcore);
            } else {
                LOG_W("[BM] No qualified result (all failed, unverified, or none stored) — "
                      "keeping current settings rather than applying an unverified point.");
            }
        }
        {
            uint32_t _start = nvs_config_get_u32(NVS_CONFIG_BM_START_TS, 0);
            if (_start > 0) nvs_config_set_u32(NVS_CONFIG_BM_TOTAL_SEC, (uint32_t)time(nullptr) - _start);
        }
        reboot_or_defer_to_ota(reason);
        vTaskDelete(NULL);
    };

    if (stable) {
        char entry[512];
        snprintf(entry, sizeof(entry),
            "{\"freq\":%d,\"vcore\":%d,\"expHR\":%.1f,\"avgHR\":%.1f,\"avgAsicTemp\":%.1f,\"avgVcoreTemp\":%.1f,"
            "\"effJTH\":%.3f,\"avgPwr\":%.2f,\"maxIout\":%.2f,\"maxPwr\":%.2f,\"maxAsicTemp\":%.1f,\"maxVcoreTemp\":%.1f,"
            "\"q\":%d,\"fail\":0,\"ts\":%ld}",
            cur_freq, cur_vcore, exp_hr_ghs, hr_avg, at_avg, vt_avg,
            eff_avg, pwr_avg, iout_max, pwr_max, at_max, vt_max,
            qualified ? 1 : 0, (long)time(nullptr));

        // Ring storage: capacity bounded by partition size (80 entries), oldest evicted when full
        esp_err_t nvs_write_err = bm_result_append(entry);
        if (nvs_write_err == ESP_OK) LOG_W("[BM] Result saved: %s", entry);
        else LOG_E("[BM] NVS write failed (%s), result not saved.", esp_err_to_name(nvs_write_err));

        cur_freq  += freq_step;
        cur_vcore  = (cur_vcore > vcore_min + vcore_step) ? (cur_vcore - vcore_step) : vcore_min;

        if (cur_freq > freq_max) {
            LOG_W("[BM] All frequencies tested — benchmark complete, applying best result.");
            finish_and_reboot("benchmark complete, switching to Normal mode with best params");
            return;
        }
    } else if (fault_tripped) {
        // Raising vcore after an overcurrent trip pushes more power into the fault —
        // strictly the wrong direction. The tombstone above already invalidated this
        // frequency, so skip straight to the next one instead of retrying vcores.
        LOG_W("[BM] freq=%dMHz tripped protection — skipping remaining vcores for this frequency.", cur_freq);
        cur_freq  += freq_step;
        cur_vcore  = vcore_min;
        if (cur_freq > freq_max) {
            LOG_W("[BM] All frequencies exhausted (with protection trips) — benchmark complete.");
            finish_and_reboot("benchmark complete (protection trips), applying best qualified point");
            return;
        }
    } else {
        cur_vcore += vcore_step;
        if (cur_vcore > vcore_max) {
            LOG_W("[BM] freq=%dMHz unstable at all vcores, skipping to next freq.", cur_freq);
            cur_freq  += freq_step;
            cur_vcore  = vcore_min;
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
    reboot_or_defer_to_ota("benchmark round complete, rebooting for next (freq,vcore)");
    vTaskDelete(NULL);
}
