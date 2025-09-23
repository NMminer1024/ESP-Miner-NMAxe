#include "board.h"

board_model_t get_board_model(){
    board_model_t model = BOARD_UNKNOWN;
    pinMode(NM_AXE_MODEL_SELECT_PIN0, INPUT_PULLUP);
    pinMode(NM_AXE_MODEL_SELECT_PIN1, INPUT_PULLUP);
    delay(100); //wait for pin stable
    uint8_t sel0 = digitalRead(NM_AXE_MODEL_SELECT_PIN0);
    uint8_t sel1 = digitalRead(NM_AXE_MODEL_SELECT_PIN1);
    // 0b11 NMAXE
    // 0b01 NMAXE_GAMMA
    // 0b10 NMQAXE
    // 0b00 BOARD_UNKNOWN
    model = static_cast<board_model_t>((sel0 << 1) | sel1);
    return model;
}

BMxxx* create_bm1366_instance(HardwareSerial& serial, uint32_t baud, uint8_t rx, uint8_t tx, uint8_t rst) {
    return new BM1366(serial, baud, rx, tx, rst);
}

BMxxx* create_bm1370_instance(HardwareSerial& serial, uint32_t baud, uint8_t rx, uint8_t tx, uint8_t rst) {
    return new BM1370(serial, baud, rx, tx, rst);
}

BoardConfig get_board_config(board_model_t model) {
    BoardConfig config;
    
    switch(model) {
        case NMAXE:
            config.name                           = "NMAxe";
            config.asic_spec.name                 = "BM1366";
            config.asic_spec.job_interval_ms      = 2000;
            config.max_x_hr                       = 1000;
            config.max_x_bars                     = 20;
            config.asic_spec.default_frq          = 550;
            config.asic_spec.default_vcore        = 1200;
            config.asic_spec.min_vcore            = 1100;
            config.asic_spec.max_vcore            = 1300;
            config.asic_spec.diff_thr_init        = 512;
            config.asic_pins.rx_pin               = NM_AXE_ESP32_RX_TO_BM13xx;
            config.asic_pins.tx_pin               = NM_AXE_ESP32_TX_TO_BM13xx;
            config.asic_pins.rst_pin              = NM_AXE_ESP32_RST_TO_BM13xx;
            config.pwr_pins.enable_pins.pwr_0v8   = NM_AXE_POWER_BM13xx_VPLL_ENABLE_PIN;
            config.pwr_pins.enable_pins.pwr_1v8   = NM_AXE_POWER_BM13xx_VDD_ENABLE_PIN;
            config.pwr_pins.enable_pins.pwr_vcore = NM_AXE_POWER_BM13xx_VCORE_ENABLE_PIN;
            config.pwr_pins.adc_pins.vbus         = NM_AXE_POWER_BM13xx_VBUS_ADC_PIN;
            config.pwr_pins.adc_pins.ibus         = NM_AXE_POWER_BM13xx_IBUS_ADC_PIN;
            config.pwr_pins.adc_pins.vcore        = NM_AXE_POWER_BM13xx_VCORE_ADC_PIN;
            config.pwr_pins.vcore_regulator_pin   = NM_AXE_POWER_BM13xx_VCORE_REGULATOR_PWM_PIN;    
            config.pwr_pins.pgood_pin             = NM_AXE_POWER_BM13xx_VCORE_P_GOOD_DET_PIN;
            config.pwr_pins.dc_plug_pin           = NM_AXE_POWER_BM13xx_VBUS_PLUG_SENSE_DET_PIN;
            config.create_asic_instance           = create_bm1366_instance;
            break;
            
        case NMAXE_GAMMA:
            config.name                           = "NMAxeGamma";
            config.asic_spec.name                 = "BM1370";
            config.asic_spec.job_interval_ms      = 500;
            config.max_x_hr                       = 2000;
            config.max_x_bars                     = 20;
            config.asic_spec.default_frq          = 600;
            config.asic_spec.default_vcore        = 1125;
            config.asic_spec.min_vcore            = 1000;
            config.asic_spec.max_vcore            = 1250;
            config.asic_spec.diff_thr_init        = 512;
            config.asic_pins.rx_pin               = NM_AXE_ESP32_RX_TO_BM13xx;
            config.asic_pins.tx_pin               = NM_AXE_ESP32_TX_TO_BM13xx;
            config.asic_pins.rst_pin              = NM_AXE_ESP32_RST_TO_BM13xx;
            config.pwr_pins.enable_pins.pwr_0v8   = NM_AXE_POWER_BM13xx_VPLL_ENABLE_PIN;
            config.pwr_pins.enable_pins.pwr_1v8   = NM_AXE_POWER_BM13xx_VDD_ENABLE_PIN;
            config.pwr_pins.enable_pins.pwr_vcore = NM_AXE_POWER_BM13xx_VCORE_ENABLE_PIN;
            config.pwr_pins.adc_pins.vbus         = NM_AXE_POWER_BM13xx_VBUS_ADC_PIN;
            config.pwr_pins.adc_pins.ibus         = NM_AXE_POWER_BM13xx_IBUS_ADC_PIN;
            config.pwr_pins.adc_pins.vcore        = NM_AXE_POWER_BM13xx_VCORE_ADC_PIN;
            config.pwr_pins.vcore_regulator_pin   = NM_AXE_POWER_BM13xx_VCORE_REGULATOR_PWM_PIN;    
            config.pwr_pins.pgood_pin             = NM_AXE_POWER_BM13xx_VCORE_P_GOOD_DET_PIN;
            config.pwr_pins.dc_plug_pin           = NM_AXE_POWER_BM13xx_VBUS_PLUG_SENSE_DET_PIN;
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




