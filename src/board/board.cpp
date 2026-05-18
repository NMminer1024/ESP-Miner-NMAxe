#include <nvs_flash.h>
#include "nvs/nvs_config.h"
#include "utils/logger/logger.h"
#include "board.h"
#include "nmaxe.h"
#include "nmaxegamma.h"
#include "nmqaxepp.h"
#include "drivers/iic/i2c_master.h"
#include "drivers/extio/tca9554.h"
#include "drivers/fan/tmp102.h"
#include "drivers/temp/temp_hal.h"

BoardModelType get_board_model(){
    // ── Step 1: active discharge ──────────────────────────────────────────────
    // Drive all model-select pins OUTPUT LOW to discharge any external
    // capacitance (e.g. fan-PWM filter caps on GPIO10) that may have charged
    // during power-on.  At this point in boot no power rails are active, so
    // briefly asserting LOW on these pins is safe for all board variants.
    pinMode(NM_MODEL_SELECT_PIN0, OUTPUT); digitalWrite(NM_MODEL_SELECT_PIN0, LOW);
    pinMode(NM_MODEL_SELECT_PIN1, OUTPUT); digitalWrite(NM_MODEL_SELECT_PIN1, LOW);
    pinMode(NM_MODEL_SELECT_PIN2, OUTPUT); digitalWrite(NM_MODEL_SELECT_PIN2, LOW);
    delay(20); // hold LOW long enough to discharge external capacitance

    // ── Step 2: release to INPUT_PULLUP ──────────────────────────────────────
    // Board pull resistors now determine the stable logical level:
    //   NMAxe / Gamma : PIN2 pulled HIGH by board hardware  → reads 1
    //   QAxe++        : PIN2 pulled LOW  by board hardware  → reads 0
    pinMode(NM_MODEL_SELECT_PIN0, INPUT_PULLUP);
    pinMode(NM_MODEL_SELECT_PIN1, INPUT_PULLUP);
    pinMode(NM_MODEL_SELECT_PIN2, INPUT_PULLUP);

    // ── Step 3: debounce ─────────────────────────────────────────────────────
    // Only accept a raw value that:
    //   a) matches a known-valid board combination, AND
    //   b) has been continuously stable for STABLE_MS.
    // Invalid transients (e.g. 0b101 while a cap finishes discharging) are
    // ignored; only legal board codes reset / accumulate the stable counter.
    constexpr uint32_t SAMPLE_INTERVAL_MS = 10;
    constexpr uint32_t STABLE_MS          = 500;
    constexpr uint32_t STABLE_SAMPLES     = STABLE_MS / SAMPLE_INTERVAL_MS; // 50
    constexpr uint32_t TIMEOUT_SAMPLES    = 3000 / SAMPLE_INTERVAL_MS;      // 3 s

    auto read_raw = []() -> uint8_t {
        return static_cast<uint8_t>(
            (digitalRead(NM_MODEL_SELECT_PIN0) << 2) |
            (digitalRead(NM_MODEL_SELECT_PIN1) << 1) |
            (digitalRead(NM_MODEL_SELECT_PIN2))
        );
    };
    auto is_valid_raw = [](uint8_t r) -> bool {
        return r == 0b111 || r == 0b011 || r == 0b100 || r == 0b110;
    };

    uint8_t  last_raw    = 0xFF;
    uint32_t stable_cnt  = 0;
    uint32_t timeout_cnt = 0;

    while (true) {
        const uint8_t raw = read_raw();
        if (raw == last_raw) {
            stable_cnt++;
        } else {
            last_raw   = raw;
            stable_cnt = 1;
        }
        if (stable_cnt >= STABLE_SAMPLES && is_valid_raw(raw)) break;
        if (++timeout_cnt >= TIMEOUT_SAMPLES) {
            LOG_W("[Board] Pin debounce timeout; last raw=0b%d%d%d",
                  (last_raw >> 2) & 1, (last_raw >> 1) & 1, last_raw & 1);
            break;
        }
        delay(SAMPLE_INTERVAL_MS);
    }

    LOG_I("[Board] GPIO%d=%d GPIO%d=%d GPIO%d=%d raw=0b%d%d%d",
          NM_MODEL_SELECT_PIN0, (last_raw >> 2) & 1,
          NM_MODEL_SELECT_PIN1, (last_raw >> 1) & 1,
          NM_MODEL_SELECT_PIN2,  last_raw        & 1,
          (last_raw >> 2) & 1, (last_raw >> 1) & 1, last_raw & 1);

    // 0b111 NMAXE
    // 0b011 NMAXE_GAMMA
    // 0b100 NMQAXE++ 2 phase
    // 0b110 NMQAXE++ 3 phase
    switch (last_raw) {
        case 0b111: return NMAXE;
        case 0b011: return NMAXE_GAMMA;
        case 0b100: return NMQAXE_PLUS_PLUS;
        case 0b110: return NMQAXE_PLUS_PLUS_REV61;
        default:    return BOARD_UNKNOWN;
    }
}

