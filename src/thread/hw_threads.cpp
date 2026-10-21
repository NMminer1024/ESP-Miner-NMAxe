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

    while (true) {
        delay(50);

        const bool ota_running = (ctx->ota_running && *ctx->ota_running);
        if (!ota_running) {
            bool oc_warn  = power->is_oc_warn();
            bool oc_fault = power->is_oc_fault();
            bool ot_warn  = power->is_ot_warn();
            bool ot_fault = power->is_ot_fault();
            if (oc_fault) {
                LOG_W("Overcurrent FAULT detected! IOUT=%.2fA limit=%.1fA. Taking safety actions...",
                      power->get_iout_amps(), power->get_oc_limit_amps());
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
                    LOG_W("!!! Overcurrent WARNING detected !!! (%.2fA/%.1fA)",
                          power->get_iout_amps(), power->get_oc_limit_amps());
                    last = millis();
                }
            } else if (ot_fault) {
                LOG_W("!!! Overtemperature FAULT detected! Temp=%.1fC limit=%.0fC. Taking safety actions !!!",
                      power->get_temperature(), power->get_ot_limit_celsius());
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
                    LOG_W("Overtemperature WARNING detected... (%.1fC/%.0fC)",
                          power->get_temperature(), power->get_ot_limit_celsius());
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

        if (update_pid && fan_cfg.auto_speed && fan_status->self_test) {
            uint32_t now_ms = millis();
            float dt = (pid_start_ms[fan_index] == 0) ? 0.125f : (now_ms - pid_start_ms[fan_index]) / 1000.0f;
            if (dt <= 0.0f) dt = 0.001f;
            float target   = fan_cfg.target_temp;
            float measured = (fan_cfg.id == 0) ? ctx->temp->asic : ctx->temp->vcore; // fan0=ASIC, fan1=Vcore
            fan_status->speed = (uint16_t)pid_compute(&fan_cfg.pid, target, measured, dt);
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
