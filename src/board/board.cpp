#include <nvs_flash.h>
#include "nvs/nvs_config.h"
#include "utils/logger/logger.h"
#include "board.h"
#include "nmaxe.h"
#include "nmaxegamma.h"
#include "nmqaxepp.h"
#include "drivers/iic/i2c_master.h"
#include "drivers/extio/tca9554.h"
#include "drivers/power/axp2101/axp2101.h"
#include "drivers/temp/tmp102.h"
#include "drivers/temp/temp_hal.h"

BoardSpecConfig get_board_config_compile_time() {
    BoardSpecConfig config;
    fan_config_t fan_cfg;

#if defined(BOARD_NMAXE)
    config.name                      = "NMAxe";
    config.display_name              = "NMAxe";
    config.mdns_name                 = "nmaxe";
    config.asic.name                 = "BM1366";
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
    // Freq ceiling padded to 700 (default 575 is also the top OC option) so the frontend's
    // hardcoded value>=0.9*max "High" check doesn't fire at 88% margin on a stock default.
    config.ui.dashboard_page.performance.asic_freq_req  = {390.0f, 700.0f};
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
    // Bundled adapter: 12 V / 2 A (24 W). Tightest budget of the whole product
    // line — the single BM1366 already runs a large fraction of it at the stock
    // 575MHz/1250mV point, so treat any overclock here as an adapter question,
    // not just a hashrate question.
    config.pwr.adapter_max_w         = 24.0f;
    config.pwr.adapter_max_a         = 2.0f;
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
    // Regulator ceiling = vc list max (1300) + 100mV line-loss headroom for the power loop.
    config.asic.max_vcore            = 1400;
    config.asic.bm_freq_step         = 25;
    config.asic.bm_vcore_step        = 25;
    config.asic.diff_thr_init        = 512;
    config.asic.com_baud_init        = 115200;
    config.asic.com_baud_work        = 1000000;
    config.asic.com_port             = &Serial1;
    config.asic.hcn_max_ghs_per_ch   = 1000.0f;  // BM1366 ~500 GH/s, 2x margin
    // Measured: 575MHz applied with the die <~38C hangs core group 56..83 (-25%) until re-init.
    config.asic.warmup_frq           = 500;
    config.asic.warmup_temp          = 42.0f;
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
    // Thermal model per board/fan -> PID gains are derived, not typed here.
    fan_cfg.thermal                  = fan_plant_fixed_blower();
    fan_cfg.pid.output_min           = 0.0f;
    fan_cfg.pid.output_max           = 99.999f;
    fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_ASIC_FAN_SPEED, true);
    fan_cfg.target_temp              = fan_clamp_target_temp(nvs_config_get_string_value(NVS_CONFIG_ASIC_TARGET_TEMP, "40").toFloat(), config.asic.temp_limit.high);
    config.fans.clear();
    config.fans.push_back(fan_cfg); // single fan
    // The NMAxe blower is hardwired to full speed: the PWM is written but the
    // airflow never changes. Record 100 % in benchmark results, and skip the
    // benchmark fan takeover since there is nothing to take over.
    config.fan_controllable = false;

#elif defined(BOARD_NMAXE_GAMMA)
    config.name                      = "NMAxeGamma";
    config.display_name              = "NMAxeGamma";
    config.mdns_name                 = "nmaxegamma";
    config.asic.name                 = "BM1370";
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
    // Bundled adapter: 12 V / 3 A (36 W). Measured at the stock 600MHz/1125mV
    // point the board already draws ~2.8 A, i.e. ~92 % of the rating, so the
    // remaining headroom for overclocking is small.
    config.pwr.adapter_max_w         = 36.0f;
    config.pwr.adapter_max_a         = 3.0f;
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
    // Regulator ceiling = vc list max (1250) + 100mV line-loss headroom for the power loop.
    config.asic.max_vcore            = 1350;
    config.asic.bm_freq_step         = 25;
    config.asic.bm_vcore_step        = 25;
    config.asic.diff_thr_init        = 1024;
    config.asic.rx_pin               = 44;
    config.asic.tx_pin               = 43;
    config.asic.rst_pin              = 45;
    config.asic.com_baud_init        = 115200;
    config.asic.com_baud_work        = 1000000;
    config.asic.com_port             = &Serial1;
    config.asic.hcn_max_ghs_per_ch   = 3000.0f;  // BM1370 ~1.8 TH/s, ~1.7x margin
    config.preference.screen.brightness    = 100;
    config.preference.screen.auto_rolling  = false;
    config.preference.screen.flip          = true;
    config.preference.screen.saver_enable  = false;
    config.preference.screen.saver_timeout = 15*60; // 15 minutes

    config.preference.led.enable           = true;
    config.create_asic_instance      = create_gamma_asic_instance;
    config.create_power_instance     = create_gamma_power_instance;
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
    fan_cfg.init.self_test_rpm_thr    = 4000;
    fan_cfg.init.danger_rpm_thr       = 500;
    // Thermal model per board/fan -> PID gains are derived, not typed here.
    fan_cfg.thermal                   = fan_plant_gamma_asic();
    fan_cfg.pid.output_min            = 0.0f;
    fan_cfg.pid.output_max            = 99.999f;
    fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_ASIC_FAN_SPEED, true);
    fan_cfg.target_temp              = fan_clamp_target_temp(nvs_config_get_string_value(NVS_CONFIG_ASIC_TARGET_TEMP, "40").toFloat(), config.asic.temp_limit.high);
    config.fans.clear();
    config.fans.push_back(fan_cfg); // single fan