BoardSpecConfig get_board_config(BoardModelType model) {
    BoardSpecConfig config;
    fan_config_t fan_cfg;
    switch(model) {
        case NMAXE:
            config.name                      = BOARD_NMAXE_NAME;
            config.display_name              = BOARD_NMAXE_NAME;
            config.asic.name                 = CHIP_NMAXE_NAME;
            config.asic.num_req              = 1;
            config.asic.temp_limit.high      = 75.0f;
            config.asic.temp_limit.medium    = 65.0f;
            config.asic.temp_limit.low       = 50.0f;
            config.tft.width                 = 135;
            config.tft.height                = 240;
            config.tft.color_invert          = true;
            config.tft.dc_pin                = 47;
            config.tft.bl.pin                = 17;
            config.tft.bl.pwm_ch             = 0;
            config.tft.bl.pwm_freq           = 1000*100; // Hz
            config.tft.bl.pwm_resolution     = 8;        // bits
            config.tft.rst_pin               = 40;
            config.tft.pwr_pin               = 18;
            config.spi.cs_pin                = 39;
            config.spi.miso_pin              = -1;
            config.spi.mosi_pin              = 48;
            config.spi.sclk_pin              = 38;
            config.ui.hashrate_dist_page.max_x_hr  = 1000;
            config.ui.hashrate_dist_page.max_x_bars= 20;
            config.ui.hashrate_dist_page.count     = 0;

            config.ui.dashboard_page.power.vbus          = {0.0f, 15.0f};
            config.ui.dashboard_page.power.ibus          = {0.0f, 4.0f};
            config.ui.dashboard_page.power.power         = {0.0f, 30.0f};
            config.ui.dashboard_page.heat.mcu            = {0.0f, 75.0f};
            config.ui.dashboard_page.heat.asic           = {0.0f, 80.0f};
            config.ui.dashboard_page.heat.vcore          = {0.0f, 100.0f};
            config.ui.dashboard_page.heat.fan            = {0.0f, 9000.0f};
            config.ui.dashboard_page.performance.asic_freq_req  = {390.0f, 650.0f};
            config.ui.dashboard_page.performance.vcore_req      = {1.000f, 1.500f};
            config.ui.dashboard_page.performance.vcore_measure  = {1.000f, 1.500f};
            config.ui.setting_page.oc = {
                {"400 MHz",           400},
                {"425 MHz",           425},
                {"475 MHz",           475},
                {"485 MHz",           485},
                {"500 MHz",           500},
                {"550 MHz",           550},
                {"575 MHz (default)", 575},
            };
            config.ui.setting_page.vc = {
                {"1100 mV",           1100},
                {"1150 mV",           1150},
                {"1200 mV",           1200},
                {"1250 mV (default)", 1250},
                {"1300 mV",           1300},
            };
            
            config.btn.boot_pin              = 0;
            config.btn.user_pin              = 12;
            config.pwr.en_pins.pwr_pll_0v8   = 13;
            config.pwr.en_pins.pwr_vdd_1v8   = 14;
            config.pwr.en_pins.pwr_vcore     = 10;
            config.pwr.adc_pins.vbus         = 2;
            config.pwr.adc_pins.ibus         = 3;
            config.pwr.adc_pins.vcore        = 1;
            config.pwr.vcore_regulator_pin   = 16;    
            config.pwr.pgood_pin             = 21;
            config.pwr.dc_plug_pin           = 11;
            config.pwr.vbus_min_required     = 8000;// mV, minimum vbus voltage to start mining
            config.pwr.temp_limit.high       = 85.0f;
            config.pwr.temp_limit.medium     = 75.0f;
            config.pwr.temp_limit.low        = 50.0f;
            config.pwr.power_low_threshold   = 10.0f; // Watt
            config.iic.scl_pin               = 8;   
            config.iic.sda_pin               = 9;
            config.led.wifi_pin              = 6;
            config.led.pool_pin              = 4;
            config.led.sys_pin               = 5;
            config.asic.rx_pin               = 44;
            config.asic.tx_pin               = 43;
            config.asic.rst_pin              = 45;
            config.asic.job_interval_ms      = 2000;
            config.asic.default_frq          = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, 575);
            config.asic.default_vcore        = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, 1250);
            config.asic.req_frq              = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, config.asic.default_frq);
            config.asic.req_vcore            = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, config.asic.default_vcore);
            config.asic.min_vcore            = 1100;
            config.asic.max_vcore            = 1300;
            config.asic.diff_thr_init        = 256;
            config.asic.com_baud_init        = 115200;
            config.asic.com_baud_work        = 1000000;
            config.asic.com_port             = &Serial1;
            config.preference.screen.brightness    = 100;
            config.preference.screen.auto_rolling  = false;
            config.preference.screen.flip          = true;
            config.preference.screen.saver_enable  = false;
            config.preference.screen.saver_timeout = 15*60; // 15 minutes

            config.preference.led.enable           = true;
            config.create_asic_instance      = create_axe_asic_instance;
            config.create_power_instance     = create_axe_power_instance;
            config.setup_temp_hal            = [](AxePowerHal*) { tmp102_register_temp_hal(); };

            fan_cfg.id                        = 0;
            fan_cfg.init.pwm.pin              = 41;
            fan_cfg.init.pwm.ch               = 2;
            fan_cfg.init.pwm.freq             = 1000*100; // Hz
            fan_cfg.init.pwm.resolution       = 8;        // bits
            fan_cfg.init.torch.pulse_gpio_num = 42;
            fan_cfg.init.torch.ctrl_gpio_num  = PCNT_PIN_NOT_USED; // Not used
            fan_cfg.init.torch.lctrl_mode     = PCNT_MODE_KEEP;
            fan_cfg.init.torch.hctrl_mode     = PCNT_MODE_KEEP;
            fan_cfg.init.torch.pos_mode       = PCNT_COUNT_INC;
            fan_cfg.init.torch.neg_mode       = PCNT_COUNT_DIS;
            fan_cfg.init.torch.counter_h_lim  = 30000;
            fan_cfg.init.torch.counter_l_lim  = 0;
            fan_cfg.init.torch.unit           = PCNT_UNIT_0;
            fan_cfg.init.torch.channel        = PCNT_CHANNEL_0;
            fan_cfg.init.self_test_rpm_thr   = 4000;
            fan_cfg.init.danger_rpm_thr      = 500;
            fan_cfg.pid.Kp                   = 50.0f;
            fan_cfg.pid.Ki                   = 1.0f;
            fan_cfg.pid.Kd                   = 0.0f;
            fan_cfg.pid.prev_error           = 0;
            fan_cfg.pid.integral             = 0;
            fan_cfg.pid.output_min           = 25.0f;
            fan_cfg.pid.output_max           = 99.999f;
            fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_ASIC_FAN_SPEED, true);
            fan_cfg.target_temp              = String(nvs_config_get_string(NVS_CONFIG_ASIC_TARGET_TEMP, "30")).toFloat();
            config.fans.clear();
            config.fans.push_back(fan_cfg); // single fan
            break;
        case NMAXE_GAMMA:
            config.name                      = BOARD_NMAXE_GAMMA_NAME;
            config.display_name              = BOARD_NMAXE_GAMMA_NAME;
            config.asic.name                 = CHIP_NMAXE_GAMMA_NAME;
            config.asic.num_req              = 1;
            config.asic.temp_limit.high      = 70.0f;
            config.asic.temp_limit.medium    = 60.0f;
            config.asic.temp_limit.low       = 50.0f;
            config.asic.job_interval_ms      = 500;
            config.tft.width                 = 135;
            config.tft.height                = 240;
            config.tft.dc_pin                = 47;
            config.tft.bl.pin                = 17;
            config.tft.bl.pwm_ch             = 0;
            config.tft.bl.pwm_freq           = 1000*100; // Hz
            config.tft.bl.pwm_resolution     = 8;        // bits
            config.tft.rst_pin               = 40;
            config.tft.pwr_pin               = 18;
            config.tft.color_invert          = true;
            config.spi.cs_pin                = 39;
            config.spi.miso_pin              = -1;
            config.spi.mosi_pin              = 48;
            config.spi.sclk_pin              = 38;
            config.ui.hashrate_dist_page.max_x_hr  = 2000;
            config.ui.hashrate_dist_page.max_x_bars= 20;
            config.ui.hashrate_dist_page.count     = 0;

            config.ui.dashboard_page.power.vbus          = {0.0f, 15.0f};
            config.ui.dashboard_page.power.ibus          = {0.0f, 5.0f};
            config.ui.dashboard_page.power.power         = {0.0f, 50.0f};
            config.ui.dashboard_page.heat.mcu            = {0.0f, 75.0f};
            config.ui.dashboard_page.heat.asic           = {0.0f, 80.0f};
            config.ui.dashboard_page.heat.vcore          = {0.0f, 100.0f};
            config.ui.dashboard_page.heat.fan            = {0.0f, 9000.0f};
            config.ui.dashboard_page.performance.asic_freq_req  = {390.0f, 800.0f};
            config.ui.dashboard_page.performance.vcore_req      = {0.900f, 1.500f};
            config.ui.dashboard_page.performance.vcore_measure  = {0.900f, 1.500f};
            config.ui.setting_page.oc = {
                {"400 MHz",           400},
                {"440 MHz",           440},
                {"490 MHz",           490},
                {"550 MHz",           550},
                {"575 MHz",           575},
                {"600 MHz (default)", 600},
                {"650 MHz",           650},
                {"700 MHz",           700},
            };
            config.ui.setting_page.vc = {
                {"1000 mV",           1000},
                {"1025 mV",           1025},
                {"1050 mV",           1050},
                {"1100 mV",           1100},
                {"1125 mV (default)", 1125},
                {"1150 mV",           1150},
                {"1175 mV",           1175},
                {"1200 mV",           1200},
                {"1225 mV",           1225},
                {"1250 mV",           1250},
            };
            config.btn.boot_pin              = 0;
            config.btn.user_pin              = 12;
            config.pwr.en_pins.pwr_pll_0v8   = 13;
            config.pwr.en_pins.pwr_vdd_1v8   = 14;
            config.pwr.en_pins.pwr_vcore     = 10;
            config.pwr.adc_pins.vbus         = 2;
            config.pwr.adc_pins.ibus         = 3;
            config.pwr.adc_pins.vcore        = 1;
            config.pwr.vcore_regulator_pin   = 16;    
            config.pwr.pgood_pin             = 21;
            config.pwr.dc_plug_pin           = 11;
            config.pwr.vbus_min_required     = 8000;// mV, minimum vbus voltage to start mining
            config.pwr.temp_limit.high       = 85.0f;
            config.pwr.temp_limit.medium     = 75.0f;
            config.pwr.temp_limit.low        = 50.0f;
            config.pwr.power_low_threshold   = 10.0f; // Watt
            config.iic.scl_pin               = 8;   
            config.iic.sda_pin               = 9;
            config.led.wifi_pin              = 6;
            config.led.pool_pin              = 4;
            config.led.sys_pin               = 5;
            config.asic.default_frq          = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, 600);
            config.asic.default_vcore        = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, 1125);
            config.asic.req_frq              = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, config.asic.default_frq);
            config.asic.req_vcore            = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, config.asic.default_vcore);
            config.asic.min_vcore            = 1000;
            config.asic.max_vcore            = 1250;
            config.asic.diff_thr_init        = 512;
            config.asic.rx_pin               = 44;
            config.asic.tx_pin               = 43;
            config.asic.rst_pin              = 45;
            config.asic.com_baud_init        = 115200;
            config.asic.com_baud_work        = 1000000;
            config.asic.com_port             = &Serial1;
            config.preference.screen.brightness    = 100;
            config.preference.screen.auto_rolling  = false;
            config.preference.screen.flip          = true;
            config.preference.screen.saver_enable  = false;
            config.preference.screen.saver_timeout = 15*60; // 15 minutes

            config.preference.led.enable           = true;
            config.create_asic_instance      = create_gamma_asic_instance;
            config.create_power_instance     = create_gamma_power_instance;
            config.setup_temp_hal            = [](AxePowerHal*) { tmp102_register_temp_hal(); };

            fan_cfg.id                       = 0;
            fan_cfg.init.pwm.pin              = 41;
            fan_cfg.init.pwm.ch               = 2;
            fan_cfg.init.pwm.freq             = 1000*100; // Hz
            fan_cfg.init.pwm.resolution       = 8;        // bits
            fan_cfg.init.torch.pulse_gpio_num = 42;
            fan_cfg.init.torch.ctrl_gpio_num  = PCNT_PIN_NOT_USED; // Not used
            fan_cfg.init.torch.lctrl_mode     = PCNT_MODE_KEEP;
            fan_cfg.init.torch.hctrl_mode     = PCNT_MODE_KEEP;
            fan_cfg.init.torch.pos_mode       = PCNT_COUNT_INC;
            fan_cfg.init.torch.neg_mode       = PCNT_COUNT_DIS;
            fan_cfg.init.torch.counter_h_lim  = 30000;
            fan_cfg.init.torch.counter_l_lim  = 0;
            fan_cfg.init.torch.unit           = PCNT_UNIT_0;
            fan_cfg.init.torch.channel        = PCNT_CHANNEL_0;
            fan_cfg.init.self_test_rpm_thr    = 4000;
            fan_cfg.init.danger_rpm_thr       = 500;
            fan_cfg.pid.Kp                    = 50.0f;
            fan_cfg.pid.Ki                    = 1.0f;
            fan_cfg.pid.Kd                    = 0.0f;
            fan_cfg.pid.prev_error            = 0;
            fan_cfg.pid.integral              = 0;
            fan_cfg.pid.output_min            = 25.0f;
            fan_cfg.pid.output_max            = 99.999f;
            fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_ASIC_FAN_SPEED, true);
            fan_cfg.target_temp              = String(nvs_config_get_string(NVS_CONFIG_ASIC_TARGET_TEMP, "30")).toFloat();
            config.fans.clear();
            config.fans.push_back(fan_cfg); // single fan
            break;
        case NMQAXE_PLUS_PLUS:
        case NMQAXE_PLUS_PLUS_REV61:
            // --- shared: NMQAXE_PLUS_PLUS and NMQAXE_PLUS_PLUS_REV61 ---
            config.name                      = BOARD_NMQAXE_PLUS_PLUS_NAME;     // runtime functional ID
            config.asic.name                 = CHIP_NMQAXE_PLUS_PLUS_NAME;
            config.asic.num_req              = 4;
            config.asic.temp_limit.high      = 75.0f;
            config.asic.temp_limit.medium    = 65.0f;
            config.asic.temp_limit.low       = 50.0f;
            config.tft.width                 = 240;
            config.tft.height                = 320;
            config.tft.dc_pin                = 3;
            config.tft.bl.pin                = 6;
            config.tft.bl.pwm_ch             = 0;
            config.tft.bl.pwm_freq           = 1000*100; // Hz
            config.tft.bl.pwm_resolution     = 8;        // bits
            config.tft.rst_pin               = -1;       
            config.tft.pwr_pin               = -1;
            config.tft.color_invert          = true;
            config.spi.cs_pin                = -1;
            config.spi.miso_pin              = 2;
            config.spi.mosi_pin              = 1;
            config.spi.sclk_pin              = 5;
            config.ui.hashrate_dist_page.max_x_hr  = 10000;
            config.ui.hashrate_dist_page.max_x_bars= 20;
            config.ui.hashrate_dist_page.count     = 0;

            // --- per-variant fields ---
            if (model == NMQAXE_PLUS_PLUS) {
                config.display_name              = BOARD_NMQAXE_PLUS_PLUS_NAME;
                config.asic.default_frq          = 600;
                config.asic.default_vcore        = 1150;
                config.asic.min_vcore            = 1000;
                config.asic.max_vcore            = 1350;
                config.asic.diff_thr_init        = 1024;
                config.asic.job_interval_ms      = 500;
                config.ui.dashboard_page.power.ibus          = {0.0f, 15.0f};
                config.ui.dashboard_page.power.power         = {0.0f, 160.0f};
                config.ui.dashboard_page.performance.asic_freq_req  = {500.0f, 800.0f};
                config.ui.dashboard_page.performance.vcore_req      = {1.00f, 1.300f};
                config.ui.dashboard_page.performance.vcore_measure  = {1.00f, 1.300f};
                config.ui.setting_page.oc = {
                        {"515 MHz",           515},
                        {"550 MHz",           550},
                        {"575 MHz",           575},
                        {"600 MHz (default)", 600},
                        {"625 MHz",           625},
                        {"650 MHz ",          650},
                        {"700 MHz",           700},
                        {"750 MHz",           750},
                    };
                config.ui.setting_page.vc = {
                        {"1025 mV",           1025},
                        {"1050 mV",           1050},
                        {"1100 mV",           1100},
                        {"1125 mV",           1125},
                        {"1150 mV (default)", 1150},
                        {"1175 mV",           1175},
                        {"1200 mV",           1200},
                        {"1225 mV ",          1225},
                    };
                config.create_power_instance     = create_qaxepp_2ph_power_instance;  // 2-phase
            } else { // NMQAXE_PLUS_PLUS_REV61
                config.display_name              = BOARD_NMQAXE_PLUS_PLUS_REV61_NAME;
                config.asic.default_frq          = 750;
                config.asic.default_vcore        = 1250;
                config.asic.min_vcore            = 1100;
                config.asic.max_vcore            = 1550;
                config.asic.diff_thr_init        = 1024;
                config.asic.job_interval_ms      = 500;
                config.ui.dashboard_page.power.ibus          = {0.0f, 18.0f};
                config.ui.dashboard_page.power.power         = {0.0f, 200.0f};
                config.ui.dashboard_page.performance.asic_freq_req  = {600.0f, 1100.0f};
                config.ui.dashboard_page.performance.vcore_req      = {1.10f, 1.500f};
                config.ui.dashboard_page.performance.vcore_measure  = {1.10f, 1.500f};
                config.ui.setting_page.oc = {
                        {"650 MHz ",          650},
                        {"675 MHz",           675},
                        {"700 MHz",           700},
                        {"750 MHz (default)", 750},
                        {"800 MHz",           800},
                        {"850 MHz",           850},
                        {"900 MHz",           900},
                        {"950 MHz",           950},
                        {"1000 MHz",          1000},
                    };
                config.ui.setting_page.vc = {
                        {"1150 mV",           1150},
                        {"1175 mV",           1175},
                        {"1200 mV",           1200},
                        {"1225 mV",           1225},
                        {"1250 mV (default)", 1250},
                        {"1275 mV",           1275},
                        {"1300 mV",           1300},
                        {"1350 mV",           1350},
                        {"1400 mV",           1400},
                        {"1450 mV",           1450},
                    };
                config.create_power_instance     = create_qaxepp_3ph_power_instance; // 3-phase
            }
            config.setup_temp_hal = [](AxePowerHal* pwr) {
                tps53647_register_vcore_temp_hal(static_cast<TPS53647Class*>(pwr));
                tmp102_register_asic_temp_hal();
            };
            config.asic.req_frq             = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ,    config.asic.default_frq);
            config.asic.req_vcore           = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, config.asic.default_vcore);
            config.ui.dashboard_page.power.vbus          = {0.0f, 15.0f};
            config.ui.dashboard_page.heat.mcu            = {0.0f, 75.0f};
            config.ui.dashboard_page.heat.asic           = {0.0f, 70.0f};
            config.ui.dashboard_page.heat.vcore          = {0.0f, 130.0f};
            config.ui.dashboard_page.heat.fan            = {0.0f, 5000.0f};
            config.btn.boot_pin              = 0;
            config.btn.user_pin              = -1; // Not used
            config.pwr.en_pins.pwr_pll_0v8   = 39;
            config.pwr.en_pins.pwr_vdd_1v8   = 40;
            config.pwr.en_pins.pwr_vcore     = 38;
            config.pwr.adc_pins.vbus         = 18;
            config.pwr.adc_pins.ibus         = 11;
            config.pwr.adc_pins.vcore        = 17;
            config.pwr.vcore_regulator_pin   = -1;  // Not used 
            config.pwr.pgood_pin             = 21;
            config.pwr.dc_plug_pin           = -1;  // Not used
            config.pwr.vbus_min_required     = 8000;// mV, minimum vbus voltage to start mining
            config.pwr.temp_limit.high       = 130.0f;
            config.pwr.temp_limit.medium     = 110.0f;
            config.pwr.temp_limit.low        = 80.0f;
            config.pwr.power_low_threshold   = 20.0f; // Watt
            config.iic.scl_pin               = 7;   
            config.iic.sda_pin               = 8;
            config.led.wifi_pin              = -1; // Not used
            config.led.pool_pin              = -1; // Not used
            config.led.sys_pin               = 9; 
            config.asic.rx_pin               = 44;
            config.asic.tx_pin               = 43;
            config.asic.rst_pin              = 45;
            config.asic.com_baud_init        = 115200;
            config.asic.com_baud_work        = 1000000;
            config.asic.com_port             = &Serial1;
            config.preference.screen.brightness    = 100;
            config.preference.screen.auto_rolling  = false;
            config.preference.screen.flip          = false;
            config.preference.screen.saver_enable  = true;
            config.preference.screen.saver_timeout = 15*60; // 15 minutes
            config.preference.led.enable           = true;
            config.create_asic_instance            = create_qaxepp_asic_instance;

            config.fans.clear();
            fan_cfg.id                        = 0;
            fan_cfg.init.pwm.pin              = 41;
            fan_cfg.init.pwm.ch               = 1;
            fan_cfg.init.pwm.freq             = 1000*100; // Hz
            fan_cfg.init.pwm.resolution       = 8;        // bits
            fan_cfg.init.torch.pulse_gpio_num = 42;
            fan_cfg.init.torch.ctrl_gpio_num  = PCNT_PIN_NOT_USED; // Not used
            fan_cfg.init.torch.lctrl_mode     = PCNT_MODE_KEEP;
            fan_cfg.init.torch.hctrl_mode     = PCNT_MODE_KEEP;
            fan_cfg.init.torch.pos_mode       = PCNT_COUNT_INC;
            fan_cfg.init.torch.neg_mode       = PCNT_COUNT_DIS;
            fan_cfg.init.torch.counter_h_lim  = 30000;
            fan_cfg.init.torch.counter_l_lim  = 0;
            fan_cfg.init.torch.unit           = PCNT_UNIT_0;
            fan_cfg.init.torch.channel        = PCNT_CHANNEL_0;
            fan_cfg.init.self_test_rpm_thr   = 1500;
            fan_cfg.init.danger_rpm_thr      = 100;
            fan_cfg.pid.Kp                   = 50.0f;
            fan_cfg.pid.Ki                   = 1.0f;
            fan_cfg.pid.Kd                   = 0.0f;
            fan_cfg.pid.prev_error           = 0;
            fan_cfg.pid.integral             = 0;
            fan_cfg.pid.output_min           = 0.00f;
            fan_cfg.pid.output_max           = 100.0f;
            fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_ASIC_FAN_SPEED, true);
            fan_cfg.target_temp              = String(nvs_config_get_string(NVS_CONFIG_ASIC_TARGET_TEMP, "30")).toFloat();
            config.fans.push_back(fan_cfg); // fan1 for asic cooling(required)

            fan_cfg.id                        = 1;
            fan_cfg.init.pwm.pin              = 10;
            fan_cfg.init.pwm.ch               = 2;
            fan_cfg.init.pwm.freq             = 1000*100; // Hz
            fan_cfg.init.pwm.resolution       = 8;        // bits
            fan_cfg.init.torch.pulse_gpio_num = 47;
            fan_cfg.init.torch.ctrl_gpio_num  = PCNT_PIN_NOT_USED; // Not used
            fan_cfg.init.torch.lctrl_mode     = PCNT_MODE_KEEP;
            fan_cfg.init.torch.hctrl_mode     = PCNT_MODE_KEEP;
            fan_cfg.init.torch.pos_mode       = PCNT_COUNT_INC;
            fan_cfg.init.torch.neg_mode       = PCNT_COUNT_DIS;
            fan_cfg.init.torch.counter_h_lim  = 30000;
            fan_cfg.init.torch.counter_l_lim  = 0;
            fan_cfg.init.torch.unit           = PCNT_UNIT_1;
            fan_cfg.init.torch.channel        = PCNT_CHANNEL_0;
            fan_cfg.init.self_test_rpm_thr   = 2000;
            fan_cfg.init.danger_rpm_thr      = 100;
            fan_cfg.pid.Kp                   = 50.0f;
            fan_cfg.pid.Ki                   = 1.0f;
            fan_cfg.pid.Kd                   = 0.0f;
            fan_cfg.pid.prev_error           = 0;
            fan_cfg.pid.integral             = 0;
            fan_cfg.pid.output_min           = 0.00f;
            fan_cfg.pid.output_max           = 100.0f;
            fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_VCORE_FAN_SPEED, true);
            fan_cfg.target_temp              = String(nvs_config_get_string(NVS_CONFIG_VCORE_TARGET_TEMP, "70")).toFloat();
            config.fans.push_back(fan_cfg); // fan2 for power cooling(optional)
            break;
        
        default:
                config.name                      = "Unknown";
                config.display_name              = "Unknown";
                config.asic.name                 = "Unknown";
                config.asic.job_interval_ms      = 0;
            break;
    }

    // ── Benchmark mode: override freq/vcore with current benchmark round values ──
    // Written by benchmark_thread_entry before each reboot; NOP in Normal mode.
    if (nvs_config_get_u8(NVS_CONFIG_BM_MODE, 0) == 1) {
        uint16_t bm_freq  = nvs_config_get_u16(NVS_CONFIG_BM_CUR_FREQ,  config.asic.req_frq);
        uint16_t bm_vcore = nvs_config_get_u16(NVS_CONFIG_BM_CUR_VCORE, config.asic.req_vcore);
        // Clamp to board limits for safety
        bm_vcore = (bm_vcore < config.asic.min_vcore) ? config.asic.min_vcore : bm_vcore;
        bm_vcore = (bm_vcore > config.asic.max_vcore) ? config.asic.max_vcore : bm_vcore;
        config.asic.req_frq   = bm_freq;
        config.asic.req_vcore = bm_vcore;
        LOG_W("[BM] Benchmark mode: freq=%dMHz vcore=%dmV", bm_freq, bm_vcore);
    }

    return config;
}

