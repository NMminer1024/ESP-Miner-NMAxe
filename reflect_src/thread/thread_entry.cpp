#include "thread_entry.h"
#include "../app/application.h"
#include "../app/system_events.h"
#include "../utils/logger/logger.h"
#include "../utils/reboot_log/reboot_log.h"
#include "../board/board.h"
#include "../mining/mining_types.h"
#include "../drivers/power/power_hal.h"
#include "../drivers/power/power_ctx.h"

void stratum_thread_entry(void* args) {
    (void)args;
    LOG_D("(stratum) placeholder");
    while (true) { delay(10000); }
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

void fan_thread_entry(void* args) {
    (void)args;
    LOG_D("(fan) placeholder");
    while (true) { delay(10000); }
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
