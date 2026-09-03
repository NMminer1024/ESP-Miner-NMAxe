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
    config.asic.diff_thr_init        = 512;
    config.asic.com_baud_init        = 115200;
    config.asic.com_baud_work        = 1000000;
    config.asic.com_port             = &Serial1;
    config.asic.hcn_max_ghs_per_ch   = 1000.0f;  // BM1366 ~500 GH/s, 2x margin
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
    fan_cfg.pid.output_min           = 0.0f;
    fan_cfg.pid.output_max           = 99.999f;
    fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_ASIC_FAN_SPEED, true);
    fan_cfg.target_temp              = nvs_config_get_string_value(NVS_CONFIG_ASIC_TARGET_TEMP, "30").toFloat();
    config.fans.clear();
    config.fans.push_back(fan_cfg); // single fan

#elif defined(BOARD_NMAXE_GAMMA)
    config.name                      = "NMAxeGamma";
    config.display_name              = "NMAxeGamma";
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
    fan_cfg.pid.Kp                    = 50.0f;
    fan_cfg.pid.Ki                    = 1.0f;
    fan_cfg.pid.Kd                    = 0.0f;
    fan_cfg.pid.prev_error            = 0;
    fan_cfg.pid.integral              = 0;
    fan_cfg.pid.output_min            = 0.0f;
    fan_cfg.pid.output_max            = 99.999f;
    fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_ASIC_FAN_SPEED, true);
    fan_cfg.target_temp              = nvs_config_get_string_value(NVS_CONFIG_ASIC_TARGET_TEMP, "30").toFloat();
    config.fans.clear();
    config.fans.push_back(fan_cfg); // single fan

#elif defined(BOARD_NMQAXE_PP)
    config.name                      = "NMQAxe++";
    config.display_name              = "NMQAxe++";
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
    config.asic.min_vcore            = 1000;
    config.asic.max_vcore            = 1350;
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
    config.ui.dashboard_page.heat.vcore          = {0.0f, 130.0f};
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
    config.pwr.temp_limit.high       = 130.0f;
    config.pwr.temp_limit.medium     = 110.0f;
    config.pwr.temp_limit.low        = 80.0f;
    config.pwr.power_low_threshold   = 20.0f;
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
    fan_cfg.pid.Kp                   = 50.0f;
    fan_cfg.pid.Ki                   = 1.0f;
    fan_cfg.pid.Kd                   = 0.0f;
    fan_cfg.pid.prev_error           = 0;
    fan_cfg.pid.integral             = 0;
    fan_cfg.pid.output_min           = 0.00f;
    fan_cfg.pid.output_max           = 100.0f;
    fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_ASIC_FAN_SPEED, true);
    fan_cfg.target_temp              = nvs_config_get_string_value(NVS_CONFIG_ASIC_TARGET_TEMP, "30").toFloat();
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
    fan_cfg.pid.Kp                   = 50.0f;
    fan_cfg.pid.Ki                   = 1.0f;
    fan_cfg.pid.Kd                   = 0.0f;
    fan_cfg.pid.prev_error           = 0;
    fan_cfg.pid.integral             = 0;
    fan_cfg.pid.output_min           = 0.00f;
    fan_cfg.pid.output_max           = 100.0f;
    fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_VCORE_FAN_SPEED, true);
    fan_cfg.target_temp              = nvs_config_get_string_value(NVS_CONFIG_VCORE_TARGET_TEMP, "70").toFloat();
    config.fans.push_back(fan_cfg);

