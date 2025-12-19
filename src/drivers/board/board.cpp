#include "board.h"
#include "nvs_config.h"
#include "esp_log.h"
#include <nvs_flash.h>
#include "logger.h"

BoardModelType get_board_model(){
    BoardModelType model = BOARD_UNKNOWN;
    pinMode(NM_AXE_MODEL_SELECT_PIN0, INPUT_PULLUP);
    pinMode(NM_AXE_MODEL_SELECT_PIN1, INPUT_PULLUP);
    delay(100); //wait for pin stable
    uint8_t sel0 = digitalRead(NM_AXE_MODEL_SELECT_PIN0);
    uint8_t sel1 = digitalRead(NM_AXE_MODEL_SELECT_PIN1);
    // 0b11 NMAXE
    // 0b01 NMAXE_GAMMA
    // 0b10 NMQAXE
    // 0b00 BOARD_UNKNOWN
    model = static_cast<BoardModelType>((sel0 << 1) | sel1);
    return model;
}

BMxxx* create_bm1366_instance(HardwareSerial& serial, uint32_t baud, uint8_t rx, uint8_t tx, uint8_t rst) {
    return new BM1366(serial, baud, rx, tx, rst);
}

BMxxx* create_bm1370_instance(HardwareSerial& serial, uint32_t baud, uint8_t rx, uint8_t tx, uint8_t rst) {
    return new BM1370(serial, baud, rx, tx, rst);
}

BoardSpecConfig get_board_config(BoardModelType model) {
    BoardSpecConfig config;
    
    switch(model) {
        case NMAXE:
            config.name                      = "NMAxe";
            config.asic.name                 = "BM1366";
            config.asic.job_interval_ms      = 2000;
            config.ui.hr_dist_max_x_hr       = 1000;
            config.ui.hr_dist_max_x_bars     = 20;
            config.asic.default_frq          = 575;
            config.asic.default_vcore        = 1250;
            config.asic.min_vcore            = 1100;
            config.asic.max_vcore            = 1300;
            config.asic.diff_thr_init        = 256;
            config.fan.pwm_pin               = 41;
            config.fan.torch_pin             = 42;
            config.fan.self_test_rpm_thr     = 4000; 
            config.btn.boot_pin              = 0;
            config.btn.user_pin              = 12;
            config.asic.rx_pin               = 44;
            config.asic.tx_pin               = 43;
            config.asic.rst_pin              = 45;
            config.pwr.enable_pins.pwr_0v8   = 13;
            config.pwr.enable_pins.pwr_1v8   = 14;
            config.pwr.enable_pins.pwr_vcore = 10;
            config.pwr.adc_pins.vbus         = 2;
            config.pwr.adc_pins.ibus         = 3;
            config.pwr.adc_pins.vcore        = 1;
            config.pwr.vcore_regulator_pin   = 16;    
            config.pwr.pgood_pin             = 21;
            config.pwr.dc_plug_pin           = 11;
            config.iic.scl_pin               = 8;   
            config.iic.sda_pin               = 9;
            config.led.wifi_pin              = 6;
            config.led.pool_pin              = 4;
            config.led.sys_pin               = 5;
            config.asic.com_port             = &Serial1;
            config.create_asic_instance      = create_bm1366_instance;
            break;
        case NMAXE_GAMMA:
            config.name                      = "NMAxeGamma";
            config.asic.name                 = "BM1370";
            config.asic.job_interval_ms      = 500;
            config.ui.hr_dist_max_x_hr       = 2000;
            config.ui.hr_dist_max_x_bars     = 20;
            config.asic.default_frq          = 600;
            config.asic.default_vcore        = 1125;
            config.asic.min_vcore            = 1000;
            config.asic.max_vcore            = 1250;
            config.asic.diff_thr_init        = 512;
            config.fan.pwm_pin               = 41;
            config.fan.torch_pin             = 42;
            config.fan.self_test_rpm_thr     = 4000; 
            config.btn.boot_pin              = 0;
            config.btn.user_pin              = 12;
            config.asic.rx_pin               = 44;
            config.asic.tx_pin               = 43;
            config.asic.rst_pin              = 45;
            config.pwr.enable_pins.pwr_0v8   = 13;
            config.pwr.enable_pins.pwr_1v8   = 14;
            config.pwr.enable_pins.pwr_vcore = 10;
            config.pwr.adc_pins.vbus         = 2;
            config.pwr.adc_pins.ibus         = 3;
            config.pwr.adc_pins.vcore        = 1;
            config.pwr.vcore_regulator_pin   = 16;    
            config.pwr.pgood_pin             = 21;
            config.pwr.dc_plug_pin           = 11;
            config.iic.scl_pin               = 8;   
            config.iic.sda_pin               = 9;
            config.led.wifi_pin              = 6;
            config.led.pool_pin              = 4;
            config.led.sys_pin               = 5;
            config.asic.com_port             = &Serial1;
            config.create_asic_instance      = create_bm1370_instance;
            break;
        case NMQAXE_PLUS_PLUS:
            config.name                      = "NMQAxe++";
            config.asic.name                 = "BM1370";
            config.asic.job_interval_ms      = 500;

            config.asic.com_port             = &Serial1;
            break;
        default:
            config.name                           = "Unknown";
            config.asic.name                 = "Unknown";
            config.asic.job_interval_ms      = 0;
            break;
    }
    
    return config;
}

void hardware_pre_init(void){
    Serial.setTimeout(20);
    Serial.begin(115200);
    delay(100);

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
}


