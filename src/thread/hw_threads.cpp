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


// ── OC trip forensics: rolling total-current trace ──────────────────────────
// The customer's core question ("average reads 64 A but the chip tripped at
// 70.5 A") can only be answered with what the current was actually doing in the
// seconds before the comparator fired. READ_IOUT is the chip's own *averaged*
// view, so this trace is NOT the sub-microsecond instantaneous peak the analog
// per-phase comparator sees — it is the honest averaged trajectory, plus a
// per-phase snapshot taken at the trip. Together they show the gap between the
// averaged stack total and the per-phase peaks that actually trip it. Kept tiny
// (~1 KB) and only dumped on a real OC fault.
namespace {
    constexpr uint16_t ITRACE_N = 120;   // 120 x ~50 ms ~= 6 s window
    float    g_itr_iout[ITRACE_N];       // NAN marks a failed read (skipped in stats)
    uint32_t g_itr_tms[ITRACE_N];
    uint16_t g_itr_head  = 0;            // next write slot
    uint16_t g_itr_count = 0;            // valid samples, capped at ITRACE_N

    float itrace_sample(float iout) {
        g_itr_iout[g_itr_head] = iout;
        g_itr_tms[g_itr_head]  = millis();
        g_itr_head = (uint16_t)((g_itr_head + 1) % ITRACE_N);
        if (g_itr_count < ITRACE_N) g_itr_count++;
        return iout;
    }

    // Dump the trace + a per-phase snapshot. Called from power_loop the instant an
    // OC fault is seen, BEFORE enter_protection_hold powers Vcore down — so the
    // per-phase reads still see the live rail (the chip's own OC_FAULT_RESPONSE
    // does not shut the output down; our supervisor does, a few lines later).
    void itrace_dump(AxePowerHal* power, const char* tag) {
        if (g_itr_count == 0) return;
        const uint16_t start = (uint16_t)((g_itr_head + ITRACE_N - g_itr_count) % ITRACE_N);
        float mn = NAN, mx = NAN, sum = 0.0f;
        int   n = 0;
        for (uint16_t i = 0; i < g_itr_count; i++) {
            float v = g_itr_iout[(uint16_t)((start + i) % ITRACE_N)];
            if (v != v) continue;                 // NAN
            if (!(mn == mn) || v < mn) mn = v;
            if (!(mx == mx) || v > mx) mx = v;
            sum += v; n++;
        }
        const uint32_t span_ms = millis() - g_itr_tms[start];
        LOG_W("[OC-TRACE %s] %d samples / %lums: min=%.2fA max=%.2fA avg=%.2fA (chip OC_FAULT limit=%.1fA)",
              tag, n, (unsigned long)span_ms, mn, mx, n ? sum / n : 0.0f,
              power->get_oc_limit_amps());
        // Last 12 samples (~0.6 s up to the trip) as a compact approach list.
        {
            const uint16_t tail = g_itr_count < 12 ? g_itr_count : 12;
            char buf[12 * 8];
            int  off = 0;
            for (uint16_t i = tail; i >= 1; i--) {
                if (off >= (int)sizeof(buf) - 8) break;   // never overrun on a huge/bad value
                uint16_t idx = (uint16_t)((g_itr_head + ITRACE_N - i) % ITRACE_N);
                float v = g_itr_iout[idx];
                off += snprintf(buf + off, sizeof(buf) - off, "%s%.1f",
                                (i == tail ? "" : " "), (v == v) ? v : 0.0f);
            }
            LOG_W("[OC-TRACE %s] last %u x ~50ms (A): %s | TRIP", tag, (unsigned)tail, buf);
        }
        // Per-phase snapshot at the trip (authoritative runtime phase count).
        // Timing caveat: the chip's analog comparator + OC_FAULT_RESPONSE shut the
        // output down in microseconds, far faster than this 50 ms poll can detect
        // the sticky bit — so on a slow soft-start ramp these reads usually land
        // AFTER the collapse (all ~0 A), which itself confirms the chip already
        // auto-reacted. On a sudden in-load OC event the first detection poll can
        // still catch a live per-phase split before the response fully settles.
        int8_t ph = power->detect_phase();
        if (ph > 0) {
            for (int8_t p = 0; p < ph; p++) {
                float pa = NAN;
                if (power->read_phase_iout_amps((uint8_t)p, &pa) == 0) {
                    LOG_W("[OC-TRACE %s] phase %d IOUT=%.2fA", tag, (int)p, pa);
                } else {
                    LOG_W("[OC-TRACE %s] phase %d IOUT unavailable", tag, (int)p);
                }
            }
        }
        // Reset the window after dumping. Without this, a second trip whose
        // pre-trip interval had sampling paused (protection hold keeps Vcore off)
        // would re-dump the PREVIOUS trip's samples with a stretched span — the
        // stale-data bug seen when forcing two trips back to back. Each trip must
        // report only its own approach window; an empty one honestly shows 0.
        g_itr_head  = 0;
        g_itr_count = 0;
    }
}


