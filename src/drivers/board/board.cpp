#include <nvs_flash.h>
#include "nvs_config.h"
#include "logger.h"
#include "board.h"
#include "nmaxe.h"
#include "nmaxegamma.h"
#include "nmqaxepp.h"
// #include "Wire.h"
#include "i2c_master.h"
#include "tca9554.h"

BoardModelType get_board_model(){
    BoardModelType model = BOARD_UNKNOWN;
    pinMode(NM_AXE_MODEL_SELECT_PIN0, INPUT_PULLUP);
    pinMode(NM_AXE_MODEL_SELECT_PIN1, INPUT_PULLUP);
    delay(100); //wait for pin stable
    uint8_t sel0 = digitalRead(NM_AXE_MODEL_SELECT_PIN0);
    uint8_t sel1 = digitalRead(NM_AXE_MODEL_SELECT_PIN1);
    // 0b11 NMAXE
    // 0b01 NMAXE_GAMMA
    // 0b10 NMQAXE++
    // 0b00 BOARD_UNKNOWN
    model = static_cast<BoardModelType>((sel0 << 1) | sel1);
    return model;

    // return NMQAXE_PLUS_PLUS;
}

BoardSpecConfig get_board_config(BoardModelType model) {
    BoardSpecConfig config;
    fan_config_t fan_cfg;
    switch(model) {
        case NMAXE:
            config.name                      = BOARD_NMAXE_NAME;
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
            config.ui.hashrate_dist_page.times     = 0;

            config.ui.dashboard_page.power.vbus.min        = 0.0f;
            config.ui.dashboard_page.power.vbus.max        = 15.0f;
            config.ui.dashboard_page.power.ibus.min        = 0.0f;
            config.ui.dashboard_page.power.ibus.max        = 4.0f;
            config.ui.dashboard_page.power.power.min       = 0.0f;
            config.ui.dashboard_page.power.power.max       = 30.0f;
            config.ui.dashboard_page.heat.mcu.min          = 0.0f;
            config.ui.dashboard_page.heat.mcu.max          = 75.0f;
            config.ui.dashboard_page.heat.asic.min         = 0.0f;
            config.ui.dashboard_page.heat.asic.max         = 80.0f;
            config.ui.dashboard_page.heat.vcore.min        = 0.0f;
            config.ui.dashboard_page.heat.vcore.max        = 100.0f;
            config.ui.dashboard_page.heat.fan.min          = 0.0f;
            config.ui.dashboard_page.heat.fan.max          = 9000.0f;
            config.ui.dashboard_page.performance.asic_freq_req.min    = 390.0f;
            config.ui.dashboard_page.performance.asic_freq_req.max    = 650.0f;
            config.ui.dashboard_page.performance.vcore_req.min        = 1.000f;
            config.ui.dashboard_page.performance.vcore_req.max        = 1.500f;
            config.ui.dashboard_page.performance.vcore_measure.min    = 1.000f;
            config.ui.dashboard_page.performance.vcore_measure.max    = 1.500f;

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
            config.preference.fan.is_auto_speed    = true;
            config.preference.led.enable           = true;
            config.preference.asic.target_temp     = 45;
            config.create_asic_instance      = create_axe_asic_instance;
            config.create_power_instance     = create_axe_power_instance;

            fan_cfg.id                       = 0;
            fan_cfg.init.pwm_pin             = 41;
            fan_cfg.init.torch_pin           = 42;
            fan_cfg.init.self_test_rpm_thr   = 4000;
            fan_cfg.init.danger_rpm_thr      = 500;
            fan_cfg.init.pwm_ch              = 2;
            fan_cfg.init.pwm_freq            = 1000*100; // Hz
            fan_cfg.init.pwm_resolution      = 8;        // bits
            fan_cfg.init.p_cnt_h_limt        = 30000;    // PCNT high limit value
            fan_cfg.pid.Kp                   = 50.0f;
            fan_cfg.pid.Ki                   = 1.0f;
            fan_cfg.pid.Kd                   = 0.0f;
            fan_cfg.pid.prev_error           = 0;
            fan_cfg.pid.integral             = 0;
            fan_cfg.pid.output_min           = 25.0f;
            fan_cfg.pid.output_max           = 99.999f;
            config.fans.clear();
            config.fans.push_back(fan_cfg); // single fan
            break;
        case NMAXE_GAMMA:
            config.name                      = BOARD_NMAXE_GAMMA_NAME;
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
            config.ui.hashrate_dist_page.times     = 0;

            config.ui.dashboard_page.power.vbus.min        = 0.0f;
            config.ui.dashboard_page.power.vbus.max        = 15.0f;
            config.ui.dashboard_page.power.ibus.min        = 0.0f;
            config.ui.dashboard_page.power.ibus.max        = 5.0f;
            config.ui.dashboard_page.power.power.min       = 0.0f;
            config.ui.dashboard_page.power.power.max       = 50.0f;
            config.ui.dashboard_page.heat.mcu.min          = 0.0f;
            config.ui.dashboard_page.heat.mcu.max          = 75.0f;
            config.ui.dashboard_page.heat.asic.min         = 0.0f;
            config.ui.dashboard_page.heat.asic.max         = 80.0f;
            config.ui.dashboard_page.heat.vcore.min        = 0.0f;
            config.ui.dashboard_page.heat.vcore.max        = 100.0f;
            config.ui.dashboard_page.heat.fan.min          = 0.0f;
            config.ui.dashboard_page.heat.fan.max          = 9000.0f;
            config.ui.dashboard_page.performance.asic_freq_req.min    = 390.0f;
            config.ui.dashboard_page.performance.asic_freq_req.max    = 800.0f;
            config.ui.dashboard_page.performance.vcore_req.min        = 0.900f;
            config.ui.dashboard_page.performance.vcore_req.max        = 1.500f;
            config.ui.dashboard_page.performance.vcore_measure.min    = 0.900f;
            config.ui.dashboard_page.performance.vcore_measure.max    = 1.500f;

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
            config.preference.fan.is_auto_speed    = true;
            config.preference.led.enable           = true;
            config.preference.asic.target_temp     = 45;
            config.create_asic_instance      = create_gamma_asic_instance;
            config.create_power_instance     = create_gamma_power_instance;

            fan_cfg.id                       = 0;
            fan_cfg.init.pwm_pin             = 41;
            fan_cfg.init.torch_pin           = 42;
            fan_cfg.init.self_test_rpm_thr   = 4000;
            fan_cfg.init.danger_rpm_thr      = 500;
            fan_cfg.init.pwm_ch              = 2;
            fan_cfg.init.pwm_freq            = 1000*100; // Hz
            fan_cfg.init.pwm_resolution      = 8;        // bits
            fan_cfg.init.p_cnt_h_limt        = 30000;    // PCNT high limit value
            fan_cfg.pid.Kp                   = 50.0f;
            fan_cfg.pid.Ki                   = 1.0f;
            fan_cfg.pid.Kd                   = 0.0f;
            fan_cfg.pid.prev_error           = 0;
            fan_cfg.pid.integral             = 0;
            fan_cfg.pid.output_min           = 25.0f;
            fan_cfg.pid.output_max           = 99.999f;
            config.fans.clear();
            config.fans.push_back(fan_cfg); // single fan
            break;
        case NMQAXE_PLUS_PLUS:
            config.name                      = BOARD_NMQAXE_PLUS_PLUS_NAME;
            config.asic.name                 = CHIP_NMQAXE_PLUS_PLUS_NAME;
            config.asic.num_req              = 4;
            config.asic.temp_limit.high      = 75.0f;
            config.asic.temp_limit.medium    = 65.0f;
            config.asic.temp_limit.low       = 50.0f;
            config.asic.job_interval_ms      = 500;
            config.tft.width                 = 240;
            config.tft.height                = 320;
            config.tft.dc_pin                = 3;
            config.tft.bl.pin                = 6;
            config.tft.bl.pwm_ch             = 0;
            config.tft.bl.pwm_freq           = 1000*100; // Hz
            config.tft.bl.pwm_resolution     = 8;        // bits
            config.tft.rst_pin               = -1;       
            config.tft.pwr_pin               = -1;
            config.tft.color_invert          = false;
            config.spi.cs_pin                = -1;
            config.spi.miso_pin              = 2;
            config.spi.mosi_pin              = 1;
            config.spi.sclk_pin              = 5;
            config.ui.hashrate_dist_page.max_x_hr  = 10000;
            config.ui.hashrate_dist_page.max_x_bars= 20;
            config.ui.hashrate_dist_page.times     = 0;

            config.ui.dashboard_page.power.vbus.min        = 0.0f;
            config.ui.dashboard_page.power.vbus.max        = 15.0f;
            config.ui.dashboard_page.power.ibus.min        = 0.0f;
            config.ui.dashboard_page.power.ibus.max        = 11.0f;
            config.ui.dashboard_page.power.power.min       = 0.0f;
            config.ui.dashboard_page.power.power.max       = 130.0f;
            config.ui.dashboard_page.heat.mcu.min          = 0.0f;
            config.ui.dashboard_page.heat.mcu.max          = 75.0f;
            config.ui.dashboard_page.heat.asic.min         = 0.0f;
            config.ui.dashboard_page.heat.asic.max         = 80.0f;
            config.ui.dashboard_page.heat.vcore.min        = 0.0f;
            config.ui.dashboard_page.heat.vcore.max        = 130.0f;
            config.ui.dashboard_page.heat.fan.min          = 0.0f;
            config.ui.dashboard_page.heat.fan.max          = 4000.0f;
            config.ui.dashboard_page.performance.asic_freq_req.min    = 390.0f;
            config.ui.dashboard_page.performance.asic_freq_req.max    = 800.0f;
            config.ui.dashboard_page.performance.vcore_req.min        = 0.900f;
            config.ui.dashboard_page.performance.vcore_req.max        = 1.500f;
            config.ui.dashboard_page.performance.vcore_measure.min    = 0.900f;
            config.ui.dashboard_page.performance.vcore_measure.max    = 1.500f;

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
            config.iic.scl_pin               = 7;   
            config.iic.sda_pin               = 8;
            config.led.wifi_pin              = -1; // Not used
            config.led.pool_pin              = -1; // Not used
            config.led.sys_pin               = -1; // Not used
            config.asic.default_frq          = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, 600);
            config.asic.default_vcore        = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, 1225);
            config.asic.req_frq              = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, config.asic.default_frq);
            config.asic.req_vcore            = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, config.asic.default_vcore);
            config.asic.min_vcore            = 1000;
            config.asic.max_vcore            = 1500;
            config.asic.diff_thr_init        = 1024*2;
            config.asic.rx_pin               = 44;
            config.asic.tx_pin               = 43;
            config.asic.rst_pin              = 45;
            config.asic.com_baud_init        = 115200;
            config.asic.com_baud_work        = 1000000;
            config.asic.com_port             = &Serial1;
            config.preference.screen.brightness    = 100;
            config.preference.screen.auto_rolling  = false;
            config.preference.screen.flip          = false;
            config.preference.fan.is_auto_speed    = true;
            config.preference.led.enable           = true;
            config.preference.asic.target_temp     = 30;
            config.create_asic_instance      = create_qaxepp_asic_instance;
            config.create_power_instance     = create_qaxepp_power_instance;
            
            config.fans.clear();
            fan_cfg.id                       = 0;
            fan_cfg.init.pwm_pin             = 41;
            fan_cfg.init.torch_pin           = 42;
            fan_cfg.init.self_test_rpm_thr   = 2000;
            fan_cfg.init.danger_rpm_thr      = 300;
            fan_cfg.init.pwm_ch              = 2;
            fan_cfg.init.pwm_freq            = 1000*100; // Hz
            fan_cfg.init.pwm_resolution      = 8;        // bits
            fan_cfg.init.p_cnt_h_limt        = 30000;    // PCNT high limit value
            fan_cfg.pid.Kp                   = 50.0f;
            fan_cfg.pid.Ki                   = 1.0f;
            fan_cfg.pid.Kd                   = 0.0f;
            fan_cfg.pid.prev_error           = 0;
            fan_cfg.pid.integral             = 0;
            fan_cfg.pid.output_min           = 1.0f;
            fan_cfg.pid.output_max           = 100.0f;
            config.fans.push_back(fan_cfg); // fan1 

            // fan_cfg.id                       = 1;
            // fan_cfg.init.pwm_pin             = 41;
            // fan_cfg.init.torch_pin           = 42;
            // fan_cfg.init.self_test_rpm_thr   = 4000;
            // fan_cfg.init.danger_rpm_thr      = 300;
            // fan_cfg.init.pwm_ch              = 2;
            // fan_cfg.init.pwm_freq            = 1000*100; // Hz
            // fan_cfg.init.pwm_resolution      = 8;        // bits
            // fan_cfg.init.p_cnt_h_limt        = 30000;    // PCNT high limit value
            // fan_cfg.pid.Kp                   = 50.0f;
            // fan_cfg.pid.Ki                   = 1.0f;
            // fan_cfg.pid.Kd                   = 0.0f;
            // fan_cfg.pid.prev_error           = 0;
            // fan_cfg.pid.integral             = 0;
            // fan_cfg.pid.output_min           = 25.0f;
            // fan_cfg.pid.output_max           = 99.999f;
            // config.fans.push_back(fan_cfg); // fan2
            break;
        default:
            config.name                      = "Unknown";
            config.asic.name                 = "Unknown";
            config.asic.job_interval_ms      = 0;
            break;
    }
    
    return config;
}

void hardware_pre_init(BoardSpecConfig config){
    Serial.setTimeout(20);
    Serial.begin(115200);
    delay(100);

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
        // reset lcd via extio_1
        tca9554_init();
    }
}


