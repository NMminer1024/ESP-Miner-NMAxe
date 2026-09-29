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

uint16_t TPS53647Class::_vid_base_mv(void) {
    return (this->_cfg.vr_mode == TPS53647_VR12_5) ? 500 : 250;
}

uint16_t TPS53647Class::_vid_step_mv(void) {
    return (this->_cfg.vr_mode == TPS53647_VR12_5) ? 10 : 5;
}

uint8_t TPS53647Class::_vid_max_reg(void) {
    return (this->_cfg.vr_mode == TPS53647_VR12_5) ? 0xC9 : 0xFF;
}

uint8_t TPS53647Class::_mode_reg_value(void) {
    // bit7 selects VID table: 1=VR12.0, 0=VR12.5
    return (this->_cfg.vr_mode == TPS53647_VR12_0) ? 0x89 : 0x09;
}

uint8_t TPS53647Class::_mv_to_vid(uint16_t mv){
    if (mv == 0.0f) return 0x00;

    uint16_t base_mv = this->_vid_base_mv();
    uint16_t step_mv = this->_vid_step_mv();
    uint8_t max_reg = this->_vid_max_reg();
    int reg = ((int)mv - (int)base_mv) / (int)step_mv + 1;

    // Clamp to valid VID range for the selected VR mode; never return 0 for a non-zero request
    if (reg > max_reg) {
        // mv is the TPS-side output target, intentionally above the user-configured ASIC
        // voltage: power_loop adds extra mV to compensate for the PCB wire voltage drop
        // between TPS output and ASIC pins. The user's configured ASIC voltage is lower.
        uint16_t chip_max_mv = this->_vid_to_mv(max_reg);
        LOG_W("To deliver the configured ASIC voltage, TPS must output %dmV (wire-drop compensated), "
              "but the TPS chip max is %dmV — clamping to chip max", mv, chip_max_mv);
        reg = max_reg;
    } else if (reg < 1) {
        uint16_t chip_min_mv = base_mv;
        LOG_W("TPS output target %dmV (wire-drop compensated) is below chip min %dmV — clamping to chip min", mv, chip_min_mv);
        reg = 0x01;
    }
    LOG_D("Converted %dmV to VID 0x%02X", mv, reg);
    return reg;
}

