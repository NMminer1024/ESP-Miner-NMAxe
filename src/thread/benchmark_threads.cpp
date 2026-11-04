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


// ── Benchmark: auto frequency/vcore sweep ────────────────────────────────────
// Resident thread. Rounds advance by HOT-SWITCHING freq/vcore in place (no
// per-round reboot): the ASIC PLL is re-locked via request_asic_frequency()
// and the rail is re-targeted through spec.asic.req_vcore (the power loop's
// line-loss compensation follows on its own). NVS bm_cur_* is still written
// after every round, but only as a checkpoint so a power loss / watchdog /
// OTA reboot resumes the sweep from the last untested point (the boot-time
// override in application.cpp applies it before the miner is constructed).
//
// The thread never exits: bm_mode==0 -> idle-poll; bm_mode==1 -> run rounds
// until the sweep completes, then hot-apply the best qualified point.

struct BmSweepConfig {
    uint16_t freq_min, freq_max, freq_step;
    uint16_t vcore_min, vcore_max, vcore_step;
    uint8_t  smp_intv;
    uint16_t bm_time, stab_time;
};

static void bm_load_config(BenchmarkCtx* ctx, BmSweepConfig& c) {
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

    c.freq_min   = nvs_config_get_u16(NVS_CONFIG_BM_FREQ_MIN,   freq_min_def);
    c.freq_max   = nvs_config_get_u16(NVS_CONFIG_BM_FREQ_MAX,   freq_max_def);
    c.freq_step  = nvs_config_get_u16(NVS_CONFIG_BM_FREQ_STEP,  freq_step_def);
    c.vcore_min  = nvs_config_get_u16(NVS_CONFIG_BM_VCORE_MIN,  vcore_min_def);
    c.vcore_max  = nvs_config_get_u16(NVS_CONFIG_BM_VCORE_MAX,  vcore_max_def);
    c.vcore_step = nvs_config_get_u16(NVS_CONFIG_BM_VCORE_STEP, vcore_step_def);
    c.smp_intv   = nvs_config_get_u8 (NVS_CONFIG_BM_SAMPLE_INTV, 2);
    c.bm_time    = nvs_config_get_u16(NVS_CONFIG_BM_TIME,        120);
    c.stab_time  = nvs_config_get_u16(NVS_CONFIG_BM_STAB_TIME,   30);
}

// Hot-switch the operating point. Vcore moves first (the rail must be at
// target before the PLL changes — same order as the web/apply hot path), then
// the frequency request is queued for the mining thread and awaited.
// spec.asic.req_frq is synced BEFORE the request so the warmup logic cannot
// re-request the previous frequency in between.
// Returns false when the frequency could not be queued or the PLL did not
// reach the target in time — the caller treats the round as unstable.
static bool bm_apply_point(BenchmarkCtx* ctx, uint16_t freq, uint16_t vcore) {
    BoardSpecConfig* spec = ctx->spec;

    if (spec && spec->asic.req_vcore != vcore) {
        spec->asic.req_vcore = vcore;
        if (ctx->power) ctx->power->set_vcore_voltage(vcore);
        delay(1500);   // rail slew + first power-loop correction pass
    }

    if (!ctx->miner) return false;
    if (ctx->miner->get_asic_frequency_current() == freq) {
        if (spec) spec->asic.req_frq = freq;
        return true;
    }
    if (spec) spec->asic.req_frq = freq;

    bool queued = false;
    for (int attempt = 0; attempt < 5 && !queued; attempt++) {
        queued = ctx->miner->request_asic_frequency(freq);
        if (!queued) delay(200);
    }
    if (!queued) {
        LOG_E("[BM] Frequency request %uMHz could not be queued", freq);
        return false;
    }
    const uint32_t t0 = millis();
    while (true) {
        if (!ctx->miner->is_asic_frequency_updating() &&
            ctx->miner->get_asic_frequency_current() == freq) return true;
        // Fast-fail: the request was consumed (no longer pending), the applier
        // is idle, and the current freq still isn't the target — the PLL
        // rejected it (e.g. off the 6.25MHz grid). No point waiting longer.
        if (!ctx->miner->is_asic_frequency_update_pending() &&
            !ctx->miner->is_asic_frequency_updating() &&
            ctx->miner->get_asic_frequency_current() != freq &&
            millis() - t0 > 3000) {   // grace: let the mining thread pick it up first
            LOG_E("[BM] PLL rejected %uMHz (still %uMHz) — skipping point",
                  freq, ctx->miner->get_asic_frequency_current());
            return false;
        }
        if (millis() - t0 > 15000) {
            LOG_E("[BM] PLL did not reach %uMHz within 15s (still %uMHz)",
                  freq, ctx->miner->get_asic_frequency_current());
            return false;
        }
        delay(250);
    }
}