// ── Per-phase current snapshot (throttled) ──────────────────────────────────
// IOUT_OC_WARN/FAULT limits are written with PHASE=0xFF, i.e. as a STACK TOTAL,
// and the controller divides them across the phases. The comparator that trips
// therefore works on (limit / num_phases) of the HEAVIEST phase, not on the
// total. Measured on Nexus at 800MHz/1225mV: total read 67.2 A (limit 70.5 A)
// while phase 1 was 23.7 A against a 23.5 A per-phase trip point — a 6.9 %
// imbalance turned a "70.5 A" limit into an effective ~66 A one. Comparing the
// TOTAL against the total limit therefore reported those warnings as "latched
// (stale)" moments before the unit actually tripped.
namespace {
    constexpr uint32_t PHASE_SNAP_INTERVAL_MS = 2000;

    struct PhaseSnapshot {
        uint32_t ts_ms   = 0;
        int8_t   phases  = 0;     // 0 = not probed yet, <0 = board has no per-phase telemetry
        float    worst_a = NAN;   // heaviest phase
        int8_t   worst_i = -1;
        float    total_a = NAN;   // sum of the sampled phases
    };
    PhaseSnapshot g_phase_snap;

    // Refreshed at most every PHASE_SNAP_INTERVAL_MS: each phase costs three I2C
    // transactions (select/read/restore), so this must never run at loop rate.
    // `force` is used right before a trip dump, where accuracy beats cost.
    const PhaseSnapshot& phase_snapshot(AxePowerHal* power, bool force) {
        const uint32_t now = millis();
        if (!force && g_phase_snap.ts_ms != 0 &&
            (uint32_t)(now - g_phase_snap.ts_ms) < PHASE_SNAP_INTERVAL_MS) {
            return g_phase_snap;
        }
        if (g_phase_snap.phases == 0) g_phase_snap.phases = power->detect_phase();
        if (g_phase_snap.phases <= 0) {
            g_phase_snap.ts_ms = now;
            return g_phase_snap;      // unsupported board: callers fall back to total
        }

        float worst = NAN, sum = 0.0f;
        int8_t worst_i = -1;
        int good = 0;
        for (int8_t p = 0; p < g_phase_snap.phases; p++) {
            float a = NAN;
            if (power->read_phase_iout_amps((uint8_t)p, &a) != 0) continue;
            if (a != a) continue;                       // NAN
            sum += a;
            good++;
            if (!(worst == worst) || a > worst) { worst = a; worst_i = p; }
        }
        g_phase_snap.worst_a = (good > 0) ? worst   : NAN;
        g_phase_snap.worst_i = (good > 0) ? worst_i : -1;
        g_phase_snap.total_a = (good > 0) ? sum     : NAN;
        g_phase_snap.ts_ms   = now;
        return g_phase_snap;
    }