uint16_t TPS53647Class::_vid_to_mv(uint8_t reg){
    if (reg == 0x00) return 0.0f;
    uint16_t mv = (uint16_t)((reg - 1) * this->_vid_step_mv() + this->_vid_base_mv());

    LOG_D("Converted VID 0x%02X to %dmV", reg, mv);
    return mv;
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

    // operation mode: keep existing load-line / slew settings, only switch VID table by config
    this->_write_byte(PMBUS_MFR_SPECIFIC_13, this->_mode_reg_value());

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
    // Board-specific current-limit code. High-current boards (e.g. BM1373) set
    // TPS53647_OCL_DEFAULT to skip this write and keep the chip's higher NVM
    // default, since the ASIC draws more than the 63 A this register can express.
    if (this->_cfg.iout_oc_level != TPS53647_OCL_DEFAULT) {
        this->_write_byte(PMBUS_MFR_SPECIFIC_00, this->_cfg.iout_oc_level & 0x0F);
        LOG_W("[TPS53647] MFR_SPECIFIC_00(0xD0) written: 0x%02X (%dA OCL)", 
              this->_cfg.iout_oc_level & 0x0F, 24 + (this->_cfg.iout_oc_level & 0x0F) * 3);
    } else {
        LOG_W("[TPS53647] MFR_SPECIFIC_00(0xD0) SKIPPED — keeping NVM default");
    }

    // set number of phases
    this->_set_phases(this->_cfg.num_phases);

    // temperature — thresholds from board config; warn is 20 °C below fault
    this->_write_word(PMBUS_OT_WARN_LIMIT,  this->_float_to_slinear11(this->_cfg.tfault - 10.0f));
    this->_write_word(PMBUS_OT_FAULT_LIMIT, this->_float_to_slinear11(this->_cfg.tfault));

    // Iout current — warn and fault thresholds from board config
    this->_write_word(PMBUS_IOUT_OC_WARN_LIMIT,  this->_float_to_slinear11(this->_cfg.ifault - 2.0f)); // set OC warn limit 5A below fault limit
    this->_write_word(PMBUS_IOUT_OC_FAULT_LIMIT, this->_float_to_slinear11(this->_cfg.ifault));

    // ── Read-back verification ────────────────────────────────────────────────
    {
        uint8_t rb_d0 = 0, rb_da = 0, rb_dc = 0, rb_dd = 0, rb_e4 = 0;
        uint8_t rb_ooc = 0, rb_vcmd = 0;
        uint16_t rb_oc_warn = 0, rb_oc_fault = 0;
        this->_read_reg(PMBUS_MFR_SPECIFIC_00, &rb_d0, 1); // 0xD0 OCL
        this->_read_reg(PMBUS_MFR_SPECIFIC_10, &rb_da, 1); // 0xDA imax
        this->_read_reg(PMBUS_MFR_SPECIFIC_12, &rb_dc, 1); // 0xDC freq
        this->_read_reg(PMBUS_MFR_SPECIFIC_13, &rb_dd, 1); // 0xDD op-mode
        this->_read_reg(PMBUS_MFR_SPECIFIC_20, &rb_e4, 1); // 0xE4 phases
        this->_read_reg(PMBUS_ON_OFF_CONFIG, &rb_ooc, 1);  // 0x02 ON_OFF_CONFIG
        this->_read_reg(PMBUS_VOUT_COMMAND,  &rb_vcmd, 1); // 0x21 VOUT_COMMAND (VID)
        this->_read_reg(PMBUS_IOUT_OC_WARN_LIMIT,  (uint8_t*)&rb_oc_warn,  2);
        this->_read_reg(PMBUS_IOUT_OC_FAULT_LIMIT, (uint8_t*)&rb_oc_fault, 2);
        LOG_W("[TPS53647] readback: D0(OCL)=0x%02X DA(imax)=0x%02X(%dA) DC(freq)=0x%02X DD(mode)=0x%02X E4(phases+1)=0x%02X",
              rb_d0, rb_da, rb_da, rb_dc, rb_dd, rb_e4);
        LOG_W("[TPS53647] readback: ON_OFF_CONFIG(0x02)=0x%02X [EN-pin:%d pol(act-high):%d op-cmd:%d]  VOUT_COMMAND(0x21)=0x%02X(%dmV)",
              rb_ooc, (rb_ooc >> 2) & 1, (rb_ooc >> 4) & 1, (rb_ooc >> 3) & 1,
              rb_vcmd, this->_vid_to_mv(rb_vcmd));
        LOG_W("[TPS53647] readback: IOUT_OC_WARN=0x%04X(%.1fA) IOUT_OC_FAULT=0x%04X(%.1fA)",
              rb_oc_warn,  this->_slinear11_to_float(rb_oc_warn),
              rb_oc_fault, this->_slinear11_to_float(rb_oc_fault));
    }
}

