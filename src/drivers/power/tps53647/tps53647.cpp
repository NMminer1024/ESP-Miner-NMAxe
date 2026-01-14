#include "tps53647.h"
#include "logger.h"
#include <Wire.h>

/** For NMQAxe++ **/
#define TPS53647_I2C_ADDRESS            (0x71)//todo 

#define REG_IBUS_SAMPLE                 (0.01f)
#define GAIN_IBUS_SAMPLE                (50.0f)
#define GAIN_VBUS_SAMPLE                (6.1f)
#define GAIN_VCORE_SAMPLE               (2.0f)


TPS53647Class::~TPS53647Class(){

}

uint8_t TPS53647Class::_read_reg(uint8_t  regaddr, uint8_t *data, uint8_t length) {
    Wire.beginTransmission(TPS53647_I2C_ADDRESS); 
    Wire.write(regaddr); 
    if (Wire.endTransmission() != 0) { 
        return 1; 
    }

    Wire.requestFrom(static_cast<uint8_t>(TPS53647_I2C_ADDRESS), (uint8_t)length); 
    uint8_t index = 0;
    while (Wire.available() && index < length) {
        data[index++] = Wire.read(); 
    }

    if (index != length) {
        return 2; 
    }
    return 0; 
}

void TPS53647Class::_write_reg(uint8_t regaddr, uint8_t data) {
    Wire.beginTransmission(TPS53647_I2C_ADDRESS); 
    Wire.write(regaddr); 
    Wire.write(data); 
    Wire.endTransmission(); 
}

uint8_t TPS53647Class::_mv_to_vid(uint16_t mv){
    if (mv == 0.0f) return 0x00;

    int reg = (int) ((mv - this->_chip_min_output_vlot_mv) / 5.0f) + 1;

    // Ensure the register value is within the valid range (0x01 to 0xFF)
    if (reg > 0xFF) {
        LOG_E("Requested voltage %dmV is out of range", mv);
        reg = 0;
    }
    LOG_D("Converted %dmV to VID 0x%02X", mv, reg);
    return reg;
}

float TPS53647Class::_vid_to_mv(uint8_t reg){
    if (reg == 0x00) return 0.0f;
    float mv = (reg - 1) * 5.0f + this->_chip_min_output_vlot_mv;
    LOG_W("Converted VID 0x%02X to %dmV", reg, (uint16_t)mv);
    return mv;
}


bool TPS53647Class::init(void){
    pinMode(this->_vcore_pgood_pin, INPUT_PULLUP);

    this->_adc_ready = AxePowerHal::init();
    this->set_vcore_status(PWR_OFF);
    delay(10);
    this->set_vcore_status(PWR_ON);
    delay(10);
    
    // Establish communication with regulator
    uint16_t device_code = 0x0000;
    this->_read_reg(PMBUS_MFR_SPECIFIC_44, (uint8_t*)&device_code, 2);
    LOG_I("TPS53647 Device ID: 0x%04X", device_code);

    if (device_code != 0x01f0) {
        LOG_E("ERROR- cannot find TPS53647 buck controller");
        return false;
    }
    LOG_I("TPS53647 buck controller initialized");

    return true;
}

bool TPS53647Class::is_adc_ready(void){
    return this->_adc_ready;
}

bool TPS53647Class::is_vcore_ready(void){
    if(digitalRead(this->_vcore_pgood_pin) == HIGH){
        delay(1);
        return (digitalRead(this->_vcore_pgood_pin) == HIGH);
    }
    return false;
}

bool TPS53647Class::is_dc_pluged(void){
    // always return true, only dc input supported
    return true;    
}

void TPS53647Class::set_vdd_1v8(power_state_t state){
    if(-1 == this->_asic_pwr_en_pins.pwr_vdd_1v8) return;
    if(state == PWR_OFF){
        digitalWrite(this->_asic_pwr_en_pins.pwr_vdd_1v8, LOW);
    } else {
        digitalWrite(this->_asic_pwr_en_pins.pwr_vdd_1v8, HIGH);
    }
}

void TPS53647Class::set_pll_0v8(power_state_t state){
    if(-1 == this->_asic_pwr_en_pins.pwr_pll_0v8) return;

    if(state == PWR_OFF){
        digitalWrite(this->_asic_pwr_en_pins.pwr_pll_0v8, LOW);
    } else {
        digitalWrite(this->_asic_pwr_en_pins.pwr_pll_0v8, HIGH);
    }
}

void TPS53647Class::set_vcore_status(power_state_t state){
    if(-1 == this->_asic_pwr_en_pins.pwr_vcore) return;
    if(state == PWR_OFF){
        digitalWrite(this->_asic_pwr_en_pins.pwr_vcore, LOW);
    } else {
        digitalWrite(this->_asic_pwr_en_pins.pwr_vcore, HIGH);
    }
}

void TPS53647Class::set_vcore_voltage(uint16_t req_mv){
    if(req_mv == 0) {
        this->set_vcore_status(PWR_OFF);
        return;
    }

    this->set_vcore_status(PWR_ON);
    uint16_t vlot_mv = (req_mv <= this->_vcore_min_mv) ? this->_vcore_min_mv : ((req_mv >= this->_vcore_max_mv) ? this->_vcore_max_mv : req_mv);

    uint8_t reg = this->_mv_to_vid(vlot_mv);

    this->_write_reg(PMBUS_VOUT_COMMAND, reg); //VCORE Voltage Set Register   

}

void TPS53647Class::set_vcore_range(uint16_t min_mv, uint16_t max_mv){
    this->_vcore_min_mv = min_mv;
    this->_vcore_max_mv = max_mv;
}

uint32_t TPS53647Class::get_vbus(void){
    uint32_t vadc = this->get_vbus_adc();
    LOG_D("Vbus %dmv", (uint32_t)(vadc));
    return (uint32_t)(vadc * GAIN_VBUS_SAMPLE);
}

uint32_t TPS53647Class::get_ibus(void){
    uint32_t vadc = this->get_ibus_adc();
    LOG_D("ibus %dmv", vadc);
    float real = (float)vadc / GAIN_IBUS_SAMPLE;
    uint32_t current = (uint32_t)(real / REG_IBUS_SAMPLE); //0.01 ohm sampling resistor
    return current;
}

uint32_t TPS53647Class::get_vcore(void){
    uint32_t vadc = this->get_vcore_adc();
    LOG_D("vcore %dmv", vadc);
    return (vadc * GAIN_VCORE_SAMPLE);
}