// Return to the persisted Normal-mode operating point. The sweep never writes
// NVS_CONFIG_ASIC_FREQ/VOLTAGE (only finish/apply do), so these still hold the
// user's pre-sweep settings.
static void bm_restore_normal_point(BenchmarkCtx* ctx) {
    uint16_t nf = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ,
                                     ctx->spec ? ctx->spec->asic.default_frq   : 0);
    uint16_t nv = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE,
                                     ctx->spec ? ctx->spec->asic.default_vcore : 0);
    if (nf == 0 || nv == 0) return;
    LOG_W("[BM] Restoring Normal-mode operating point: freq=%dMHz vcore=%dmV", nf, nv);
    bm_apply_point(ctx, nf, nv);
}

// Select the best qualified result from NVS, apply it to Normal mode (hot, no
// reboot) and end the sweep. Without a qualified point, keep the user's
// pre-sweep settings instead of applying an unverified one.
static void bm_finish(BenchmarkCtx* ctx, const char* reason) {
    BenchmarkState& bm = *ctx->bm;
    bm.active = false;

    BmSweepConfig c;
    bm_load_config(ctx, c);
    nvs_config_set_u16(NVS_CONFIG_BM_CUR_FREQ,  c.freq_min);
    nvs_config_set_u16(NVS_CONFIG_BM_CUR_VCORE, c.vcore_min);

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
            bm_apply_point(ctx, best_freq, best_vcore);
        } else {
            LOG_W("[BM] No qualified result (all failed, unverified, or none stored) — "
                  "restoring pre-sweep settings rather than applying an unverified point.");
            bm_restore_normal_point(ctx);
        }
    }
    {
        uint32_t _start = nvs_config_get_u32(NVS_CONFIG_BM_START_TS, 0);
        if (_start > 0) nvs_config_set_u32(NVS_CONFIG_BM_TOTAL_SEC, (uint32_t)time(nullptr) - _start);
    }
    LOG_W("[BM] Sweep finished (%s) — back to Normal mode without reboot.", reason);
    nvs_config_set_u8(NVS_CONFIG_BM_MODE, 0);
    *ctx->bm_mode = 0;
}