#elif defined(BOARD_NMQAXE_PP)
    config.name                      = "NMQAxe++";
    config.display_name              = "NMQAxe++";
    config.mdns_name                 = "nmaxepp";
    config.asic.name                 = "BM1370";
    config.asic.num_req              = 4;
    config.asic.temp_limit.high      = 75.0f;
    config.asic.temp_limit.medium    = 65.0f;
    config.asic.temp_limit.low       = 50.0f;
    config.tft.width                 = 240;
    config.tft.height                = 320;
    config.tft.dc_pin                = 3;
    config.tft.bl.pin                = 6;
    config.tft.bl.pwm_ch             = 0;
    config.tft.bl.pwm_freq           = 1000*100;
    config.tft.bl.pwm_resolution     = 8;
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
    config.asic.diff_thr_init        = 1024 * 1;
    config.asic.default_frq          = 600;
    config.asic.default_vcore        = 1150;
    config.asic.min_vcore            = 1100;
    // Regulator setpoint ceiling for the power loop's line-loss compensation — NOT the
    // user-facing cap. The user-selectable range is the vc list (1100..1250mV); this
    // stays 100mV above its last entry so the PID can push the TPS output past the
    // target to cover the rail-to-chip drop (same pattern as NMAxe 1400/1300,
    // Rev8.1 1500/1400).
    config.asic.max_vcore            = 1350;
    config.asic.bm_freq_step         = 10;
    config.asic.bm_vcore_step        = 10;
    config.asic.job_interval_ms      = 500;
    config.ui.dashboard_page.power.ibus          = {0.0f, 15.0f};
    config.ui.dashboard_page.power.power         = {0.0f, 160.0f};
    // Gauge ceiling 850 keeps the selectable max (725MHz) below the frontend's
    // hardcoded value>=0.9*max "High" check (0.9*850=765MHz).
    config.ui.dashboard_page.performance.asic_freq_req  = {500.0f, 850.0f};
    // Vcore floor matches the vc dropdown min (1100mV); ceiling padded to 1.45 so the
    // frontend's hardcoded value>=0.9*max "High" check (0.9*1450=1305mV) doesn't fire
    // anywhere in the selectable 1100..1250mV range.
    config.ui.dashboard_page.performance.vcore_req      = {1.10f, 1.45f};
    config.ui.dashboard_page.performance.vcore_measure  = {1.10f, 1.45f};
    config.ui.setting_page.oc = {
            {"515 MHz",           515},
            {"550 MHz",           550},
            {"575 MHz",           575},
            {"600 MHz (default)", 600},
            {"625 MHz",           625},
            {"650 MHz",           650},
            {"675 MHz",           675},
            {"700 MHz",           700},
            {"725 MHz",           725},
        };
    config.ui.setting_page.vc = {
            {"1100 mV",           1100},
            {"1125 mV",           1125},
            {"1150 mV (default)", 1150},
            {"1175 mV",           1175},
            {"1200 mV",           1200},
            {"1225 mV",           1225},
            {"1250 mV",           1250},
        };
    config.create_power_instance     = create_qaxepp_2ph_power_instance;
    config.setup_temp_hal = [](AxePowerHal* pwr) {
        tps53647_register_vcore_temp_hal(static_cast<TPS53647Class*>(pwr));
        tmp102_register_asic_temp_hal();
    };
    config.asic.req_frq             = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ,    config.asic.default_frq);
    config.asic.req_vcore           = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, config.asic.default_vcore);
    config.ui.dashboard_page.power.vbus          = {0.0f, 15.0f};
    config.ui.dashboard_page.heat.mcu            = {0.0f, 75.0f};
    config.ui.dashboard_page.heat.asic           = {0.0f, 70.0f};
    // VRM (Vcore) thermal limits derived from the regulator's OWN trip point, so
    // software always acts before the chip: fans reach full at 115 °C, the software
    // over-temperature escalation fires at 120 °C, the TPS53647 OT_FAULT trips at
    // 125 °C. These were hardcoded 130/110/80 with a 130 °C gauge ceiling — 5 °C
    // ABOVE the chip trip — so the rail could be shut down by hardware while the
    // firmware still reported everything as fine. Same class of bug already fixed
    // on Nexus; see QXPP_VRM_OT_FAULT_C in nmqaxepp.h.
    config.ui.dashboard_page.heat.vcore          = {0.0f, QXPP_VRM_OT_FAULT_C - FAN_TARGET_TEMP_MARGIN_C};
    config.ui.dashboard_page.heat.fan            = {0.0f, 5000.0f};
    config.btn.boot_pin              = 0;
    config.btn.user_pin              = -1;
    config.pwr.en_pins.pwr_pll_0v8   = 39;
    config.pwr.en_pins.pwr_vdd_1v8   = 40;
    config.pwr.en_pins.pwr_vcore     = 38;
    config.pwr.adc_pins.vbus         = 18;
    config.pwr.adc_pins.ibus         = 11;
    config.pwr.adc_pins.vcore        = 17;
    config.pwr.vcore_regulator_pin   = -1;
    config.pwr.pgood_pin             = 21;
    config.pwr.dc_plug_pin           = -1;
    config.pwr.vbus_min_required     = 8000;
    config.pwr.temp_limit.high       = QXPP_VRM_OT_FAULT_C - FAN_TARGET_TEMP_MARGIN_C;
    config.pwr.temp_limit.medium     = 110.0f;   // UI "hot" tint, 10 °C below high
    config.pwr.temp_limit.low        = 80.0f;
    config.pwr.power_low_threshold   = 20.0f;
    // Bundled adapter: 12 V / 10 A (120 W). Measured at 600MHz/1150mV the board
    // draws ~6.8 A (~68 %), leaving real headroom for the higher OC options.
    config.pwr.adapter_max_w         = 120.0f;
    config.pwr.adapter_max_a         = 10.0f;
    config.iic.scl_pin               = 7;
    config.iic.sda_pin               = 8;
    config.led.wifi_pin              = -1;
    config.led.pool_pin              = -1;
    config.led.sys_pin               = 9;
    config.asic.rx_pin               = 44;
    config.asic.tx_pin               = 43;
    config.asic.rst_pin              = 45;
    config.asic.com_baud_init        = 115200;
    config.asic.com_baud_work        = 1000000;
    config.asic.com_port             = &Serial1;
    config.asic.hcn_max_ghs_per_ch   = 3000.0f;  // BM1370 ~1.8 TH/s, ~1.7x margin
    config.preference.screen.brightness    = 100;
    config.preference.screen.auto_rolling  = false;
    config.preference.screen.flip          = false;
    config.preference.screen.saver_enable  = true;
    config.preference.screen.saver_timeout = 15*60;
    config.preference.led.enable           = true;
    config.create_asic_instance            = create_qaxepp_asic_instance;

    config.fans.clear();
    fan_cfg.id                        = 0;
    fan_cfg.init.pwm.pin              = 41;
    fan_cfg.init.pwm.ch               = 1;
    fan_cfg.init.pwm.freq             = 1000*100;
    fan_cfg.init.pwm.resolution       = 8;
    fan_cfg.init.torch.pulse_gpio_num = 42;
    fan_cfg.init.torch.ctrl_gpio_num  = PCNT_PIN_NOT_USED;
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
    // Thermal model per board/fan -> PID gains are derived, not typed here.
    fan_cfg.thermal                  = fan_plant_qaxepp2_asic();
    fan_cfg.pid.output_min           = 0.00f;
    fan_cfg.pid.output_max           = 100.0f;
    fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_ASIC_FAN_SPEED, true);
    fan_cfg.target_temp              = fan_clamp_target_temp(nvs_config_get_string_value(NVS_CONFIG_ASIC_TARGET_TEMP, "40").toFloat(), config.asic.temp_limit.high);
    config.fans.push_back(fan_cfg);

    fan_cfg.id                        = 1;
    fan_cfg.init.pwm.pin              = 10;
    fan_cfg.init.pwm.ch               = 2;
    fan_cfg.init.pwm.freq             = 1000*100;
    fan_cfg.init.pwm.resolution       = 8;
    fan_cfg.init.torch.pulse_gpio_num = 47;
    fan_cfg.init.torch.ctrl_gpio_num  = PCNT_PIN_NOT_USED;
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
    fan_cfg.thermal                  = fan_plant_qaxepp2_vcore();
    fan_cfg.pid.output_min           = 0.00f;
    fan_cfg.pid.output_max           = 100.0f;
    fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_VCORE_FAN_SPEED, true);
    fan_cfg.target_temp              = fan_clamp_target_temp(nvs_config_get_string_value(NVS_CONFIG_VCORE_TARGET_TEMP, "80").toFloat(), config.pwr.temp_limit.high);
    config.fans.push_back(fan_cfg);

