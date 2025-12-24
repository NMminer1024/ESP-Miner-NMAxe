#include "tps53647.h"
#include "logger.h"


/** For NMQAxe++ **/
#define TPS53647_I2C_ADDRESS            0x71//todo 

#define REG_IBUS_SAMPLE                 (0.01f)
#define GAIN_IBUS_SAMPLE                (50.0f)
#define GAIN_VBUS_SAMPLE                (6.1f)
#define GAIN_VCORE_SAMPLE               (2.0f)


TPS53647Class::~TPS53647Class(){

}

uint8_t TPS53647Class::_read_reg(uint8_t registerAddress, uint8_t *data, uint8_t length) {
    Wire.beginTransmission(TPS53647_I2C_ADDRESS); 
    Wire.write(registerAddress); 
    if (Wire.endTransmission() != 0) { 
        return 1; 
    }

    Wire.requestFrom(TPS53647_I2C_ADDRESS, length); 
    uint8_t index = 0;
    while (Wire.available() && index < length) {
        data[index++] = Wire.read(); 
    }

    if (index != length) {
        return 2; 
    }
    return 0; 
}

void TPS53647Class::_write_reg(uint8_t registerAddress, uint8_t data) {
    Wire.beginTransmission(TPS53647_I2C_ADDRESS); 
    Wire.write(registerAddress); 
    Wire.write(data); 
    Wire.endTransmission(); 
}


bool TPS53647Class::init(void){
    this->_adc_ready = AxePowerHal::init();
    pinMode(this->_vcore_pgood_pin, INPUT_PULLUP);
    pinMode(this->_dc_plug_pin, INPUT_PULLUP);
    return this->_adc_ready;
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
    //if not, that might be usb pd plug
    return (digitalRead(this->_dc_plug_pin) == HIGH);
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
        digitalWrite(this->_asic_pwr_en_pins.pwr_vcore, HIGH);
    } else {
        digitalWrite(this->_asic_pwr_en_pins.pwr_vcore, LOW);
    }
}

void TPS53647Class::set_vcore_voltage(uint16_t req_mv){
    uint16_t pwm = 0;
    //pwm = 0.14*req_mv - 140
    // if (req_mv >= this->_vcore_min_mv && req_mv <= this->_vcore_max_mv) {
    //     pwm = 0.14 * (req_mv) - 140; // bias 140, linear 0.14
    // } else {
    //     pwm = 0; //for safety
    //     LOG_E("Vcore request %dmV out of range %d-%d mV", req_mv, this->_vcore_min_mv, this->_vcore_max_mv);
    // }
    // ledcWrite(VCORE_REGULATOR_PWM_CHANNEL, pwm);
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