    // Per-phase limit for a stack TOTAL limit. Returns NAN when the board exposes
    // no per-phase telemetry (then callers must fall back to the total).
    float per_phase_limit(float total_limit, const PhaseSnapshot& ps) {
        if (!(total_limit == total_limit) || total_limit <= 0.0f) return NAN;
        if (ps.phases <= 0) return NAN;
        return total_limit / (float)ps.phases;
    }
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
    uint32_t last_vbus_read_ms = 0;
    uint32_t cached_vbus_mv   = 0;
    while (true) {
        EventBits_t gate = xEventGroupWaitBits(ctx->init_evt,
                                               vcore_gate_bits,
                                               pdFALSE, pdTRUE,
                                               pdMS_TO_TICKS(100));
        uint32_t now = millis();
        // Throttle the PMBus READ_VIN poll to 1s: on a wrong-firmware board the
        // TPS53647 is absent and every read logs an error, flooding the console.
        if (last_vbus_read_ms == 0 || now - last_vbus_read_ms >= 1000) {
            cached_vbus_mv = power->get_vbus();
            last_vbus_read_ms = now;
        }
        bool vbus_ready = cached_vbus_mv >= spec.pwr.vbus_min_required;
        if (vbus_ready) {
            xEventGroupSetBits(ctx->init_evt, INIT_EVENT_VBUS_READY);
        } else {
            xEventGroupClearBits(ctx->init_evt, INIT_EVENT_VBUS_READY);
            if (last_vbus_warn_ms == 0 || now - last_vbus_warn_ms >= 1000) {
                LOG_W("Vbus is %.2fV , at least %.2fV required...",
                      cached_vbus_mv / 1000.0, spec.pwr.vbus_min_required / 1000.0);
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
    // is_vcore_ready() stays silent while healthy and self-diagnoses on failure,
    // so this loop only needs a slow heartbeat plus a periodic full dump.
    uint16_t vcore_wait_ticks = 0;
    while (!power->is_vcore_ready()) {
        delay(500);
        if (++vcore_wait_ticks % 6 == 0) {
            LOG_W("Waiting for vcore power setup... (%ds)", vcore_wait_ticks / 2);
        }
        if (vcore_wait_ticks % 40 == 0) {
            power->debugPrint();
        }
    }
    delay(1000);// wait for the Nexus logic-level rail to rise and the ASIC to stabilize before ESP32 talks to it
    xEventGroupSetBits(ctx->init_evt, INIT_EVENT_VCORE_READY);
    delay(500);

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

    // ── Protection hold state ────────────────────────────────────────────────
    // Escalation: two trips inside this window means "resuming is not working",
    // so the unit is restarted instead of being allowed to cycle.
    constexpr uint32_t PROT_ESCALATE_WINDOW_MS = 60000;
    constexpr uint8_t  PROT_ESCALATE_TRIPS     = 2;
    constexpr uint32_t PROT_LOG_INTERVAL_MS    = 30000;  // hold-state heartbeat (not 1/s)
    constexpr uint32_t PROT_HOLD_POLL_MS       = 100;

    static uint8_t  trip_count_in_window = 0;
    static uint32_t first_trip_ms        = 0;

    // Shut the rails down, publish the fault, and HOLD here until something clears
    // the fault bit. Previously this was an unconditional `while(true)`, which killed
    // the supervisor for the rest of the boot: the fault bit was published, the UI
    // could dismiss it, the mining thread could power Vcore back up through its auto
    // reinit, and the ASIC then ran with NO overcurrent/overtemperature supervision
    // until a manual reboot. The unit also logged the same line every second forever.
    //
    // The hold now releases when the fault bit is cleared, which is the explicit
    // "operator acknowledged / resume requested" signal. Releasing re-arms by
    // clearing the chip's latched status so a stale bit cannot re-trip instantly.
    auto enter_protection_hold = [&](bool is_oc, EventBits_t oc_bit, RebootIntent reboot_intent,
                                     const char* intent_msg, const char* hold_msg) {
        // Immediate safety action: shut down ASIC power.
        power->set_vcore_status(PWR_OFF);
        power->set_vdd_1v8(PWR_OFF);
        power->set_pll_0v8(PWR_OFF);
        // Pre-stamp the real root cause so the ASIC-frozen watchdog (3 min later)
        // sees this intent and does not overwrite it.
        reboot_intent_set(reboot_intent, intent_msg);
        xEventGroupSetBits(ctx->sys_evt, oc_bit);

        // Remember WHICH point tripped. The resume path uses this to avoid walking
        // straight back into the same fault ("press resume, trips again"). The count
        // is consecutive at the same point and survives reboots, so a user who
        // re-selects a known-bad point is escalated instead of silently retried.
        {
            const uint16_t f = spec.asic.req_frq;
            const uint16_t v = spec.asic.req_vcore;
            const uint16_t pf = nvs_config_get_u16(NVS_CONFIG_PROT_FAIL_FREQ,  0);
            const uint16_t pv = nvs_config_get_u16(NVS_CONFIG_PROT_FAIL_VCORE, 0);
            const uint8_t  cnt = (pf == f && pv == v)
                                 ? (uint8_t)(nvs_config_get_u8(NVS_CONFIG_PROT_FAIL_CNT, 0) + 1)
                                 : (uint8_t)1;
            nvs_config_set_u16(NVS_CONFIG_PROT_FAIL_FREQ,  f);
            nvs_config_set_u16(NVS_CONFIG_PROT_FAIL_VCORE, v);
            nvs_config_set_u8 (NVS_CONFIG_PROT_FAIL_CNT,   cnt);
            LOG_W("Failed operating point recorded: %uMHz/%umV (consecutive trips at this point: %u)",
                  (unsigned)f, (unsigned)v, (unsigned)cnt);
        }

        // Count trips in the rolling window.
        uint32_t now_ms = millis();
        if (first_trip_ms == 0 ||
            (uint32_t)(now_ms - first_trip_ms) > PROT_ESCALATE_WINDOW_MS) {
            first_trip_ms        = now_ms;
            trip_count_in_window = 1;
        } else {
            trip_count_in_window++;
        }
        LOG_E("%s", hold_msg);
        LOG_W("Protection trip %u in the last %lus window",
              (unsigned)trip_count_in_window,
              (unsigned long)((now_ms - first_trip_ms) / 1000));

        if (trip_count_in_window >= PROT_ESCALATE_TRIPS) {
            // Restarting alone is NOT enough to break the cycle: the operating point
            // is persisted in NVS, so the unit would come back up, trip again on the
            // very same point, and reboot forever. Reset the work point so the
            // restart actually reloads a configuration that is known to run — the
            // same recovery the overcurrent overlay's "Reset & Reboot" performs.
            uint16_t safe_frq   = spec.asic.default_frq;
            uint16_t safe_vcore = spec.asic.default_vcore;
            // On some boards default_frq/default_vcore are themselves read back from
            // NVS, so they equal the current (failing) setting and "reset" would be a
            // no-op. Fall back to the lowest supported point in that case; anything
            // static beats restarting into the same trip.
            if (safe_frq >= spec.asic.req_frq && !spec.ui.setting_page.oc.empty()) {
                safe_frq = spec.ui.setting_page.oc.front().value;
            }
            if (safe_vcore >= spec.asic.req_vcore) {
                safe_vcore = spec.asic.min_vcore;
            }
            LOG_E("Protection tripped %u times within %lus: resuming is not viable, "
                  "resetting %uMHz/%umV -> %uMHz/%umV and restarting",
                  (unsigned)trip_count_in_window,
                  (unsigned long)(PROT_ESCALATE_WINDOW_MS / 1000),
                  (unsigned)spec.asic.req_frq, (unsigned)spec.asic.req_vcore,
                  (unsigned)safe_frq, (unsigned)safe_vcore);
            nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ,    safe_frq);
            nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, safe_vcore);
            if (ctx->reboot_xsem != nullptr) {
                xSemaphoreGive(ctx->reboot_xsem);
                // Daemon picks it up; hold in the safe state while it does.
                while (true) { delay(500); }
            }
            // No restart channel: stay in the safe state rather than resume blindly.
            while (true) { delay(1000); }
        }

        // Controlled wait: stay powered down, but keep supervising. `protection`
        // in /api/system/info derives from these bits, so an operator (or an
        // external monitor) can see exactly which protection is latched.
        uint32_t last_log = millis();
        while (xEventGroupGetBits(ctx->sys_evt) & oc_bit) {
            delay(PROT_HOLD_POLL_MS);
            uint32_t t = millis();
            if (t - last_log >= PROT_LOG_INTERVAL_MS) {
                last_log = t;
                LOG_E("%s (still latched; dismiss the alert or restart to recover)",
                      hold_msg);
            }
        }

        // Fault bit cleared -> re-arm. Clear the chip's latched status first so the
        // very same sticky bit cannot immediately re-enter this hold.
        power->clear_faults();
        LOG_W("Protection re-armed (%s cleared); supervising Vcore again",
              is_oc ? "overcurrent" : "overtemperature");
    };

    while (true) {
        delay(50);

        const bool ota_running = (ctx->ota_running && *ctx->ota_running);
        if (!ota_running) {
            bool oc_warn  = power->is_oc_warn();
            bool oc_fault = power->is_oc_fault();
            bool ot_warn  = power->is_ot_warn();
            bool ot_fault = power->is_ot_fault();
            // ORDER MATTERS: every *_fault must be evaluated before any *_warn.
            // STATUS_IOUT/STATUS_TEMPERATURE warning bits are STICKY (latched until
            // CLEAR_FAULTS), so a stale OC_WARN that was once set during a Turbo run
            // stays true forever. With the warn branch ahead of OT_FAULT, that stale
            // bit swallowed genuine over-temperature events: the unit kept logging
            // "Overcurrent WARNING" every 3 s while the real condition was the VRM
            // overheating, and the OT shutdown path never ran. Faults win, warnings
            // only fill the gap when nothing is actually faulting.
            //
            // Trace sampling: one extra READ_IOUT per ~50 ms loop iteration
            // (~20 reads/s on top of the 4 status-byte polls already here — well
            // under 1% of a 400 kHz bus). Sampling stops while a fault hold is
            // active (Vcore is off there, so the samples would all be ~0 A and
            // would bury the pre-trip window we care about).
            float iout_live = NAN;
            if (!oc_fault && !ot_fault) {
                iout_live = itrace_sample(power->get_iout_amps());
            }
            if (oc_fault) {
                // Quote the per-phase trip point too: the limit is a stack TOTAL
                // divided across phases, so "limit=70.5A" alone reads as "it should
                // not have tripped" when in fact the heaviest phase had crossed
                // 70.5/3 = 23.5 A.
                const PhaseSnapshot& fs = phase_snapshot(power, true);
                const float flim = power->get_oc_limit_amps();
                const float plim = per_phase_limit(flim, fs);
                if (plim == plim) {
                    LOG_W("Overcurrent FAULT detected! IOUT=%.2fA limit=%.1fA total (%.1fA/phase over %d phases). Taking safety actions...",
                          power->get_iout_amps(), flim, plim, (int)fs.phases);
                } else {
                    LOG_W("Overcurrent FAULT detected! IOUT=%.2fA limit=%.1fA. Taking safety actions...",
                          power->get_iout_amps(), flim);
                }
                itrace_dump(power, "OC");
                enter_protection_hold(
                    true, SYS_EVENT_POWER_OC_FAULT, REBOOT_INTENT_OVERCURRENT_FAULT,
                    "power overcurrent fault, Vcore/VDD/PLL shut down",
                    "ASIC powered down due to Power OverCurrent. Please check your ASICs OverClocking settings, and cooling.");
            } else if (ot_fault) {
                LOG_W("!!! Overtemperature FAULT detected! Temp=%.1fC limit=%.0fC. Taking safety actions !!!",
                      power->get_temperature(), power->get_ot_limit_celsius());
                enter_protection_hold(
                    false, SYS_EVENT_POWER_OT_FAULT, REBOOT_INTENT_OVERHEAT_VCORE,
                    "power overtemperature fault, Vcore/VDD/PLL shut down",
                    "ASIC powered down due to OverTemperature. Please check your cooling system and ambient temperature.");
            } else if (oc_warn) {
                // The chip latches OC_WARN until CLEAR_FAULTS, so the bit alone cannot
                // tell "overcurrent right now" from "crossed the warn line minutes ago
                // at a different operating point".
                //
                // Compare against the PER-PHASE limit whenever the board exposes
                // per-phase telemetry. Comparing the total against the total limit is
                // wrong on an imbalanced stack: measured on Nexus, total 67.1 A was
                // reported as "latched (stale)" while the heaviest phase sat at
                // 23.7 A against a 22.5 A per-phase warn point — and it tripped
                // seconds later. The warning was live all along.
                float wlim = power->get_oc_warn_limit();
                const PhaseSnapshot& ws = phase_snapshot(power, false);
                const float wphase = per_phase_limit(wlim, ws);
                bool live;
                if (wphase == wphase) {                       // per-phase comparison
                    live = (ws.worst_a == ws.worst_a) && (ws.worst_a >= wphase);
                } else {                                      // total-only fallback
                    live = (iout_live == iout_live) && (wlim == wlim) && (iout_live >= wlim);
                }
                static uint32_t last = millis();
                if (millis() - last >= 3000) {
                    if (live) {
                        if (wphase == wphase) {
                            LOG_W("!!! Overcurrent WARNING (live) !!! phase %d = %.2fA >= %.1fA/phase (IOUT total %.2fA)",
                                  (int)ws.worst_i, ws.worst_a, wphase, iout_live);
                        } else {
                            LOG_W("!!! Overcurrent WARNING (live) !!! IOUT=%.2fA >= warn-limit %.1fA",
                                  iout_live, wlim);
                        }
                    } else {
                        if (wphase == wphase) {
                            LOG_W("Overcurrent WARNING latched (heaviest phase %d now %.2fA, below %.1fA/phase; IOUT total %.2fA; stale, clears on op-point change)",
                                  (int)ws.worst_i, ws.worst_a, wphase, iout_live);
                        } else {
                            LOG_W("Overcurrent WARNING latched (IOUT now %.2fA, below warn-limit %.1fA; stale, clears on op-point change)",
                                  iout_live, wlim);
                        }
                    }
                    last = millis();
                }
            } else if (ot_warn) {
                float temp = power->get_temperature();
                float wlim = power->get_ot_warn_limit_celsius();
                bool live = (temp == temp) && (wlim == wlim) && (temp >= wlim);  // NAN-safe
                static uint32_t last = millis();
                if (millis() - last >= 3000) {
                    if (live) {
                        LOG_W("Overtemperature WARNING (live)... Temp=%.1fC >= warn-limit %.1fC", temp, wlim);
                    } else {
                        LOG_W("Overtemperature WARNING latched (Temp now %.1fC, below warn-limit %.1fC; stale)",
                              temp, wlim);
                    }
                    last = millis();
                }
            }
        }
#if 0
        {
            static uint32_t last_debug = millis();
            if (millis() - last_debug >= 3000) {
                power->debugPrint();
                // power->dump();
                last_debug = millis();
            }
        }
#endif
        // skip vcore regulation while the miner is intentionally idle
        static uint32_t vcore_set = spec.asic.req_vcore;
        if (ctx->mining && ctx->mining->is_controlled_idle()) {
            // Resume/reinit restart the rail at req_vcore; start the loop from there too.
            vcore_set = spec.asic.req_vcore;
            continue;
        }

        uint32_t vcore_measure = power->get_vcore();
        // get_vcore() samples for ~50ms: a pause/reinit cutting Vcore inside that window
        // yields a collapsing reading that would slam vcore_set to max_vcore.
        if (ctx->mining && ctx->mining->is_controlled_idle()) {
            continue;
        }
        int32_t err = vcore_measure - spec.asic.req_vcore;
        if (abs(err) <= 5) {
            LOG_D("Vcore %d/%dmV, error %d mV, Vcore within acceptable range",
                  vcore_measure, spec.asic.req_vcore, err);
            continue;
        }
        LOG_D("Vcore %d/%dmV, error %d mV, Adjust vcore for error correction %d mV",
              vcore_measure, spec.asic.req_vcore, err, err / 5);
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
    std::vector<uint32_t> rpm_start_ms(fan_n, 0);
    std::vector<uint32_t> pid_start_ms(fan_n, 0);
    delay(100);

    // Initialize TMP102 temperature sensor
    tmp102_init();

    // TMP102 self-test: retry until both sensors pass (3-sample average each attempt)
    // Exponential back-off: on a wrong-firmware board the VRM (TPS53647) is absent,
    // so retrying at a fixed 500ms floods the console with read errors. Back off
    // up to 5s to throttle those logs without touching the low-level driver.
    uint32_t retry_delay_ms = 500;
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
        static bool asic_detail_printed = false;
        if (asic_ok && !asic_detail_printed) {
            tmp102_debug_print(TMP102_IIC_ASIC_ADDR, "ASIC");
            asic_detail_printed = true;
        }
        if (vcore_ok && asic_ok) break;
        LOG_E("TMP102 self test failed, retrying...");
        delay(retry_delay_ms);
        retry_delay_ms *= 2;
        if (retry_delay_ms > 5000) retry_delay_ms = 5000;
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
    auto get_fan_index = [&](uint8_t fan_id) -> int {
        for (size_t i = 0; i < spec.fans.size(); i++)
            if (spec.fans[i].id == fan_id) return (int)i;
        return -1;
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
            // Written last: once the UI sees a non-TESTING state, rpm is already
            // this round's measured value.
            a->fan_status->self_test_state = a->fan_status->self_test ? FAN_SELF_TEST_PASS : FAN_SELF_TEST_FAIL;
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
            // Mark this round as in-progress so the loading page doesn't keep
            // showing a stale FAIL from the previous round while measuring.
            fan.self_test_state = FAN_SELF_TEST_TESTING;
            xTaskCreate(selftest_task, "fan_st", 4096, &st_args[i], uxTaskPriorityGet(NULL), NULL);
            i++;
        }
        for (size_t j = 0; j < n; j++) xSemaphoreTake(st_done, portMAX_DELAY);
        vSemaphoreDelete(st_done);

        bool all_fan_ok = true;
        for (auto& fan : fan_list) all_fan_ok = all_fan_ok && fan.self_test;

        if (all_fan_ok) { LOG_W("All fans passed self test."); break; }
        else {
            LOG_W("Some fans failed self test, retrying...");
            // Hold the FAIL state before the next round overwrites it with
            // TESTING. Without this, the state lives only a few ms between
            // semaphore return and the next loop iteration, so the loading
            // page (10ms poll) barely catches one frame of the FAIL message.
            delay(2500);
        }
    }
    xEventGroupSetBits(ctx->init_evt, INIT_EVENT_FAN_READY);
    delay(2000); // let user see the self-test result on loading page before fan speed changes

    auto sample_fan_rpm = [&](size_t fan_index, uint32_t now_ms) {
        fan_config_t& fan_cfg = spec.fans[fan_index];
        fan_status_t* fan_status = get_fan_status(fan_cfg.id);
        if (fan_status == nullptr) return;

        if (rpm_start_ms[fan_index] == 0) {
            pcnt_counter_clear(fan_cfg.init.torch.unit);
            rpm_start_ms[fan_index] = now_ms;
            return;
        }

        uint32_t delta_time = now_ms - rpm_start_ms[fan_index];
        if (delta_time < 1000) return;

        int16_t pulse_count = 0;
        pcnt_get_counter_value(fan_cfg.init.torch.unit, &pulse_count);
        pcnt_counter_clear(fan_cfg.init.torch.unit);
        fan_status->rpm = calculate_rpm(pulse_count, delta_time / 1000.0);
        rpm_start_ms[fan_index] = now_ms;
    };
    auto drive_fan = [&](size_t fan_index, bool update_pid) {
        fan_config_t& fan_cfg = spec.fans[fan_index];
        fan_status_t* fan_status = get_fan_status(fan_cfg.id);
        if (fan_status == nullptr) return;

        // Benchmark takeover: hold a fixed duty so every measured point sees the
        // same cooling. Lease-based, so a hung benchmark thread cannot pin the fan.
        constexpr size_t FAN_SLOTS_MAX = 4;
        static bool     ovr_prev[FAN_SLOTS_MAX]  = {false};
        static uint16_t ovr_saved[FAN_SLOTS_MAX] = {0};
        const bool override_active =
            spec.fan_override_until_ms != 0 &&
            (int32_t)(millis() - spec.fan_override_until_ms) < 0;

        if (fan_index < FAN_SLOTS_MAX) {
            if (override_active && !ovr_prev[fan_index]) {
                ovr_saved[fan_index] = fan_status->speed;
            } else if (!override_active && ovr_prev[fan_index]) {
                // Hand control back where we found it, so a manual duty set through
                // the API survives a benchmark run.
                fan_status->speed = ovr_saved[fan_index];
            }
            ovr_prev[fan_index] = override_active;
        }

        if (update_pid && override_active && spec.fan_controllable) {
            fan_status->speed = spec.fan_override_duty;
        } else if (update_pid && fan_cfg.auto_speed && fan_status->self_test) {
            uint32_t now_ms = millis();
            float dt = (pid_start_ms[fan_index] == 0) ? 0.125f : (now_ms - pid_start_ms[fan_index]) / 1000.0f;
            if (dt <= 0.0f) dt = 0.001f;
            float target   = fan_cfg.target_temp;
            float measured = (fan_cfg.id == 0) ? ctx->temp->asic : ctx->temp->vcore; // fan0=ASIC, fan1=Vcore
            float duty = pid_compute(&fan_cfg.pid, target, measured, dt);
            // Baud-rate guard on the CAST: a non-finite float converted to uint16_t
            // is undefined and was observed as 65535 (fan ran full speed while every
            // consumer read a nonsense duty). pid_compute already recovers from NaN,
            // this keeps any future path from publishing one.
            if (!isfinite(duty)) duty = 0.0f;
            if (duty < 0.0f)    duty = 0.0f;
            if (duty > 100.0f)  duty = 100.0f;
            fan_status->speed = (uint16_t)duty;
            pid_start_ms[fan_index] = now_ms;
        }

        fan_set_speed(fan_cfg.init, fan_status->speed / 100.0f, fan_cfg.polarity);
    };

    uint32_t now_ms = millis();
    for (size_t i = 0; i < fan_n; i++) {
        pcnt_counter_clear(spec.fans[i].init.torch.unit);
        rpm_start_ms[i] = now_ms;
        drive_fan(i, false);
    }

    // fan control loop
    while (true) {
        EventBits_t bits = xEventGroupWaitBits(ctx->sys_evt,
            SYS_EVENT_MINER_VCORE_TEMP_UPDATE | SYS_EVENT_MINER_ASIC_TEMP_UPDATE,
            pdTRUE, pdFALSE, pdMS_TO_TICKS(125));

        now_ms = millis();
        for (size_t i = 0; i < fan_n; i++) {
            sample_fan_rpm(i, now_ms);
        }

        if ((bits & SYS_EVENT_MINER_ASIC_TEMP_UPDATE) == SYS_EVENT_MINER_ASIC_TEMP_UPDATE) {
            int fan_index = get_fan_index(0);
            if (fan_index >= 0) drive_fan((size_t)fan_index, true);
        }
        if ((bits & SYS_EVENT_MINER_VCORE_TEMP_UPDATE) == SYS_EVENT_MINER_VCORE_TEMP_UPDATE) {
            int fan_index = get_fan_index(1);
            if (fan_index >= 0) drive_fan((size_t)fan_index, true);
        }
    }
}