#elif defined(BOARD_NMQAXE_PP_REV61)
    config.name                      = "NMQAxe++";
    config.display_name              = "NMQAxe++Rev6.1";
    config.mdns_name                 = "nmaxepp61";
    config.asic.name                 = "BM1370";
    config.asic.num_req              = 4;
    config.asic.temp_limit.high      = 75.0f;
    config.asic.temp_limit.medium    = 65.0f;
    config.asic.temp_limit.low       = 50.0f;
    config.tft.width                 = 240;
    config.tft.height                = 320;
    config.tft.dc_pin                = 3;
    config.tft.bl.pin                = 6;
    config.tft.bl.pwm_ch             = 0;
    config.tft.bl.pwm_freq           = 1000*100;
    config.tft.bl.pwm_resolution     = 8;
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
    config.asic.diff_thr_init        = 1024 * 1;
    config.asic.default_frq          = 750;
    config.asic.default_vcore        = 1250;
    config.asic.min_vcore            = 1100;
    // Regulator setpoint ceiling for the power loop's line-loss compensation
    // (thread_entry.cpp power_loop, tps53647.cpp set_vcore_voltage clamp) — NOT the
    // user-facing cap. The user-selectable max is the vc list's last entry (1350mV);
    // this must stay ~150mV above it so the PID can push the TPS output past the
    // target to cover the rail-to-chip drop (same pattern as NMQAxePP 1325/1225,
    // Rev8.1 1500/1400).
    config.asic.max_vcore            = 1500;
    config.asic.bm_freq_step         = 10;
    config.asic.bm_vcore_step        = 10;
    config.asic.job_interval_ms      = 500;
    config.ui.dashboard_page.power.ibus          = {0.0f, 18.0f};
    config.ui.dashboard_page.power.power         = {0.0f, 200.0f};
    // Freq ceiling padded to 900 (OC list tops out at 800, default 750) so the frontend's
    // hardcoded value>=0.9*max "High" check doesn't fire at/near the selectable range.
    config.ui.dashboard_page.performance.asic_freq_req  = {600.0f, 900.0f};
    // Vcore gauge ceiling 1.55 keeps the selectable max (1350mV) below the frontend's
    // hardcoded value>=0.9*max "High" check (0.9*1550=1395mV).
    config.ui.dashboard_page.performance.vcore_req      = {1.10f, 1.550f};
    config.ui.dashboard_page.performance.vcore_measure  = {1.10f, 1.550f};
    config.ui.setting_page.oc = {
            {"650 MHz ",          650},
            {"675 MHz",           675},
            {"700 MHz",           700},
            {"725 MHz",           725},
            {"750 MHz (default)", 750},
            {"775 MHz",           775},
            {"800 MHz",           800},
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
        };
    config.create_power_instance     = create_qaxepp61_3ph_power_instance;
    config.setup_temp_hal = [](AxePowerHal* pwr) {
        tps53647_register_vcore_temp_hal(static_cast<TPS53647Class*>(pwr));
        tmp102_register_asic_temp_hal();
    };
    config.asic.req_frq             = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ,    config.asic.default_frq);
    config.asic.req_vcore           = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, config.asic.default_vcore);
    config.ui.dashboard_page.power.vbus          = {0.0f, 15.0f};
    config.ui.dashboard_page.heat.mcu            = {0.0f, 75.0f};
    config.ui.dashboard_page.heat.asic           = {0.0f, 70.0f};
    // VRM (Vcore) thermal limits derived from the regulator's OWN trip point, so
    // software always acts before the chip: fans reach full at 115 °C, the software
    // over-temperature escalation fires at 120 °C, the TPS53647 OT_FAULT trips at
    // 125 °C. See the QAxe++ block above / QXPP_VRM_OT_FAULT_C in nmqaxepp.h.
    config.ui.dashboard_page.heat.vcore          = {0.0f, QXPP_VRM_OT_FAULT_C - FAN_TARGET_TEMP_MARGIN_C};
    config.ui.dashboard_page.heat.fan            = {0.0f, 5000.0f};
    config.btn.boot_pin              = 0;
    config.btn.user_pin              = -1;
    config.pwr.en_pins.pwr_pll_0v8   = 39;
    config.pwr.en_pins.pwr_vdd_1v8   = 40;
    config.pwr.en_pins.pwr_vcore     = 38;
    config.pwr.adc_pins.vbus         = 18;
    config.pwr.adc_pins.ibus         = 11;
    config.pwr.adc_pins.vcore        = 17;
    config.pwr.vcore_regulator_pin   = -1;
    config.pwr.pgood_pin             = 21;
    config.pwr.dc_plug_pin           = -1;
    config.pwr.vbus_min_required     = 8000;
    config.pwr.temp_limit.high       = QXPP_VRM_OT_FAULT_C - FAN_TARGET_TEMP_MARGIN_C;
    config.pwr.temp_limit.medium     = 110.0f;   // UI "hot" tint, 10 °C below high
    config.pwr.temp_limit.low        = 80.0f;
    config.pwr.power_low_threshold   = 20.0f;
    // Bundled adapter: 12 V / 12 A (144 W). Measured at 700MHz/1250mV the board
    // draws ~8.4 A (~70 %), leaving real headroom for the higher OC options.
    config.pwr.adapter_max_w         = 144.0f;
    config.pwr.adapter_max_a         = 12.0f;
    config.iic.scl_pin               = 7;
    config.iic.sda_pin               = 8;
    config.led.wifi_pin              = -1;
    config.led.pool_pin              = -1;
    config.led.sys_pin               = 9;
    config.asic.rx_pin               = 44;
    config.asic.tx_pin               = 43;
    config.asic.rst_pin              = 45;
    config.asic.com_baud_init        = 115200;
    config.asic.com_baud_work        = 1000000;
    config.asic.com_port             = &Serial1;
    config.asic.hcn_max_ghs_per_ch   = 3000.0f;  // BM1370 ~1.8 TH/s, ~1.7x margin
    config.preference.screen.brightness    = 100;
    config.preference.screen.auto_rolling  = false;
    config.preference.screen.flip          = false;
    config.preference.screen.saver_enable  = true;
    config.preference.screen.saver_timeout = 15*60;
    config.preference.led.enable           = true;
    config.create_asic_instance            = create_qaxepp_asic_instance;

    config.fans.clear();
    fan_cfg.id                        = 0;
    fan_cfg.init.pwm.pin              = 41;
    fan_cfg.init.pwm.ch               = 1;
    fan_cfg.init.pwm.freq             = 1000*100;
    fan_cfg.init.pwm.resolution       = 8;
    fan_cfg.init.torch.pulse_gpio_num = 42;
    fan_cfg.init.torch.ctrl_gpio_num  = PCNT_PIN_NOT_USED;
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
    // Thermal model per board/fan -> PID gains are derived, not typed here.
    fan_cfg.thermal                  = fan_plant_qaxepp3_asic();
    fan_cfg.pid.output_min           = 0.00f;
    fan_cfg.pid.output_max           = 100.0f;
    fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_ASIC_FAN_SPEED, true);
    fan_cfg.target_temp              = fan_clamp_target_temp(nvs_config_get_string_value(NVS_CONFIG_ASIC_TARGET_TEMP, "40").toFloat(), config.asic.temp_limit.high);
    config.fans.push_back(fan_cfg);

    fan_cfg.id                        = 1;
    fan_cfg.init.pwm.pin              = 10;
    fan_cfg.init.pwm.ch               = 2;
    fan_cfg.init.pwm.freq             = 1000*100;
    fan_cfg.init.pwm.resolution       = 8;
    fan_cfg.init.torch.pulse_gpio_num = 47;
    fan_cfg.init.torch.ctrl_gpio_num  = PCNT_PIN_NOT_USED;
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
    fan_cfg.thermal                  = fan_plant_qaxepp3_vcore();
    fan_cfg.pid.output_min           = 0.00f;
    fan_cfg.pid.output_max           = 100.0f;
    fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_VCORE_FAN_SPEED, true);
    fan_cfg.target_temp              = fan_clamp_target_temp(nvs_config_get_string_value(NVS_CONFIG_VCORE_TARGET_TEMP, "75").toFloat(), config.pwr.temp_limit.high);
    config.fans.push_back(fan_cfg);

