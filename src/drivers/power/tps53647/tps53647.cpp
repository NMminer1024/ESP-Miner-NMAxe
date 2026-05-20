#include "tps53647.h"
#include "utils/logger/logger.h"
#include "drivers/temp/temp_hal.h"
#include <driver/i2c.h>

/** For NMQAxe++ **/
#define TPS53647_I2C_ADDRESS            (0x71)
#define I2C_MASTER_NUM                  I2C_NUM_0
#define I2C_MASTER_TIMEOUT_MS           1000
#define ACK_CHECK_EN                    0x1
#define ACK_VAL                         0x0
#define NACK_VAL                        0x1

// Board-independent ADC gain constants
#define GAIN_IBUS_SAMPLE                (50.0f)
#define GAIN_VBUS_SAMPLE                (6.1f)
#define GAIN_VCORE_SAMPLE               (2.0f)
// Board-specific parameters (num_phases, imax, ifault, reg_ibus_sample) are
// passed in via tps53647_cfg_t at construction time.

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

    // Clamp to valid VID range (0x01 to 0xFF); never return 0 for a non-zero request
    if (reg > 0xFF) {
        LOG_W("Requested voltage %dmV exceeds chip max, clamping to 0xFF", mv);
        reg = 0xFF;
    } else if (reg < 1) {
        LOG_W("Requested voltage %dmV below chip min, clamping to 0x01", mv);
        reg = 0x01;
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

void TPS53647Class::hw_init(void){
    pinMode(this->_vcore_pgood_pin, INPUT_PULLUP);

    // Establish communication with regulator
    uint16_t device_code = 0x00;
    this->_read_reg(PMBUS_MFR_SPECIFIC_44, (uint8_t*)&device_code, 2);
    LOG_D("TPS53647 Device ID: 0x%04X", device_code);

    if (device_code != 0x01f0) {
        LOG_E("Cannot find TPS53647 buck controller");
        return;
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
    this->_write_byte(PMBUS_MFR_SPECIFIC_10, this->_cfg.imax);

    // operation mode
    // VR12 Mode
    // Enable dynamic phase shedding
    // Slew Rate 0.68mV/us
    this->_write_byte(PMBUS_MFR_SPECIFIC_13, 0x89); // default value

    // set up the ON_OFF_CONFIG
    this->_write_byte(PMBUS_ON_OFF_CONFIG, 0b00010111);

    // 0000:24A
    // 0001:27A
    // 0010:30A
    // 0011:33A
    // 0100:36A
    // 0101:39A
    // 0110:42A
    // 0111:45A
    // 1000:48A
    // 1001:51A
    // 1010:54A
    // 1011:57A
    // 1100:60A
    // 1101:63A
    this->_write_byte(PMBUS_MFR_SPECIFIC_00, 0b0100 & 0x0F); 

    // set number of phases
    this->_set_phases(this->_cfg.num_phases);

    // temperature — thresholds from board config; warn is 20 °C below fault
    this->_write_word(PMBUS_OT_WARN_LIMIT,  this->_float_to_slinear11(this->_cfg.tfault - 10.0f));
    this->_write_word(PMBUS_OT_FAULT_LIMIT, this->_float_to_slinear11(this->_cfg.tfault));

    // Iout current — warn and fault thresholds from board config
    this->_write_word(PMBUS_IOUT_OC_WARN_LIMIT,  this->_float_to_slinear11(this->_cfg.ifault - 2.0f)); // set OC warn limit 5A below fault limit
    this->_write_word(PMBUS_IOUT_OC_FAULT_LIMIT, this->_float_to_slinear11(this->_cfg.ifault));
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

    // this->set_vcore_status(PWR_ON);
    uint16_t vlot_mv = (req_mv <= this->_vcore_min_mv) ? this->_vcore_min_mv : ((req_mv >= this->_vcore_max_mv) ? this->_vcore_max_mv : req_mv);

    uint8_t reg = this->_mv_to_vid(vlot_mv);

    this->_write_word(PMBUS_VOUT_COMMAND, reg); //VCORE Voltage Set Register   
}

void TPS53647Class::set_vcore_range(uint16_t min_mv, uint16_t max_mv){
    this->_vcore_min_mv = min_mv;
    this->_vcore_max_mv = max_mv;
    LOG_I("Vcore range from %dmv to %dmv",this->_vcore_min_mv, this->_vcore_max_mv = max_mv);
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
    uint32_t current = (uint32_t)(real / this->_cfg.reg_ibus_sample);
    return current;
}

uint32_t TPS53647Class::get_vcore(void){
    // ── PMBus READ_VOUT diagnostic (VID mode: low byte is VID code) ──────────
    // {
    //     uint16_t raw_vout = 0;
    //     this->_read_reg(PMBUS_READ_VOUT, (uint8_t*)&raw_vout, 2);
    //     uint8_t vid = (uint8_t)(raw_vout & 0xFF);
    //     uint32_t vcore_pmbus_mv = (vid == 0) ? 0u : (uint32_t)((vid - 1) * 5 + 250);
    //     LOG_W("[TPS53647] PMBus READ_VOUT VID=0x%02X => %u mV", vid, vcore_pmbus_mv);
    //     return vcore_pmbus_mv;
    // }

    uint32_t vadc     = this->get_vcore_adc();
    uint32_t vcore_mv = (uint32_t)(vadc * GAIN_VCORE_SAMPLE);
    LOG_D("[TPS53647] ADC vcore %u mV (x2 gain -> %u mV)", vadc, vcore_mv);
    return vcore_mv;
}

bool TPS53647Class::is_oc_fault(void){
    uint8_t status_iout = 0;
    this->_read_reg(PMBUS_STATUS_IOUT, &status_iout, 1);
    return (status_iout & 0x80) != 0;  // bit7: OC_FAULT (sticky/latched)
}

void TPS53647Class::clear_faults(void){
    this->_write_cmd(PMBUS_CLEAR_FAULTS);
}

bool TPS53647Class::is_oc_warn(void){
    uint8_t status_iout = 0;
    this->_read_reg(PMBUS_STATUS_IOUT, &status_iout, 1);
    return (status_iout & 0x20) != 0;  // bit5: OC_WARN (sticky/latched)
}

bool TPS53647Class::is_ot_fault(void){
    uint8_t status_temp = 0;
    this->_read_reg(PMBUS_STATUS_TEMPERATURE, &status_temp, 1);
    return (status_temp & 0x80) != 0;  // bit7: OT_FAULT (sticky/latched)
}

bool TPS53647Class::is_ot_warn(void){
    uint8_t status_temp = 0;
    this->_read_reg(PMBUS_STATUS_TEMPERATURE, &status_temp, 1);
    return (status_temp & 0x40) != 0;  // bit6: OT_WARN (sticky/latched)
}

void TPS53647Class::debugPrint(void){
    uint16_t raw = 0;

    this->_read_reg(PMBUS_READ_IOUT, (uint8_t*)&raw, 2);
    float iout = this->_slinear11_to_float(raw);
    this->_read_reg(PMBUS_READ_POUT, (uint8_t*)&raw, 2);
    float pout = this->_slinear11_to_float(raw);
    this->_read_reg(PMBUS_READ_PIN,  (uint8_t*)&raw, 2);
    float pin  = this->_slinear11_to_float(raw);
    float eff  = (pin > 0.1f) ? (pout / pin * 100.0f) : 0.0f;

    // OC status — STATUS_IOUT bit7=OC_FAULT(latched), bit5=OC_WARN
    // STATUS_WORD bit14=IOUT/POUT summary, bit2=TEMPERATURE summary
    uint16_t status_word        = 0;
    uint8_t  status_iout        = 0;
    uint8_t  status_temp        = 0;
    uint8_t  iout_oc_fault_resp = 0;
    uint16_t raw_temp           = 0;
    this->_read_reg(PMBUS_STATUS_WORD,            (uint8_t*)&status_word, 2);
    this->_read_reg(PMBUS_STATUS_IOUT,            &status_iout,           1);
    this->_read_reg(PMBUS_STATUS_TEMPERATURE,     &status_temp,           1);
    this->_read_reg(PMBUS_IOUT_OC_FAULT_RESPONSE, &iout_oc_fault_resp,    1);
    this->_read_reg(PMBUS_READ_TEMPERATURE_1,     (uint8_t*)&raw_temp,    2);
    float temp_c = this->_slinear11_to_float(raw_temp);

    char buf[400];
    snprintf(buf, sizeof(buf),
        "\n-----------TPS53647 OC MONITOR-----------"
        "\n  IOUT = %.2f A  (limit: %.1f A)"
        "\n  POUT = %.2f W   PIN = %.2f W   Eff = %.1f %%"
        "\n  TEMP = %.1f \xc2\xb0" "C  (warn:95\xc2\xb0" "C  fault:125\xc2\xb0" "C)"
        "\n  STATUS_WORD = 0x%04X  [IOUT_summary(b14):%d  TEMP_summary(b2):%d]"
        "\n  STATUS_IOUT = 0x%02X  [OC_FAULT:%d  OC_WARN:%d]"
        "\n  STATUS_TEMP = 0x%02X  [OT_FAULT:%d  OT_WARN:%d]"
        "\n  OC_FAULT_RESP(0x47) = 0x%02X  [action:%d retries:%d]"
        "\n    action: 0=continue 1/2=shutdown+retry 3=latch-off"
        "\n------------------------------------------",
        iout, this->_cfg.ifault,
        pout, pin, eff,
        temp_c,
        status_word, (status_word >> 14) & 1, (status_word >> 2) & 1,
        status_iout, (status_iout >> 7) & 1, (status_iout >> 5) & 1,
        status_temp, (status_temp >> 7) & 1, (status_temp >> 6) & 1,
        iout_oc_fault_resp, (iout_oc_fault_resp >> 6) & 0x3, iout_oc_fault_resp & 0x7);
    LOG_W("%s", buf);
}

// ---------------------------------------------------------------------------
// Temperature reading (PMBUS_READ_TEMPERATURE_1, SLINEAR11 format)
// ---------------------------------------------------------------------------
float TPS53647Class::get_temperature(void) {
    uint8_t data[2] = {0, 0};
    if (_read_reg(PMBUS_READ_TEMPERATURE_1, data, 2) != 0) return NAN;
    uint16_t raw = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    return _slinear11_to_float(raw);
}

// ---------------------------------------------------------------------------
// Temperature HAL registration
// ---------------------------------------------------------------------------
static float _tps53647_temp_cb(void *ctx) {
    return static_cast<TPS53647Class*>(ctx)->get_temperature();
}

void tps53647_register_vcore_temp_hal(TPS53647Class *inst) {
    temp_hal_register_vcore(_tps53647_temp_cb, inst);
}