bool TPS53647Class::is_vcore_ready(void){
    bool pin_good = (digitalRead(this->_vcore_pgood_pin) == HIGH);
    if (pin_good) {
        delay(1);
        pin_good = (digitalRead(this->_vcore_pgood_pin) == HIGH);
    }
    if (pin_good) {
        this->_pgood_fail_log_ms = 0;   // arm the throttle so a later failure logs immediately
        return true;
    }

    uint32_t now = millis();
    if (this->_pgood_fail_log_ms != 0 && (now - this->_pgood_fail_log_ms) < 3000) {
        return false;
    }
    this->_pgood_fail_log_ms = now;

    uint16_t status_word = 0;
    uint8_t  status_vout = 0, status_iout = 0, status_input = 0;
    uint8_t  status_temp = 0, status_cml = 0;
    uint8_t  operation = 0, on_off_config = 0, vout_command = 0;
    this->_read_reg(PMBUS_STATUS_WORD,        (uint8_t*)&status_word, 2);
    this->_read_reg(PMBUS_STATUS_VOUT,        &status_vout,   1);
    this->_read_reg(PMBUS_STATUS_IOUT,        &status_iout,   1);
    this->_read_reg(PMBUS_STATUS_INPUT,       &status_input,  1);
    this->_read_reg(PMBUS_STATUS_TEMPERATURE, &status_temp,   1);
    this->_read_reg(PMBUS_STATUS_CML,         &status_cml,    1);
    this->_read_reg(PMBUS_OPERATION,          &operation,     1);
    this->_read_reg(PMBUS_ON_OFF_CONFIG,      &on_off_config, 1);
    this->_read_reg(PMBUS_VOUT_COMMAND,       &vout_command,  1);

    // STATUS_WORD bit11 is POWER_GOOD# (1 = chip says power is NOT good)
    const bool chip_pg   = ((status_word >> 11) & 1) == 0;
    const bool en_pin_hi = (this->_asic_pwr_en_pins.pwr_vcore < 0) ||
                           (digitalRead(this->_asic_pwr_en_pins.pwr_vcore) == HIGH);

    const char* cause;
    if (chip_pg) {
        cause = "CHIP REPORTS POWER GOOD but PGOOD GPIO reads LOW -> PGOOD net/pull-up/GPIO problem, not the regulator";
    } else if ((status_cml & 0xC0) != 0) {
        cause = "PMBus command/data rejected (CML) -> a config write was invalid, regulator refused to start";
    } else if (((status_input >> 4) & 1) || ((status_word >> 3) & 1)) {
        cause = "VIN undervoltage -> input rail sagging or latched off, needs a full VIN power cycle";
    } else if ((status_vout & 0x80) || ((status_word >> 5) & 1)) {
        cause = "VOUT overvoltage fault latched -> output shorted high or feedback/sense problem";
    } else if ((status_vout >> 4) & 1) {
        cause = "VOUT undervoltage fault latched -> output pulled down, soft-start failed (short on Vcore?)";
    } else if ((status_iout & 0x80) || ((status_word >> 4) & 1)) {
        cause = "IOUT overcurrent fault latched -> inrush or short on Vcore";
    } else if (status_temp & 0xC0) {
        cause = "over-temperature fault latched -> check cooling";
    } else if (vout_command == 0x00) {
        cause = "VOUT_COMMAND is 0 (VID=0) -> no output target was programmed, check NVS asic_voltage";
    } else if (((on_off_config >> 3) & 1) && (((operation >> 7) & 1) == 0)) {
        cause = "ON_OFF_CONFIG requires the OPERATION command but OPERATION=OFF -> EN pin alone will never start it";
    } else if (!en_pin_hi) {
        cause = "Vcore EN GPIO is driven LOW -> firmware has not enabled the rail";
    } else if (((on_off_config >> 2) & 1) == 0) {
        cause = "ON_OFF_CONFIG ignores the EN pin -> config write did not take effect after RESTORE_DEFAULT_ALL";
    } else if ((status_word >> 6) & 1) {
        cause = "chip reports OFF with no fault flagged -> EN net may not actually reach the chip pin";
    } else {
        cause = "enabled with no fault flagged, output still not in regulation -> soft-start stalled";
    }

    LOG_W("[TPS53647] Vcore NOT ready: %s", cause);
    LOG_W("[TPS53647]   EN gpio=%s  chip_PG=%d  VOUT_COMMAND=0x%02X(%dmV)  OPERATION=0x%02X  ON_OFF_CONFIG=0x%02X[en-pin:%d op-cmd:%d]",
          en_pin_hi ? "HIGH" : "LOW", chip_pg ? 1 : 0,
          vout_command, this->_vid_to_mv(vout_command), operation,
          on_off_config, (on_off_config >> 2) & 1, (on_off_config >> 3) & 1);
    LOG_W("[TPS53647]   WORD=0x%04X VOUT=0x%02X IOUT=0x%02X IN=0x%02X TEMP=0x%02X CML=0x%02X",
          status_word, status_vout, status_iout, status_input, status_temp, status_cml);
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
        LOG_W("[TPS53647] req_vcore=0 -> VOUT_COMMAND NOT written, Vcore forced OFF. "
              "PGOOD stays LOW until a valid voltage is set (check NVS asic_voltage).");
        this->set_vcore_status(PWR_OFF);
        return;
    }

    // this->set_vcore_status(PWR_ON);
    uint16_t vlot_mv = (req_mv <= this->_vcore_min_mv) ? this->_vcore_min_mv : ((req_mv >= this->_vcore_max_mv) ? this->_vcore_max_mv : req_mv);

    if (vlot_mv != req_mv && req_mv != this->_last_clamp_warned_mv) {
        this->_last_clamp_warned_mv = req_mv;
        LOG_W("[TPS53647] req_vcore=%dmV clamped to %dmV (range %d~%d mV)",
              req_mv, vlot_mv, this->_vcore_min_mv, this->_vcore_max_mv);
    }

    uint8_t reg = this->_mv_to_vid(vlot_mv);
    if (reg != this->_last_vid_written) {
        this->_last_vid_written = reg;
        LOG_I("[TPS53647] VOUT_COMMAND <- VID 0x%02X (%dmV)", reg, vlot_mv);
    }

    this->_write_word(PMBUS_VOUT_COMMAND, reg); //VCORE Voltage Set Register   
}

