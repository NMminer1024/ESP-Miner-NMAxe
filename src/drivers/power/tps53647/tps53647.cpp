#include "tps53647.h"
#include "logger.h"
#include <driver/i2c.h>

/** For NMQAxe++ **/
#define TPS53647_I2C_ADDRESS            (0x71)
#define I2C_MASTER_NUM                  I2C_NUM_0
#define I2C_MASTER_TIMEOUT_MS           1000
#define ACK_CHECK_EN                    0x1
#define ACK_VAL                         0x0
#define NACK_VAL                        0x1

#define REG_IBUS_SAMPLE                 (0.01f)
#define GAIN_IBUS_SAMPLE                (50.0f)
#define GAIN_VBUS_SAMPLE                (6.1f)
#define GAIN_VCORE_SAMPLE               (2.0f)


#define IMAX                            (20)      // Maximum current in A
#define IFAULT                          (25.0f)   // Over current fault limit in A
#define NUM_PHASE                       (2)       // Number of phases used



TPS53647Class::~TPS53647Class(){

}

uint8_t TPS53647Class::_read_reg(uint8_t  regaddr, uint8_t *data, uint8_t length) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    
    // Write register address
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TPS53647_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, regaddr, ACK_CHECK_EN);
    
    // Repeated start and read data
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TPS53647_I2C_ADDRESS << 1) | I2C_MASTER_READ, ACK_CHECK_EN);
    
    if (length > 1) {
        i2c_master_read(cmd, data, length - 1, (i2c_ack_type_t)ACK_VAL);
    }
    i2c_master_read_byte(cmd, data + length - 1, (i2c_ack_type_t)NACK_VAL);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        LOG_E("TPS53647 read register 0x%02X failed: %d", regaddr, ret);
        return 1;
    }
    return 0;
}

void TPS53647Class::_write_byte(uint8_t regaddr, uint8_t data) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TPS53647_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, regaddr, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, data, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        LOG_E("TPS53647 write register 0x%02X failed: %d", regaddr, ret);
    }
}

void TPS53647Class::_write_cmd(uint8_t cmd) {
    i2c_cmd_handle_t command = i2c_cmd_link_create();
    
    i2c_master_start(command);
    i2c_master_write_byte(command, (TPS53647_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write_byte(command, cmd, ACK_CHECK_EN);
    i2c_master_stop(command);
    
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, command, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(command);
    
    if (ret != ESP_OK) {
        LOG_E("TPS53647 write command 0x%02X failed: %d", cmd, ret);
    }
}

void TPS53647Class::_write_word(uint8_t regaddr, uint16_t data){
    esp_err_t err = ESP_FAIL;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, TPS53647_I2C_ADDRESS << 1 | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, regaddr, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, (uint8_t) (data & 0x00FF), ACK_CHECK_EN);
    i2c_master_write_byte(cmd, (uint8_t) ((data & 0xFF00) >> 8), NACK_VAL);
    i2c_master_stop(cmd);
    err = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);

    if(err != ESP_OK){
        LOG_E("TPS53647 write word register 0x%02X failed: %d", regaddr, err);
    }
}

uint8_t TPS53647Class::_mv_to_vid(uint16_t mv){
    if (mv == 0.0f) return 0x00;

    float vlot = (float)mv / 1000.0f;

    int reg = (int) ((vlot - this->_chip_min_output_vlot_mv) / 0.005f) + 1;

    // Ensure the register value is within the valid range (0x01 to 0xFF)
    if (reg > 0xFF) {
        LOG_E("Requested voltage %dmV is out of range", mv);
        reg = 0;
    }
    LOG_D("Converted %dmV to VID 0x%02X", mv, reg);
    return reg;
}

uint16_t TPS53647Class::_vid_to_mv(uint8_t reg){
    if (reg == 0x00) return 0.0f;
    float vlot = (reg - 1) * 0.005f + this->_chip_min_output_vlot_mv;

    LOG_W("Converted VID 0x%02X to %dmV", reg, (uint16_t)(vlot*1000.0f));
    return (uint16_t)(vlot*1000.0f);
}

/**
 * @brief Convert a float value into an SLINEAR11
 */
uint16_t TPS53647Class::_float_to_slinear11(float x){
    if (x <= 0.0f) {
        LOG_W("No negative numbers at this time");
        return 0;
    }
    int32_t e = -16;
    int32_t m;
    while (e <= 15) {
        float scale = powf(2.0f, (float) e);
        float temp = x / scale;
        m = (int32_t) roundf(temp);
        if (m >= 0 && m <= 1023) {
            break;
        }
        e++;
    }
    if (e > 15) {
        LOG_W("Could not find a solution");
        return 0;
    }
    uint16_t mantissa_bits = (uint16_t) m & 0x7FF;
    uint16_t exponent_bits = (uint16_t) (e & 0x1F);
    uint16_t value = (exponent_bits << 11) | mantissa_bits;
    return value;
}

