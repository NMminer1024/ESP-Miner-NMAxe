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
#include <vector>

void stratum_thread_entry(void* args) {
    (void)args;
    LOG_D("(stratum) placeholder");
    while (true) { delay(10000); }
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

void miner_tx_thread_entry(void* args) {
    (void)args;
    LOG_D("(asic_tx) placeholder");
    while (true) { delay(10000); }
}

void miner_rx_thread_entry(void* args) {
    (void)args;
    LOG_D("(asic_rx) placeholder");
    while (true) { delay(10000); }
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