void TPS53647Class::set_vcore_range(uint16_t min_mv, uint16_t max_mv){
    this->_vcore_min_mv = min_mv;
    this->_vcore_max_mv = max_mv;
    LOG_I("Vcore range from %dmv to %dmv",this->_vcore_min_mv, this->_vcore_max_mv = max_mv);
}

uint32_t TPS53647Class::get_vbus(void){
    // ADC path (commented out):
    // uint32_t vadc = this->get_vbus_adc();
    // LOG_D("Vbus %dmv", (uint32_t)(vadc));
    // return (uint32_t)(vadc * GAIN_VBUS_SAMPLE);

    // PMBus path: READ_VIN (0x88) → SLINEAR11 → V → mV
    uint16_t raw = 0;
    this->_read_reg(PMBUS_READ_VIN, (uint8_t*)&raw, 2);
    float vin_v = this->_slinear11_to_float(raw);
    uint32_t vin_mv = (uint32_t)(vin_v * 1000.0f);
    LOG_D("Vbus PMBus 0x%04X -> %.3f V -> %u mV", raw, vin_v, vin_mv);
    return vin_mv;
}

uint32_t TPS53647Class::get_ibus(void){
    // ADC path (commented out):
    // uint32_t vadc = this->get_ibus_adc();
    // LOG_D("ibus %dmv", vadc);
    // float real = (float)vadc / GAIN_IBUS_SAMPLE;
    // uint32_t current = (uint32_t)(real / this->_cfg.reg_ibus_sample);
    // return current;

    // PMBus path: READ_IIN (0x89) → SLINEAR11 → A → mA
    uint16_t raw = 0;
    this->_read_reg(PMBUS_READ_IIN, (uint8_t*)&raw, 2);
    float iin_a = this->_slinear11_to_float(raw);
    uint32_t iin_ma = (uint32_t)(iin_a * 1000.0f);
    LOG_D("Ibus PMBus 0x%04X -> %.3f A -> %u mA", raw, iin_a, iin_ma);
    return iin_ma;
}