#elif defined(BOARD_NMQAXE_PP_REV61)
    config.name                      = "NMQAxe++";
    config.display_name              = "NMQAxe++Rev6.1";
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
    config.asic.max_vcore            = 1550;
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
    config.ui.dashboard_page.heat.vcore          = {0.0f, 130.0f};
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
    config.pwr.temp_limit.high       = 130.0f;
    config.pwr.temp_limit.medium     = 110.0f;
    config.pwr.temp_limit.low        = 80.0f;
    config.pwr.power_low_threshold   = 20.0f;
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
    fan_cfg.pid.Kp                   = 50.0f;
    fan_cfg.pid.Ki                   = 1.0f;
    fan_cfg.pid.Kd                   = 0.0f;
    fan_cfg.pid.prev_error           = 0;
    fan_cfg.pid.integral             = 0;
    fan_cfg.pid.output_min           = 0.00f;
    fan_cfg.pid.output_max           = 100.0f;
    fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_ASIC_FAN_SPEED, true);
    fan_cfg.target_temp              = nvs_config_get_string_value(NVS_CONFIG_ASIC_TARGET_TEMP, "30").toFloat();
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
    fan_cfg.pid.Kp                   = 50.0f;
    fan_cfg.pid.Ki                   = 1.0f;
    fan_cfg.pid.Kd                   = 0.0f;
    fan_cfg.pid.prev_error           = 0;
    fan_cfg.pid.integral             = 0;
    fan_cfg.pid.output_min           = 0.00f;
    fan_cfg.pid.output_max           = 100.0f;
    fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_VCORE_FAN_SPEED, true);
    fan_cfg.target_temp              = nvs_config_get_string_value(NVS_CONFIG_VCORE_TARGET_TEMP, "70").toFloat();
    config.fans.push_back(fan_cfg);

#elif defined(BOARD_NMQAXE_PP_REV81)
    config.name                      = "NMQAxe++";     // runtime functional ID
    config.asic.name                 = "BM1373";
    config.display_name              = "NMQAxe++Rev8.1";
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
    fan_cfg.pid.Kp                   = 50.0f;
    fan_cfg.pid.Ki                   = 1.0f;
    fan_cfg.pid.Kd                   = 0.0f;
    fan_cfg.pid.prev_error           = 0;
    fan_cfg.pid.integral             = 0;
    fan_cfg.pid.output_min           = 0.00f;
    fan_cfg.pid.output_max           = 100.0f;
    fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_ASIC_FAN_SPEED, true);
    fan_cfg.target_temp              = nvs_config_get_string_value(NVS_CONFIG_ASIC_TARGET_TEMP, "30").toFloat();
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
    fan_cfg.pid.Kp                   = 50.0f;
    fan_cfg.pid.Ki                   = 1.0f;
    fan_cfg.pid.Kd                   = 0.0f;
    fan_cfg.pid.prev_error           = 0;
    fan_cfg.pid.integral             = 0;
    fan_cfg.pid.output_min           = 0.00f;
    fan_cfg.pid.output_max           = 100.0f;
    fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_VCORE_FAN_SPEED, true);
    fan_cfg.target_temp              = nvs_config_get_string_value(NVS_CONFIG_VCORE_TARGET_TEMP, "70").toFloat();
    config.fans.push_back(fan_cfg);

#elif defined(BOARD_NMQAXE_PP_NEXUS)
    config.name                      = "NMQAxe++";     // runtime functional ID
    config.asic.name                 = "BM1373";
    config.display_name              = "NMQAxe++Nexus";
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
    fan_cfg.pid.Kp                   = 50.0f;
    fan_cfg.pid.Ki                   = 1.0f;
    fan_cfg.pid.Kd                   = 0.0f;
    fan_cfg.pid.prev_error           = 0;
    fan_cfg.pid.integral             = 0;
    fan_cfg.pid.output_min           = 0.00f;
    fan_cfg.pid.output_max           = 100.0f;
    fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_ASIC_FAN_SPEED, true);
    fan_cfg.target_temp              = nvs_config_get_string_value(NVS_CONFIG_ASIC_TARGET_TEMP, "30").toFloat();
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
    fan_cfg.pid.Kp                   = 50.0f;
    fan_cfg.pid.Ki                   = 1.0f;
    fan_cfg.pid.Kd                   = 0.0f;
    fan_cfg.pid.prev_error           = 0;
    fan_cfg.pid.integral             = 0;
    fan_cfg.pid.output_min           = 0.00f;
    fan_cfg.pid.output_max           = 100.0f;
    fan_cfg.auto_speed               = nvs_config_get_u16(NVS_CONFIG_AUTO_VCORE_FAN_SPEED, true);
    fan_cfg.target_temp              = nvs_config_get_string_value(NVS_CONFIG_VCORE_TARGET_TEMP, "70").toFloat();
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