void hardware_pre_init(BoardSpecConfig config){
    Serial.setTimeout(20);
    Serial.begin(115200);
    delay(100);

    // PSRAM explicit init + sanity check.
    // Arduino-ESP32 initialises PSRAM automatically before setup(), but on boards
    // with non-standard PSRAM chips (e.g. QPI vs OPI) the silent auto-init can
    // succeed yet leave PSRAM in an unstable state.  Calling psramInit() here is
    // a no-op when PSRAM is already up, but on a bad chip it will log a clear
    // failure instead of a mysterious heap-corruption crash later.
    if (!psramFound()) {
        if (!psramInit()) {
            LOG_E("PSRAM init FAILED — heap_caps_malloc(MALLOC_CAP_SPIRAM) will silently fall through to internal RAM");
        } else {
            LOG_I("PSRAM init OK (explicit): size=%uKB", ESP.getPsramSize()/1024);
        }
    } else {
        LOG_I("PSRAM already initialised: size=%uKB free=%uKB", ESP.getPsramSize()/1024, ESP.getFreePsram()/1024);
    }

    // i2c init
    i2c_master_init(config.iic.sda_pin, config.iic.scl_pin, 400000);

    // nvs init
    esp_err_t ret = nvs_flash_init();
    while (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        LOG_W("NVS partition is full or has invalid version, erasing...");
        if(nvs_flash_erase() != ESP_OK){
            LOG_E("NVS partition erase failed");
        }
        LOG_I("Reinitializing NVS...");
        ret = nvs_flash_init();
        delay(1000);
    }

    if(config.name == BOARD_NMQAXE_PLUS_PLUS_NAME){
        // init extio chip tca9554
        tca9554_init();
        // LCD reset
        tca9554_set_io_level(TCA9554_IO_1, 0); 
        delay(10);
        tca9554_set_io_level(TCA9554_IO_1, 1); 
    }
}