/**
 * @brief Convert an SLINEAR11 value into an int
 */
float TPS53647Class::_slinear11_to_float(uint16_t value){
    // 5 bits exponent in two's complement
    int32_t exponent = value >> 11;

    // 11 bits mantissa in two's complement
    int32_t mantissa = value & 0x7ff;

    // extend signs
    exponent |= (exponent & 0x10) ? 0xffffffe0 : 0;
    mantissa |= (mantissa & 0x400) ? 0xfffff800 : 0;

    // calculate result (mantissa * 2^exponent)
    return mantissa * powf(2.0, exponent);
}

void TPS53647Class::_set_phases(int num_phases){
    if (num_phases < 1 || num_phases > 6) {
        LOG_E("number of phases out of range: %d", num_phases);
        return;
    }
    LOG_I("TPS53647 setting %d phases", num_phases);
    this->_write_byte(PMBUS_MFR_SPECIFIC_20, (uint8_t) (num_phases - 1));
}

bool TPS53647Class::init(void){
    pinMode(this->_vcore_pgood_pin, INPUT_PULLUP);

    this->_adc_ready = AxePowerHal::init();

    // Establish communication with regulator
    uint16_t device_code = 0x00;
    this->_read_reg(PMBUS_MFR_SPECIFIC_44, (uint8_t*)&device_code, 2);
    LOG_I("TPS53647 Device ID: 0x%04X", device_code);

    if (device_code != 0x01f0) {
        LOG_E("Cannot find TPS53647 buck controller");
        return false;
    }
    LOG_I("Found TPS53647 controller !!!");


    // clear flags
    this->_write_cmd(PMBUS_CLEAR_FAULTS); 

    // restore all from nvm
    this->_write_cmd(PMBUS_RESTORE_DEFAULT_ALL);

    // set ON_OFF config, make sure the buck is switched off
    this->_write_byte(PMBUS_ON_OFF_CONFIG, 0b00010111); //bit0: VOUT; bit1: IOUT; bit2: VIN; bit3: OT; bit4: UV; bit5: OC; bit6: OTW; bit7: SCLK

    // Switch frequency, 500kHz
    this->_write_byte(PMBUS_MFR_SPECIFIC_12, 0x20); // default value

    // set maximum current
    this->_write_byte(PMBUS_MFR_SPECIFIC_10, (uint8_t) IMAX);

    // operation mode
    // VR12 Mode
    // Enable dynamic phase shedding
    // Slew Rate 0.68mV/us
    this->_write_byte(PMBUS_MFR_SPECIFIC_13, 0x89); // default value

    // set up the ON_OFF_CONFIG
    this->_write_byte(PMBUS_ON_OFF_CONFIG, 0b00010111);

    // Switch frequency, 500kHz
    this->_write_byte(PMBUS_MFR_SPECIFIC_12, 0x20);

    // set number of phases
    this->_set_phases(NUM_PHASE);

    // temperature
    this->_write_word(PMBUS_OT_WARN_LIMIT, this->_float_to_slinear11(95.0f));
    this->_write_word(PMBUS_OT_FAULT_LIMIT, this->_float_to_slinear11(125.0f));

    // Iout current
    // set warn and fault to the same value
    this->_write_word(PMBUS_IOUT_OC_WARN_LIMIT, this->_float_to_slinear11(IFAULT));
    this->_write_word(PMBUS_IOUT_OC_FAULT_LIMIT, this->_float_to_slinear11(IFAULT));

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

    this->_write_word(PMBUS_VOUT_COMMAND, reg); //VCORE Voltage Set Register   






    // uint16_t u16_value;
    // float f_value;
    // LOG_I("-----------VOLTAGE---------------------");
    // // // VOUT_MAX
    // // this->_read_reg(PMBUS_VOUT_MAX, (uint8_t*)&u16_value, 2);
    // // f_value = this->_vid_to_mv(u16_value);
    // // LOG_I("Vout Max set to: %f V", f_value);

    // // --- VOUT_COMMAND ---
    // this->_read_reg(PMBUS_VOUT_COMMAND, (uint8_t*)&u16_value, 2);
    // f_value = this->_vid_to_mv(u16_value);
    // LOG_I("Vout read back : 0x%02X (%f V)", u16_value, f_value);


    // uint8_t reg_1;
    // this->_read_reg(PMBUS_MFR_SPECIFIC_12, (uint8_t*)&reg_1, 1);
    // LOG_I("Switching Frequency Reg: 0x%02X", reg_1);




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