// Run rounds until the sweep completes (bm_finish) or bm_mode is cleared
// externally (stop/apply/reset) — in that case abandon the current round and
// restore the Normal-mode point.
static void bm_run_sweep(BenchmarkCtx* ctx) {
    BenchmarkState& bm = *ctx->bm;

    xEventGroupWaitBits(ctx->init_evt, INIT_EVENT_MINER_READY, pdFALSE, pdTRUE, portMAX_DELAY);
    if (*ctx->bm_mode != 1) return;

    while (*ctx->bm_mode == 1) {
        // Config and the current point are re-read EVERY round (the legacy
        // per-reboot design did the same on every boot), so a PATCH or a fresh
        // start issued mid-sweep takes effect from the next round.
        BmSweepConfig c;
        bm_load_config(ctx, c);
        const uint16_t freq_min   = c.freq_min,  freq_max   = c.freq_max,  freq_step  = c.freq_step;
        const uint16_t vcore_min  = c.vcore_min, vcore_max  = c.vcore_max, vcore_step = c.vcore_step;
        const uint8_t  smp_intv   = c.smp_intv;
        const uint16_t bm_time    = c.bm_time,   stab_time  = c.stab_time;

        uint16_t cur_freq   = nvs_config_get_u16(NVS_CONFIG_BM_CUR_FREQ,  freq_min);
        uint16_t cur_vcore  = nvs_config_get_u16(NVS_CONFIG_BM_CUR_VCORE, vcore_min);

        LOG_W("[BM] === Benchmark round: freq=%d vcore=%d ===", cur_freq, cur_vcore);
        LOG_W("[BM] Range freq %d~%dMHz step=%d, vcore %d~%dmV step=%d",
              freq_min, freq_max, freq_step, vcore_min, vcore_max, vcore_step);
        LOG_W("[BM] stab=%ds bm=%ds smp=%ds", stab_time, bm_time, smp_intv);

        // Hot-switch to this round's point. When the PLL refuses the target the
        // ASIC keeps mining at the PREVIOUS frequency — measuring anyway could
        // record a "stable" entry labelled with the wrong freq (e.g. a failed
        // switch-down still hashing at the higher rate). Skip the round and
        // advance as unstable instead; nothing is recorded.
        if (!bm_apply_point(ctx, cur_freq, cur_vcore)) {
            LOG_W("[BM] Switch to %dMHz/%dmV failed — advancing as unstable (no measurement).",
                  cur_freq, cur_vcore);
            cur_vcore += vcore_step;
            if (cur_vcore > vcore_max) {
                cur_freq  += freq_step;
                cur_vcore  = vcore_min;
            }
            if (cur_freq > freq_max) {
                LOG_W("[BM] All frequencies exhausted — benchmark complete, applying best result.");
                bm_finish(ctx, "all frequencies exhausted (switch failures)");
                return;
            }
            nvs_config_set_u16(NVS_CONFIG_BM_CUR_FREQ,  cur_freq);
            nvs_config_set_u16(NVS_CONFIG_BM_CUR_VCORE, cur_vcore);
            continue;
        }

        if (*ctx->bm_mode != 1) break;   // stopped while switching

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

        // Start every round from a clean status slate.
        //
        // The PMBus OC/OT status bits (STATUS_IOUT bit7 / STATUS_TEMPERATURE bit7) are
        // STICKY: once set they stay set until CLEAR_FAULTS or a power cycle. This code
        // only ever READ them, so a bit latched by an earlier test — or by a previous
        // sweep point at a more aggressive setting — aborted the next round immediately
        // and, because a tripped round now writes a destructive FAILED tombstone, that
        // permanently invalidated a perfectly good frequency. Observed on Nexus: a round
        // at 775MHz/1175mV reported fail=1 while every measured peak was comfortably
        // inside its limit (phase 20.1A/22.5A, input 13.3A/14.25A, Vcore 67C/115C).
        //
        // Anything that latches DURING the round is still caught by the loops below, so
        // this only discards state that describes a different operating point.
        if (ctx->power) {
            const bool stale_oc = ctx->power->is_oc_fault();
            const bool stale_ot = ctx->power->is_ot_fault();
            if (stale_oc || stale_ot) {
                LOG_W("[BM] Clearing stale latched fault before round (OC=%d OT=%d) — "
                      "carried over from an earlier test, not caused by this point.",
                      (int)stale_oc, (int)stale_ot);
            }
            ctx->power->clear_faults();
            delay(20);   // let the regulator finish clearing its status registers
        }

        bool aborted = false;
        bool oc_ot_abort = false;
        // Set ONLY when the regulator actually tripped (OC/OT), as opposed to an
        // unstable/zero-hashrate round. The distinction drives two behaviours:
        //   - the point is stored as an explicit failure (tombstone), and
        //   - the sweep skips the remaining vcores for this frequency instead of
        //     retrying higher (raising vcore after an OC trip feeds the fault).
        bool fault_tripped = false;
        for (uint16_t s = 0; s < stab_time; s++) {
            if (*ctx->bm_mode != 1) { aborted = true; break; }
            bm.phase_elapsed = s;
            bm.asic_temp     = ctx->temp->asic;
            bm.vcore_temp    = ctx->temp->vcore;
            uint32_t stab_rem = (stab_time > s) ? (stab_time - s) : 0;
            bm.eta_sec = stab_rem + (uint32_t)bm_time + bm_future_rounds;
            if (ctx->power && (ctx->power->is_oc_fault() || ctx->power->is_ot_fault())) {
                // Name the actual fault. "OC/OT fault" alone cannot be diagnosed: an
                // OC trip at a safe current reading means a transient per-phase spike,
                // while an OT trip points at the VRM thermals — completely different
                // follow-ups.
                const bool oc = ctx->power->is_oc_fault();
                const bool ot = ctx->power->is_ot_fault();
                LOG_W("[BM] Fault during stabilization (OC=%d OT=%d) at %dMHz/%dmV — aborting round. "
                      "Vcore=%.1fC, input=%.2fA/%.1fW",
                      (int)oc, (int)ot, cur_freq, cur_vcore,
                      ctx->temp ? (double)ctx->temp->vcore : 0.0,
                      ctx->pwr ? (double)(ctx->pwr->ibus / 1000.0) : 0.0,
                      ctx->pwr ? (double)(ctx->pwr->vbus * ctx->pwr->ibus / 1000000.0) : 0.0);
                oc_ot_abort = true;
                fault_tripped = true;
                break;
            }
            delay(1000);
        }

        if (aborted) break;

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
        // Per-phase peaks. The regulator divides the stack-total OC limit across the
        // phases, so the comparator trips on (limit / phases) for the HEAVIEST phase.
        // Qualifying on the total alone lets an imbalanced stack pass the margin rule
        // while sitting right on the per-phase trip point. phase_cnt stays 0 on boards
        // without per-phase telemetry (TPS53355 / TPS53647), where the rule falls back
        // to the total.
        double  iout_phase_max = 0;
        int8_t  phase_cnt = 0;
        float   phase_min_at_max = NAN;   // lightest phase at the same instant, for imbalance
        // Input side. The adapter rating is a CURRENT limit in practice: 180 W at a
        // sagging 11.80 V rail needs 15.25 A, so wattage alone reads "96 %" while the
        // current is at 98 % of 15 A. Both peaks are kept, current is the primary test.
        double  ibus_max = 0;             // A  (input)
        double  pwr_max_in = 0;           // W  (input, = vbus * ibus)
        {
            int8_t hp = ctx->power ? ctx->power->detect_phase() : -1;
            if (hp > 0) phase_cnt = hp;
        }

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
            for (uint8_t s = 0; s < smp_intv; s++) {
                if (*ctx->bm_mode != 1) { aborted = true; break; }
                delay(1000);
            }
            if (aborted) break;

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
                const bool oc = ctx->power->is_oc_fault();
                const bool ot = ctx->power->is_ot_fault();
                LOG_W("[BM] Fault during sampling (OC=%d OT=%d) at sample %u/%u — aborting round. "
                      "peaks so far: phase=%.2fA total=%.2fA input=%.2fA/%.1fW Vcore=%.1fC",
                      (int)oc, (int)ot, (unsigned)(i + 1), (unsigned)total_samples,
                      iout_phase_max, iout_max, (double)ibus_max, pwr_max_in,
                      ctx->temp ? (double)vt_max : 0.0);
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
            if (pwr_w > pwr_max_in) pwr_max_in = pwr_w;
            if (ibus  > ibus_max)  ibus_max  = ibus;
            if (at    > at_max)  at_max  = at;
            if (ctx->temp->vcore > vt_max) vt_max = ctx->temp->vcore;
            if (ctx->power) {
                float io = ctx->power->get_iout_amps();
                if (io == io && io > iout_max) iout_max = io;   // NAN-safe
                // Per-phase peaks: once per sample interval (smp_intv seconds), which
                // keeps the select/read/restore cost negligible for a benchmark run.
                if (phase_cnt > 0) {
                    float worst = NAN, lightest = NAN;
                    bool  any = false;
                    for (int8_t p = 0; p < phase_cnt; p++) {
                        float a = NAN;
                        if (ctx->power->read_phase_iout_amps((uint8_t)p, &a) != 0) continue;
                        if (a != a) continue;
                        any = true;
                        if (!(worst == worst) || a > worst) worst = a;
                        if (!(lightest == lightest) || a < lightest) lightest = a;
                    }
                    if (any && worst == worst) {
                        if (worst > iout_phase_max) {
                            iout_phase_max = worst;
                            phase_min_at_max = lightest;
                        }
                    }
                }
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

        if (aborted) break;

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
        // Fraction of the bundled adapter's rating a 24/7 point may use. The remaining
        // headroom absorbs rail sag (Vbus drops as load rises, which raises the current
        // for the same wattage) and slow thermal drift in the adapter.
        constexpr float BM_ADAPTER_BUDGET = 0.95f;
        bool  qualified = true;
        const char* q_reason = "ok";
        if (fault_tripped) {
            // A round that tripped has no valid measurements (sampling is discarded),
            // so evaluating the margin rule against zeros would report "qualified=1"
            // right before the tombstone says FAILED. The trip IS the verdict.
            qualified = false;
            q_reason = "protection tripped";
        } else {
            const BoardSpecConfig* bspec = ctx->spec;
            float oc_warn = ctx->power ? ctx->power->get_oc_warn_limit() : NAN;
            if (oc_warn == oc_warn && oc_warn > 0.0f) {
                // Prefer the PER-PHASE comparison when the board reports per-phase
                // current: the controller splits the stack-total limit across phases
                // and trips on the heaviest one, so an imbalanced stack reaches the
                // per-phase trip point while the total still looks comfortable. Only
                // fall back to the total when no per-phase data is available.
                if (phase_cnt > 0 && iout_phase_max > 0.0) {
                    float plim = oc_warn / (float)phase_cnt;
                    if (iout_phase_max > plim) {
                        qualified = false; q_reason = "peak phase current above per-phase OC warn limit";
                    }
                } else if (iout_max > oc_warn) {
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
            // ── Bundled adapter budget ────────────────────────────────────────────
            // Last gate, and the one that actually decides whether a Turbo point may
            // be run 24/7 on the stock PSU. Checked against the CURRENT first: at a
            // sagging rail the wattage figure understates the draw.
            //
            // Budget is 95 %, not 100 %: the margin has to absorb rail sag and slow
            // temperature drift. Measured reference on Nexus — 775MHz/1175mV drew
            // 13.5 A / 160 W (90 % of 15 A, 89 % of 180 W) and qualifies; 785MHz/1210mV
            // drew 14.71 A / 173.5 W (98 %, 96 %) and does not.
            if (qualified && bspec && bspec->pwr.adapter_max_a > 0.0f) {
                const float budget_a = bspec->pwr.adapter_max_a * BM_ADAPTER_BUDGET;
                if (ibus_max > budget_a) {
                    qualified = false;
                    q_reason = "peak input current above adapter budget";
                }
            }
            if (qualified && bspec && bspec->pwr.adapter_max_w > 0.0f) {
                const float budget_w = bspec->pwr.adapter_max_w * BM_ADAPTER_BUDGET;
                if (pwr_max_in > budget_w) {
                    qualified = false;
                    q_reason = "peak input power above adapter budget";
                }
            }
        }
        if (fault_tripped) {
            LOG_W("[BM] Round tripped protection — no valid peak data, verdict=FAILED.");
        } else if (phase_cnt > 0 && iout_phase_max > 0.0) {
            float oc_warn = ctx->power ? ctx->power->get_oc_warn_limit() : NAN;
            float plim = (oc_warn == oc_warn && oc_warn > 0.0f) ? (oc_warn / (float)phase_cnt) : NAN;
            // Imbalance at the worst instant: how far the heaviest phase sat above the
            // average of all phases. This is what erodes the effective total headroom
            // (a +7 % imbalance turns a 70.5 A stack limit into ~66 A of usable total).
            float imbalance_pct = NAN;
            if (iout_phase_max > 0.0 && phase_min_at_max == phase_min_at_max) {
                const float avg_phase = (iout_phase_max + phase_min_at_max) * 0.5f;
                if (avg_phase > 0.0f) imbalance_pct = 100.0f * (iout_phase_max - avg_phase) / avg_phase;
            }
            LOG_W("[BM] Peaks: phase-max=%.2fA/%.1fA-per-phase (IOUT total max=%.2fA, %d phases, imbalance %.1f%%) "
                  "PWRmax=%.2fW ATmax=%.1fC VTmax=%.1fC -> qualified=%d (%s)",
                  iout_phase_max, plim, iout_max, (int)phase_cnt, imbalance_pct,
                  pwr_max, at_max, vt_max, (int)qualified, q_reason);
        } else {
            LOG_W("[BM] Peaks: IOUTmax=%.2fA PWRmax=%.2fW ATmax=%.1fC VTmax=%.1fC -> qualified=%d (%s)",
                  iout_max, pwr_max, at_max, vt_max, (int)qualified, q_reason);
        }
        // Adapter occupancy, on its own line so it survives log filters that match on
        // "qualified" / "Peaks". Only printed for boards that declare a budget.
        {
            const BoardSpecConfig* bspec = ctx->spec;
            if (bspec && (bspec->pwr.adapter_max_a > 0.0f || bspec->pwr.adapter_max_w > 0.0f)) {
                char ibus_pct[24] = "n/a", pwr_pct[24] = "n/a";
                if (bspec->pwr.adapter_max_a > 0.0f) {
                    snprintf(ibus_pct, sizeof(ibus_pct), "%.0f%%", 100.0 * ibus_max / bspec->pwr.adapter_max_a);
                }
                if (bspec->pwr.adapter_max_w > 0.0f) {
                    snprintf(pwr_pct, sizeof(pwr_pct), "%.0f%%", 100.0 * pwr_max_in / bspec->pwr.adapter_max_w);
                }
                LOG_W("[BM] Adapter: peak %.2fA (%s of %.0fA) / %.1fW (%s of %.0fW) — budget %.0f%%",
                      ibus_max, ibus_pct, (double)bspec->pwr.adapter_max_a,
                      pwr_max_in, pwr_pct, (double)bspec->pwr.adapter_max_w,
                      BM_ADAPTER_BUDGET * 100.0);
            }
        }

        // A tripped point must INVALIDATE the stored result for this frequency, not
        // merely skip writing. Results are upserted by freq alone, so skipping leaves
        // an older "stable" entry alive — and that entry can still be the highest
        // avgHR in the table, i.e. it would be picked as Turbo even though this exact
        // point now trips. Write an explicit tombstone instead.
        if (fault_tripped) {
            char tomb[720];
            snprintf(tomb, sizeof(tomb),
                "{\"freq\":%d,\"vcore\":%d,\"expHR\":%.1f,\"avgHR\":0.0,\"avgAsicTemp\":%.1f,\"avgVcoreTemp\":%.1f,"
                "\"effJTH\":0.0,\"avgPwr\":0.0,\"maxIout\":%.2f,\"maxPwr\":%.2f,\"maxAsicTemp\":%.1f,\"maxVcoreTemp\":%.1f,"
                "\"maxPhaseIout\":%.2f,\"phases\":%d,\"maxIbus\":%.2f,\"maxPwrIn\":%.2f,"
                "\"q\":0,\"fail\":1,\"ts\":%ld}",
                cur_freq, cur_vcore, exp_hr_ghs, at_avg, vt_avg,
                iout_max, pwr_max, at_max, vt_max,
                iout_phase_max, (int)phase_cnt, ibus_max, pwr_max_in, (long)time(nullptr));
            esp_err_t terr = bm_result_append(tomb);
            LOG_W("[BM] Protection tripped at %dMHz/%dmV — stored as FAILED (invalidates any earlier result)%s",
                  cur_freq, cur_vcore, (terr == ESP_OK) ? "." : " (NVS write failed)");
        }

        if (stable) {
            char entry[720];
            snprintf(entry, sizeof(entry),
                "{\"freq\":%d,\"vcore\":%d,\"expHR\":%.1f,\"avgHR\":%.1f,\"avgAsicTemp\":%.1f,\"avgVcoreTemp\":%.1f,"
                "\"effJTH\":%.3f,\"avgPwr\":%.2f,\"maxIout\":%.2f,\"maxPwr\":%.2f,\"maxAsicTemp\":%.1f,\"maxVcoreTemp\":%.1f,"
                "\"maxPhaseIout\":%.2f,\"phases\":%d,\"maxIbus\":%.2f,\"maxPwrIn\":%.2f,"
                "\"q\":%d,\"fail\":0,\"ts\":%ld}",
                cur_freq, cur_vcore, exp_hr_ghs, hr_avg, at_avg, vt_avg,
                eff_avg, pwr_avg, iout_max, pwr_max, at_max, vt_max,
                iout_phase_max, (int)phase_cnt, ibus_max, pwr_max_in,
                qualified ? 1 : 0, (long)time(nullptr));

            // Ring storage: capacity bounded by partition size (80 entries), oldest evicted when full
            esp_err_t nvs_write_err = bm_result_append(entry);
            if (nvs_write_err == ESP_OK) LOG_W("[BM] Result saved: %s", entry);
            else LOG_E("[BM] NVS write failed (%s), result not saved.", esp_err_to_name(nvs_write_err));

            cur_freq  += freq_step;
            cur_vcore  = (cur_vcore > vcore_min + vcore_step) ? (cur_vcore - vcore_step) : vcore_min;

            if (cur_freq > freq_max) {
                LOG_W("[BM] All frequencies tested — benchmark complete, applying best result.");
                bm_finish(ctx, "all frequencies tested");
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
                bm_finish(ctx, "all frequencies exhausted (protection trips)");
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
                    bm_finish(ctx, "all frequencies exhausted (all unstable)");
                    return;
                }
            }
        }

        nvs_config_set_u16(NVS_CONFIG_BM_CUR_FREQ,  cur_freq);
        nvs_config_set_u16(NVS_CONFIG_BM_CUR_VCORE, cur_vcore);
        LOG_W("[BM] Next round: freq=%dMHz vcore=%dmV (hot-switch, no reboot)", cur_freq, cur_vcore);
        // Loop continues: config + point are re-read from NVS, then hot-switched.
    }

    // External stop/apply/reset cleared bm_mode mid-round: abandon the sweep and
    // return the miner to the persisted Normal-mode point. When the stop came
    // from /api/benchmark/apply, that endpoint already wrote the requested point
    // into the same NVS keys, so this converges to the user's choice.
    bm.active = false;
    LOG_W("[BM] Sweep interrupted (bm_mode cleared) — restoring Normal-mode point.");
    bm_restore_normal_point(ctx);
}

void benchmark_thread_entry(void* args) {
    BenchmarkCtx* ctx = static_cast<BenchmarkCtx*>(args);

    // Resident thread: idle-poll the mode flag (flipped live by the web API,
    // no reboot involved on either edge) and run the sweep while it is set.
    while (true) {
        if (*ctx->bm_mode != 1) {
            ctx->bm->active = false;
            delay(1000);
            continue;
        }
        bm_run_sweep(ctx);
        delay(500);
    }
}