#elif defined(BOARD_NMQAXE_PP_REV81)
    config.name                      = "NMQAxe++";     // runtime functional ID
    config.asic.name                 = "BM1373";
    config.display_name              = "NMQAxe++Rev8.1";
    config.mdns_name                 = "nmaxepp81";
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
    config.ui.hashrate_dist_page.max_x_hr  = 20000;
    config.ui.hashrate_dist_page.max_x_bars= 20;
    config.ui.hashrate_dist_page.count     = 0;
    config.asic.diff_thr_init        = 1024 * 2;
    config.asic.default_frq          = 325;
    config.asic.default_vcore        = 1000;
    config.asic.min_vcore            = 900;
    config.asic.max_vcore            = 1500;
    config.asic.bm_freq_step         = 10;
    config.asic.bm_vcore_step        = 10;
    config.asic.job_interval_ms      = 500;
    config.ui.dashboard_page.power.ibus          = {0.0f, 25.0f};
    config.ui.dashboard_page.power.power         = {0.0f, 250.0f};
    config.ui.dashboard_page.performance.asic_freq_req  = {280.0f, 900.0f};
    config.ui.dashboard_page.performance.vcore_req      = {0.9f, 1.5f};
    config.ui.dashboard_page.performance.vcore_measure  = {0.9f, 1.5f};
    config.ui.setting_page.oc = {
            {"300 MHz",           300},
            {"325 MHz(default)",  325},
            {"350 MHz",           350},
            {"375 MHz",           375},
            {"400 MHz",           400},
            {"425 MHz",           425},
            {"450 MHz",           450},
            {"475 MHz",           475},
            {"500 MHz",           500},
            {"525 MHz",           525},
            {"550 MHz",           550},
            {"575 MHz",           575},
            {"600 MHz",           600},
            {"625 MHz",           625},
            {"650 MHz",           650},
            {"675 MHz",           675},
            {"700 MHz",           700},
            {"725 MHz",           725},
            {"750 MHz",           750},
            {"775 MHz",           775},
            {"800 MHz",           800},
            {"825 MHz",           825},
            {"850 MHz",           850},
            {"875 MHz",           875},
            {"900 MHz",           900},
        };
    config.ui.setting_page.vc = {
            {"900 mV",            900},
            {"925 mV",            925},
            {"950 mV",            950},
            {"1000 mV (default)", 1000},
            {"1025 mV",           1025},
            {"1050 mV",           1050},
            {"1075 mV",           1075},
            {"1100 mV",           1100},
            {"1125 mV",           1125},
            {"1150 mV",           1150},
            {"1175 mV",           1175},
            {"1200 mV",           1200},
            {"1225 mV",           1225},
            {"1250 mV",           1250},
            {"1275 mV",           1275},
            {"1300 mV",           1300},
            {"1325 mV",           1325},
            {"1350 mV",           1350},
            {"1375 mV",           1375},
            {"1400 mV",           1400},
        };
    config.create_power_instance     = create_qaxepp81_4ph_power_instance; // 4-phase
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
    config.asic.hcn_max_ghs_per_ch   = 10000.0f;  // BM1373 ~4-5 TH/s, ~2x margin
    config.preference.screen.brightness    = 100;
    config.preference.screen.auto_rolling  = false;
    config.preference.screen.flip          = false;
    config.preference.screen.saver_enable  = true;
    config.preference.screen.saver_timeout = 15*60; // 15 minutes
    config.preference.led.enable           = true;
    config.create_asic_instance            = create_qaxepp81_asic_instance;

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
    // Thermal model per board/fan -> PID gains are derived, not typed here.
    fan_cfg.thermal                  = fan_plant_qaxepp3_asic();
    fan_cfg.pid.output_min           = 0.00f;
    fan_cfg.pid.output_max           = 100.0f;
    fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_ASIC_FAN_SPEED, true);
    fan_cfg.target_temp              = fan_clamp_target_temp(nvs_config_get_string_value(NVS_CONFIG_ASIC_TARGET_TEMP, "40").toFloat(), config.asic.temp_limit.high);
    config.fans.push_back(fan_cfg); // fan1 for asic cooling(required)

    fan_cfg.id                        = 1;
    fan_cfg.init.pwm.pin              = 10;
    fan_cfg.init.pwm.ch               = 2;
    fan_cfg.init.pwm.freq             = 1000*100;
    fan_cfg.init.pwm.resolution       = 8;
    fan_cfg.init.torch.pulse_gpio_num = 47;
    fan_cfg.init.torch.ctrl_gpio_num  = PCNT_PIN_NOT_USED;
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
    fan_cfg.thermal                  = fan_plant_qaxepp3_vcore();
    fan_cfg.pid.output_min           = 0.00f;
    fan_cfg.pid.output_max           = 100.0f;
    fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_VCORE_FAN_SPEED, true);
    fan_cfg.target_temp              = fan_clamp_target_temp(nvs_config_get_string_value(NVS_CONFIG_VCORE_TARGET_TEMP, "70").toFloat(), config.pwr.temp_limit.high);
    config.fans.push_back(fan_cfg);