uint32_t TPS53647Class::get_vcore(void){
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

float TPS53647Class::get_iout_amps(void){
    uint16_t raw = 0;
    this->_read_reg(PMBUS_READ_IOUT, (uint8_t*)&raw, 2);
    return this->_slinear11_to_float(raw);
}

float TPS53647Class::get_oc_limit_amps(void){
    return this->_cfg.ifault;
}

float TPS53647Class::get_ot_limit_celsius(void){
    return this->_cfg.tfault;
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

    this->_read_reg(PMBUS_READ_VOUT, (uint8_t*)&raw, 2);
    uint8_t vid = (uint8_t)(raw & 0xFF);
    float vout = this->_vid_to_mv(vid) / 1000.0f;
    this->_read_reg(PMBUS_READ_IOUT, (uint8_t*)&raw, 2);
    float iout = this->_slinear11_to_float(raw);
    this->_read_reg(PMBUS_READ_PIN,  (uint8_t*)&raw, 2);
    float pin  = this->_slinear11_to_float(raw);
    this->_read_reg(PMBUS_READ_TEMPERATURE_1, (uint8_t*)&raw, 2);
    float temp_c = this->_slinear11_to_float(raw);

    uint16_t status_word = 0;
    uint8_t  status_vout = 0, status_iout = 0, status_input = 0;
    uint8_t  status_temp = 0, status_cml = 0;
    uint8_t  operation = 0, on_off_config = 0, vout_command = 0;
    this->_read_reg(PMBUS_STATUS_WORD,        (uint8_t*)&status_word, 2);
    this->_read_reg(PMBUS_STATUS_VOUT,        &status_vout,    1);
    this->_read_reg(PMBUS_STATUS_IOUT,        &status_iout,    1);
    this->_read_reg(PMBUS_STATUS_INPUT,       &status_input,   1);
    this->_read_reg(PMBUS_STATUS_TEMPERATURE, &status_temp,    1);
    this->_read_reg(PMBUS_STATUS_CML,         &status_cml,     1);
    this->_read_reg(PMBUS_OPERATION,          &operation,      1);
    this->_read_reg(PMBUS_ON_OFF_CONFIG,      &on_off_config,  1);
    this->_read_reg(PMBUS_VOUT_COMMAND,       &vout_command,   1);

    LOG_W("-----------TPS53647 vcore bring-up state-----------");
    LOG_W("  VOUT=%.3fV  IOUT=%.2fA  PIN=%.2fW  TEMP=%.1f\xc2\xb0" "C", vout, iout, pin, temp_c);
    LOG_W("  target: VOUT_COMMAND=0x%02X(%dmV)  OPERATION=0x%02X[ON:%d]  ON_OFF_CONFIG=0x%02X[EN-pin:%d pol:%d op-cmd:%d]",
          vout_command, this->_vid_to_mv(vout_command),
          operation, (operation >> 7) & 1,
          on_off_config, (on_off_config >> 2) & 1, (on_off_config >> 4) & 1, (on_off_config >> 3) & 1);
    LOG_W("  STATUS_WORD=0x%04X [PG#:%d OFF:%d VOUT_OV:%d IOUT_OC:%d VIN_UV:%d TEMP:%d CML:%d]",
          status_word,
          (status_word >> 11) & 1, (status_word >> 6) & 1, (status_word >> 5) & 1,
          (status_word >> 4) & 1, (status_word >> 3) & 1, (status_word >> 2) & 1,
          (status_word >> 1) & 1);
    LOG_W("  VOUT_ST=0x%02X[OV_F:%d UV_F:%d]  IN_ST=0x%02X[VIN_UV_F:%d]  CML_ST=0x%02X[bad_cmd:%d bad_data:%d]",
          status_vout, (status_vout >> 7) & 1, (status_vout >> 4) & 1,
          status_input, (status_input >> 4) & 1,
          status_cml, (status_cml >> 7) & 1, (status_cml >> 6) & 1);
    LOG_W("  IOUT_ST=0x%02X[OC_F:%d OC_W:%d]  TEMP_ST=0x%02X[OT_F:%d OT_W:%d]",
          status_iout, (status_iout >> 7) & 1, (status_iout >> 5) & 1,
          status_temp, (status_temp >> 7) & 1, (status_temp >> 6) & 1);
    LOG_W("--------------------------------------------------");
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

// ---------------------------------------------------------------------------
// Full register dump
// ---------------------------------------------------------------------------
void TPS53647Class::dump(void) {
    uint16_t raw16 = 0;
    uint8_t  raw8  = 0;

    LOG_W("========== TPS53647 REGISTER DUMP ==========");

    // ── Read-only telemetry (SLINEAR11 or VID) ──────────────────────────
    this->_read_reg(PMBUS_READ_VIN, (uint8_t*)&raw16, 2);
    LOG_W("READ_VIN         (0x88): 0x%04X (%.3f V)", raw16, this->_slinear11_to_float(raw16));

    this->_read_reg(PMBUS_READ_IIN, (uint8_t*)&raw16, 2);
    LOG_W("READ_IIN         (0x89): 0x%04X (%.3f A)", raw16, this->_slinear11_to_float(raw16));

    this->_read_reg(PMBUS_READ_VOUT, (uint8_t*)&raw16, 2);
    {
        uint8_t vid = (uint8_t)(raw16 & 0xFF);
        uint16_t mv = this->_vid_to_mv(vid);
        LOG_W("READ_VOUT        (0x8B): 0x%04X (%.3f V)", raw16, mv / 1000.0f);
    }

    this->_read_reg(PMBUS_READ_IOUT, (uint8_t*)&raw16, 2);
    LOG_W("READ_IOUT        (0x8C): 0x%04X (%.3f A)", raw16, this->_slinear11_to_float(raw16));

    this->_read_reg(PMBUS_READ_TEMPERATURE_1, (uint8_t*)&raw16, 2);
    LOG_W("READ_TEMP_1      (0x8D): 0x%04X (%.1f C)", raw16, this->_slinear11_to_float(raw16));

    this->_read_reg(PMBUS_READ_POUT, (uint8_t*)&raw16, 2);
    LOG_W("READ_POUT        (0x96): 0x%04X (%.3f W)", raw16, this->_slinear11_to_float(raw16));

    this->_read_reg(PMBUS_READ_PIN, (uint8_t*)&raw16, 2);
    LOG_W("READ_PIN         (0x97): 0x%04X (%.3f W)", raw16, this->_slinear11_to_float(raw16));

    // ── Configuration / status (1-byte reads) ───────────────────────────
    this->_read_reg(PMBUS_OPERATION, &raw8, 1);
    LOG_W("OPERATION        (0x01): 0x%02X", raw8);

    this->_read_reg(PMBUS_ON_OFF_CONFIG, &raw8, 1);
    LOG_W("ON_OFF_CONFIG    (0x02): 0x%02X", raw8);

    this->_read_reg(PMBUS_CAPABILITY, &raw8, 1);
    LOG_W("CAPABILITY       (0x19): 0x%02X", raw8);

    this->_read_reg(PMBUS_VOUT_MODE, &raw8, 1);
    LOG_W("VOUT_MODE        (0x20): 0x%02X", raw8);

    this->_read_reg(PMBUS_VOUT_COMMAND, (uint8_t*)&raw16, 2);
    {
        uint8_t vid = (uint8_t)(raw16 & 0xFF);
        uint16_t mv = this->_vid_to_mv(vid);
        LOG_W("VOUT_COMMAND     (0x21): 0x%04X (%.3f V)", raw16, mv / 1000.0f);
    }

    // ── Status registers ────────────────────────────────────────────────
    this->_read_reg(PMBUS_STATUS_BYTE, &raw8, 1);
    LOG_W("STATUS_BYTE      (0x78): 0x%02X", raw8);

    this->_read_reg(PMBUS_STATUS_WORD, (uint8_t*)&raw16, 2);
    LOG_W("STATUS_WORD      (0x79): 0x%04X", raw16);

    this->_read_reg(PMBUS_STATUS_VOUT, &raw8, 1);
    LOG_W("STATUS_VOUT      (0x7A): 0x%02X", raw8);

    this->_read_reg(PMBUS_STATUS_IOUT, &raw8, 1);
    LOG_W("STATUS_IOUT      (0x7B): 0x%02X", raw8);

    this->_read_reg(PMBUS_STATUS_INPUT, &raw8, 1);
    LOG_W("STATUS_INPUT     (0x7C): 0x%02X", raw8);

    this->_read_reg(PMBUS_STATUS_TEMPERATURE, &raw8, 1);
    LOG_W("STATUS_TEMP      (0x7D): 0x%02X", raw8);

    this->_read_reg(PMBUS_STATUS_CML, &raw8, 1);
    LOG_W("STATUS_CML       (0x7E): 0x%02X", raw8);

    this->_read_reg(PMBUS_STATUS_MFR_SPECIFIC, &raw8, 1);
    LOG_W("STATUS_MFR_SPECIFIC (80h): 0x%02X", raw8);

    // ── MFR_SPECIFIC registers ──────────────────────────────────────────
    this->_read_reg(PMBUS_MFR_SPECIFIC_00, &raw8, 1);
    LOG_W("MFR_SPEC_00      (0xD0): 0x%02X", raw8);

    this->_read_reg(PMBUS_MFR_SPECIFIC_01, &raw8, 1);
    LOG_W("MFR_SPEC_01      (0xD1): 0x%02X", raw8);

    this->_read_reg(PMBUS_MFR_SPECIFIC_04, &raw8, 1);
    LOG_W("MFR_SPEC_04      (0xD4): 0x%02X", raw8);

    this->_read_reg(PMBUS_MFR_SPECIFIC_05, &raw8, 1);
    LOG_W("MFR_SPEC_05      (0xD5): 0x%02X", raw8);

    this->_read_reg(PMBUS_MFR_SPECIFIC_07, &raw8, 1);
    LOG_W("MFR_SPEC_07      (0xD7): 0x%02X", raw8);

    this->_read_reg(PMBUS_MFR_SPECIFIC_08, &raw8, 1);
    LOG_W("MFR_SPEC_08      (0xD8): 0x%02X", raw8);

    this->_read_reg(PMBUS_MFR_SPECIFIC_09, &raw8, 1);
    LOG_W("MFR_SPEC_09      (0xD9): 0x%02X", raw8);

    this->_read_reg(PMBUS_MFR_SPECIFIC_10, &raw8, 1);
    LOG_W("MFR_SPEC_10      (0xDA): 0x%02X (imax)", raw8);

    this->_read_reg(PMBUS_MFR_SPECIFIC_11, &raw8, 1);
    LOG_W("MFR_SPEC_11      (0xDB): 0x%02X", raw8);

    this->_read_reg(PMBUS_MFR_SPECIFIC_12, &raw8, 1);
    LOG_W("MFR_SPEC_12      (0xDC): 0x%02X (fsw)", raw8);

    this->_read_reg(PMBUS_MFR_SPECIFIC_13, &raw8, 1);
    LOG_W("MFR_SPEC_13      (0xDD): 0x%02X (op mode)", raw8);

    this->_read_reg(PMBUS_MFR_SPECIFIC_14, &raw8, 1);
    LOG_W("MFR_SPEC_14      (0xDE): 0x%02X", raw8);

    this->_read_reg(PMBUS_MFR_SPECIFIC_15, &raw8, 1);
    LOG_W("MFR_SPEC_15      (0xDF): 0x%02X", raw8);

    this->_read_reg(PMBUS_MFR_SPECIFIC_16, &raw8, 1);
    LOG_W("MFR_SPEC_16      (0xE0): 0x%02X", raw8);

    this->_read_reg(PMBUS_MFR_SPECIFIC_20, &raw8, 1);
    LOG_W("MFR_SPEC_20      (0xE4): 0x%02X (phases)", raw8);

    this->_read_reg(PMBUS_MFR_SPECIFIC_44, (uint8_t*)&raw16, 2);
    LOG_W("MFR_SPEC_44      (0xFC): 0x%04X (device id)", raw16);

    LOG_W("=============================================");
}

void tps53647_register_vcore_temp_hal(TPS53647Class *inst) {
    temp_hal_register_vcore(_tps53647_temp_cb, inst);
}
