#include "board.h"

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
            config.name                           = "NMAxe";
            config.asic_spec.name                 = "BM1366";
            config.asic_spec.job_interval_ms      = 2000;
            config.ui_spec.hr_dist_max_x_hr       = 1000;
            config.ui_spec.hr_dist_max_x_bars     = 20;
            config.asic_spec.default_frq          = 550;
            config.asic_spec.default_vcore        = 1200;
            config.asic_spec.min_vcore            = 1100;
            config.asic_spec.max_vcore            = 1300;
            config.asic_spec.diff_thr_init        = 512;
            config.fan_spec.pwm_pin               = 41;
            config.fan_spec.torch_pin             = 42;
            config.fan_spec.self_test_rpm_thr     = 4000; 
            config.btn_spec.boot_pin              = 0;
            config.btn_spec.user_pin              = 12;
            config.asic_spec.rx_pin               = 44;
            config.asic_spec.tx_pin               = 43;
            config.asic_spec.rst_pin              = 45;
            config.pwr_pins.enable_pins.pwr_0v8   = 13;
            config.pwr_pins.enable_pins.pwr_1v8   = 14;
            config.pwr_pins.enable_pins.pwr_vcore = 10;
            config.pwr_pins.adc_pins.vbus         = 2;
            config.pwr_pins.adc_pins.ibus         = 3;
            config.pwr_pins.adc_pins.vcore        = 1;
            config.pwr_pins.vcore_regulator_pin   = 16;    
            config.pwr_pins.pgood_pin             = 21;
            config.pwr_pins.dc_plug_pin           = 11;
            config.iic_pins.scl_pin               = 8;   
            config.iic_pins.sda_pin               = 9;
            config.create_asic_instance           = create_bm1366_instance;
            break;
            
        case NMAXE_GAMMA:
            config.name                           = "NMAxeGamma";
            config.asic_spec.name                 = "BM1370";
            config.asic_spec.job_interval_ms      = 500;
            config.ui_spec.hr_dist_max_x_hr       = 2000;
            config.ui_spec.hr_dist_max_x_bars     = 20;
            config.asic_spec.default_frq          = 600;
            config.asic_spec.default_vcore        = 1125;
            config.asic_spec.min_vcore            = 1000;
            config.asic_spec.max_vcore            = 1250;
            config.asic_spec.diff_thr_init        = 512;
            config.fan_spec.pwm_pin               = 41;
            config.fan_spec.torch_pin             = 42;
            config.fan_spec.self_test_rpm_thr     = 4000; 
            config.btn_spec.boot_pin              = 0;
            config.btn_spec.user_pin              = 12;
            config.asic_spec.rx_pin               = 44;
            config.asic_spec.tx_pin               = 43;
            config.asic_spec.rst_pin              = 45;
            config.pwr_pins.enable_pins.pwr_0v8   = 13;
            config.pwr_pins.enable_pins.pwr_1v8   = 14;
            config.pwr_pins.enable_pins.pwr_vcore = 10;
            config.pwr_pins.adc_pins.vbus         = 2;
            config.pwr_pins.adc_pins.ibus         = 3;
            config.pwr_pins.adc_pins.vcore        = 1;
            config.pwr_pins.vcore_regulator_pin   = 16;    
            config.pwr_pins.pgood_pin             = 21;
            config.pwr_pins.dc_plug_pin           = 11;
            config.iic_pins.scl_pin               = 8;   
            config.iic_pins.sda_pin               = 9;
            config.create_asic_instance           = create_bm1370_instance;
            break;
        default:
            config.name                           = "Unknown";
            config.asic_spec.name                 = "Unknown";
            config.asic_spec.job_interval_ms      = 0;
            break;
    }
    
    return config;
}