#elif defined(BOARD_NMQAXE_PP_NEXUS)
    config.name                      = "NMQAxe++";     // runtime functional ID
    config.asic.name                 = "BM1373";
    config.display_name              = "NMQAxe++Nexus";
    config.mdns_name                 = "nmaxeppnexus";
    config.asic.num_req              = 2;
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
    config.ui.hashrate_dist_page.max_x_hr  = 20000;
    config.ui.hashrate_dist_page.max_x_bars= 20;
    config.ui.hashrate_dist_page.count     = 0;
    config.asic.diff_thr_init        = 1024 * 2;
    config.asic.default_frq          = 600;
    // Nexus's 2 BM1373 dies are Vcore-series-stacked (not parallel like Rev8.1), but the
    // series scaling is handled INSIDE TPS546D24AClass (vcore_series_count=2), so all the
    // values below are user-facing PER-DIE numbers (selectable range 900-1250mV/die).
    //
    // 1140, not 1150: the sweep grid is 950 + n*10 mV, so 1150 is never measured
    // and the Normal preset could never quote a benchmark row for the default
    // pair. 1140 is the nearest grid point below the old default (field-verified
    // on 124.88: 600MHz/1140mV qualified at 8258.8 GH/s).
    config.asic.default_vcore        = 1140;
    // Selectable floor lowered to 325MHz/900mV to allow low-power operation;
    // pairs this low are meant for ECO-style tuning, verify stability with a benchmark.
    config.asic.min_vcore            = 900;
    // Regulator ceiling = vc list max (1250/die) + 200mV/die line-loss headroom for the
    // power loop (series-stacked rail, so the rail-level headroom is 2x this).
    config.asic.max_vcore            = 1450;
    // Sweep granularity, tuned for THIS board's selectable lists.
    //
    // Freq 25 MHz, not 10: the OC list is 325..800 in 25 MHz steps, so a 25 MHz
    // sweep lands exactly on selectable points — every benchmark row can be
    // reproduced from Settings. A 10 MHz sweep produced 48 points of which only
    // 10 existed in the list, so 38 measured rows could not be re-selected by
    // hand. It also keeps the results partition comfortable: 20 rows instead of
    // 48 on a 24 KB NVS that already holds the configuration.
    //
    // Vcore 10 mV from a 950 mV start.
    //
    // 950 is where this board actually lands: measured across several Nexus units,
    // 325 MHz needs ~1000 mV, so a 900 mV start burns ~10 rounds per frequency
    // proving that 900-940 mV fail. Starting at 950 roughly halves the sweep.
    //
    // 10 mV (not 15) because at 950 the window is narrow enough for it to be
    // affordable: ~13 h for a full 20-point window instead of ~8 h at 15 mV, and
    // the stored value lands within 9 mV of the true floor instead of 14 mV.
    //
    // The start is bm_vcore_min, NOT min_vcore: min_vcore stays 900 because it is
    // the regulator setpoint floor and the over-temperature step-down floor, and
    // raising it would make the 900/925 mV dropdown entries unappliable.
    config.asic.bm_freq_step         = 25;
    config.asic.bm_vcore_step        = 10;
    config.asic.bm_vcore_min         = 950;
    config.asic.job_interval_ms      = 500;
    config.ui.dashboard_page.power.ibus          = {0.0f, 20.0f};
    config.ui.dashboard_page.power.power         = {0.0f, 200.0f};
    // Ceilings padded (freq 820->900, vcore 1.3->1.4) so the default 600MHz/1150mV point
    // stays well below the frontend's hardcoded value>=0.9*max "High" check.
    config.ui.dashboard_page.performance.asic_freq_req  = {325.0f, 900.0f};
    config.ui.dashboard_page.performance.vcore_req      = {0.9f, 1.4f};
    config.ui.dashboard_page.performance.vcore_measure  = {0.9f, 1.4f};
    config.ui.setting_page.oc = {
            {"325 MHz",           325},
            {"350 MHz",           350},
            {"375 MHz",           375},
            {"400 MHz",           400},
            {"425 MHz",           425},
            {"450 MHz",           450},
            {"475 MHz",           475},
            {"500 MHz",           500},
            {"525 MHz",           525},
            {"550 MHz",           550},
            {"575 MHz",           575},
            {"600 MHz(default)",  600},
            {"625 MHz",           625},
            {"650 MHz",           650},
            {"675 MHz",           675},
            {"700 MHz",           700},
            {"725 MHz",           725},
            {"750 MHz",           750},
            {"775 MHz",           775},
            {"800 MHz",           800}
        };
    // Per-die voltages; the series rail is derived inside the driver.
    // Floor lowered to 900mV alongside the 325MHz freq floor -- see min_vcore comment above.
    //
    // 10 mV steps, matching `bm_vcore_step` above.
    //
    // This list used to be a 25 mV grid (900, 925, 950, 975, 1000, 1025, ...) while
    // the sweep measured and stored the whole 10 mV grid. Every point the benchmark
    // qualified at 990/1190/1210/1220 was therefore unselectable by hand, and the
    // resume/over-temperature step-down - which lands on a SELECTABLE value - could
    // not reproduce a measured point either. Reported from the field on v3.2.02s:
    // 325/990 had to be typed in through the browser developer tools.
    //
    // Cost is UI-only: ~11 extra entries in the Advanced dropdown. `min_vcore` stays
    // 900 because it is the regulator setpoint floor, not the first sweep point.
    config.ui.setting_page.vc = {
            {"900 mV",             900},
            {"910 mV",             910},
            {"920 mV",             920},
            {"930 mV",             930},
            {"940 mV",             940},
            {"950 mV",             950},
            {"960 mV",             960},
            {"970 mV",             970},
            {"980 mV",             980},
            {"990 mV",             990},
            {"1000 mV",           1000},
            {"1010 mV",           1010},
            {"1020 mV",           1020},
            {"1030 mV",           1030},
            {"1040 mV",           1040},
            {"1050 mV",           1050},
            {"1060 mV",           1060},
            {"1070 mV",           1070},
            {"1080 mV",           1080},
            {"1090 mV",           1090},
            {"1100 mV",           1100},
            {"1110 mV",           1110},
            {"1120 mV",           1120},
            {"1130 mV",           1130},
            {"1140 mV(default)",  1140},
            {"1150 mV",           1150},
            {"1160 mV",           1160},
            {"1170 mV",           1170},
            {"1180 mV",           1180},
            {"1190 mV",           1190},
            {"1200 mV",           1200},
            {"1210 mV",           1210},
            {"1220 mV",           1220},
            {"1230 mV",           1230},
            {"1240 mV",           1240},
            {"1250 mV",           1250},
        };
    config.create_power_instance     = create_nexus_3ph_power_instance; // TPS546D24A, 3-phase (1 master + 2 slaves)
    config.setup_temp_hal = [](AxePowerHal* pwr) {
        tps546d24a_register_vcore_temp_hal(static_cast<TPS546D24AClass*>(pwr));
        tmp102_register_asic_temp_hal();
    };
    config.asic.req_frq             = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ,    config.asic.default_frq);
    config.asic.req_vcore           = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, config.asic.default_vcore);
    config.ui.dashboard_page.power.vbus          = {0.0f, 15.0f};
    config.ui.dashboard_page.heat.mcu            = {0.0f, 75.0f};
    config.ui.dashboard_page.heat.asic           = {0.0f, 70.0f};
    config.ui.dashboard_page.heat.vcore          = {0.0f, NEXUS_VRM_OT_FAULT_C - FAN_TARGET_TEMP_MARGIN_C};
    config.ui.dashboard_page.heat.fan            = {0.0f, 5000.0f};
    config.btn.boot_pin              = 0;
    config.btn.user_pin              = -1; // Not used
    config.pwr.en_pins.pwr_pll_0v8   = -1; // Not used
    config.pwr.en_pins.pwr_vdd_1v8   = -1; // Not used  
    config.pwr.en_pins.pwr_vcore     = 38;
    config.pwr.adc_pins.vbus         = 18;
    config.pwr.adc_pins.ibus         = 11;
    config.pwr.adc_pins.vcore        = 17;
    config.pwr.vcore_regulator_pin   = -1;  // Not used 
    config.pwr.pgood_pin             = 21;  
    config.pwr.dc_plug_pin           = -1;  // Not used
    config.pwr.vbus_min_required     = 8000;// mV, minimum vbus voltage to start mining
    // VRM (Vcore) thermal limits, derived from the regulator's OWN trip point.
    //
    // These used to be hardcoded 100/80/60, which sat ABOVE the TPS546D24A's
    // OT_FAULT_LIMIT of 85 °C. The two layers therefore disagreed:
    //   - the auto-fan clamp is (this high - FAN_TARGET_TEMP_MARGIN_C), i.e. it was
    //     95 °C, so a user-set target of 90 °C was accepted and the fans only went
    //     to full speed at 90 °C
    //   - but the regulator shuts its output down at 85 °C by itself
    // net effect: between 85 and 95 °C the rail was disabled by hardware while the
    // software believed everything was fine, and the resulting power-fault alert
    // confused the field report.
    //
    // Keeping the software limit one margin BELOW the chip trip means software
    // always acts first (fan ramp, then the 10 s over-temp escalation), and the
    // regulator trip remains a last-resort backstop that should never fire.
    config.pwr.temp_limit.high       = NEXUS_VRM_OT_FAULT_C - FAN_TARGET_TEMP_MARGIN_C;
    config.pwr.temp_limit.medium     = 70.0f;   // UI "hot" tint, 10 °C below high
    config.pwr.temp_limit.low        = 55.0f;
    config.pwr.power_low_threshold   = 20.0f; // Watt
    // Bundled adapter: 180 W / 15 A. Measured on the bench, the CURRENT is what
    // binds: at 785MHz/1210mV the rail sags to ~11.80 V, so 180 W would need
    // 15.25 A — already past the 15 A rating — while the reading shows 14.71 A
    // (98 % of 15 A) and only 96 % of 180 W. A wattage-only rule would therefore
    // call that point comfortable. Both numbers are recorded; the current one is
    // the primary check.
    config.pwr.adapter_max_w         = 180.0f;
    config.pwr.adapter_max_a         = 15.0f;
    // Nexus alone runs at 0.93 instead of the 0.95 default (13.95 A of 15 A).
    //
    // 0.90 was tried first and rejected the 750MHz/1200mV point, whose steady draw
    // under the user's fan policy is 13.59 A (90.6 %) — the same point the field
    // reported as acceptable. 0.93 accepts it with a real 7 % reserve and still
    // leaves the 785MHz/1210mV / 795MHz/1230mV class (14.7 A and up, 98 %+) clearly
    // outside. Note 775MHz/1220mV sits at 13.85 A (92.3 %) and therefore also
    // passes at this setting.
    //
    // Other boards keep the default: they never came near their rating, so
    // tightening them would only invalidate points they already qualified.
    config.pwr.adapter_budget        = 0.93f;
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
    config.asic.hcn_max_ghs_per_ch   = 10000.0f;  // BM1373 ~4-5 TH/s, ~2x margin
    config.preference.screen.brightness    = 100;
    config.preference.screen.auto_rolling  = false;
    config.preference.screen.flip          = false;
    config.preference.screen.saver_enable  = true;
    config.preference.screen.saver_timeout = 15*60; // 15 minutes
    config.preference.led.enable           = true;
    config.create_asic_instance            = create_qaxepp81_asic_instance;

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
    // Thermal model per board/fan -> PID gains are derived, not typed here.
    fan_cfg.thermal                  = fan_plant_nexus_asic();
    fan_cfg.pid.output_min           = 0.00f;
    fan_cfg.pid.output_max           = 100.0f;
    fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_ASIC_FAN_SPEED, true);
    // 55 °C, NOT the 40 °C the single-fan boards use.
    //
    // 40 °C is UNREACHABLE here, so it pins the loop at 100 % for the life of the
    // unit: measured on 124.2 (750 MHz/1190 mV, ~160 W) the ASIC fan holds
    // 50.8-52 °C at 100 %, and the identification run at 700/1150 (~135 W) gave
    // 44.89 °C at 100 %, 49.10 °C at 70 %, 58.05 °C at 50 %. Any target below
    // ~45 °C is therefore past the end of the fan's authority: the duty saturates
    // and the "temperature" is just whatever full speed achieves.
    //
    // 55 °C settles around 64 % duty at 160 W (36 points of reserve) and stays
    // 20 °C below the 75 °C ASIC limit.
    //
    // The two fans share one air path, so this number is NOT free-standing: a
    // colder ASIC target starves the VRM. Measured at 160 W, ASIC target 52 left
    // the VRM fan at 100 %, unable to reach its own 65 °C. See the Vcore target
    // below for the pair that works.
    //
    // Users who set a lower target on purpose are unaffected: this is only the
    // default for a board whose NVS has no value yet.
    fan_cfg.target_temp              = fan_clamp_target_temp(nvs_config_get_string_value(NVS_CONFIG_ASIC_TARGET_TEMP, "55").toFloat(), config.asic.temp_limit.high);
    config.fans.push_back(fan_cfg); // fan1 for asic cooling(required)

    fan_cfg.id                        = 1;
    fan_cfg.init.pwm.pin              = 10;
    fan_cfg.init.pwm.ch               = 2;
    fan_cfg.init.pwm.freq             = 1000*100;
    fan_cfg.init.pwm.resolution       = 8;
    fan_cfg.init.torch.pulse_gpio_num = 47;
    fan_cfg.init.torch.ctrl_gpio_num  = PCNT_PIN_NOT_USED;
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
    fan_cfg.thermal                  = fan_plant_nexus_vcore();
    fan_cfg.pid.output_min           = 0.00f;
    fan_cfg.pid.output_max           = 100.0f;
    fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_VCORE_FAN_SPEED, true);
    // 70 °C, not 65 °C.
    //
    // 65 °C is not reachable once the ASIC fan stops running flat out: measured
    // at 160 W with the ASIC target at 52/55, the VRM fan sat at 100 % and still
    // read 67-68 °C. A target the loop cannot reach is worse than no target - the
    // duty pins at 100 %, the soak's cooling-headroom rule then rejects EVERY
    // point, and ECO/Turbo disappear from the UI.
    //
    // 70 °C holds with ~15 % duty in reserve at 160 W and sits 15 °C below the
    // TPS546D24A's own OT_FAULT (85 °C, i.e. pwr.temp_limit.high is 80). It is
    // above the chip's OT_WARN (75 °C) by design, so no warning is produced.
    fan_cfg.target_temp              = fan_clamp_target_temp(nvs_config_get_string_value(NVS_CONFIG_VCORE_TARGET_TEMP, "70").toFloat(), config.pwr.temp_limit.high);
    config.fans.push_back(fan_cfg);

#else
    #error "No board model defined. Add -D BOARD_<model> in platformio.ini"
#endif

    return config;
}

void hardware_pre_init(const BoardSpecConfig& config){
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

#if defined(BOARD_NMQAXE_PP) || defined(BOARD_NMQAXE_PP_REV61) || defined(BOARD_NMQAXE_PP_REV81) || defined(BOARD_NMQAXE_PP_NEXUS)
        // init PMU (AXP2101)
        axp2101_init();
        // init extio chip tca9554
        tca9554_init();
        // LCD reset
        tca9554_set_io_level(TCA9554_IO_1, 0); 
        delay(10);
        tca9554_set_io_level(TCA9554_IO_1, 1); 
        delay(10);
#elif defined(BOARD_NMAXE_GAMMA) || defined(BOARD_NMAXE) 
      
#else
    #error "No board model defined. Add -D BOARD_<model> in platformio.ini"
#endif


}


