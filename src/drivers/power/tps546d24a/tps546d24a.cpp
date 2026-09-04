#include "tps546d24a.h"
#include "utils/logger/logger.h"
#include "drivers/temp/temp_hal.h"
#include <driver/i2c.h>

#define I2C_MASTER_NUM                  I2C_NUM_0
#define I2C_MASTER_TIMEOUT_MS           1000
#define ACK_CHECK_EN                    0x1
#define ACK_VAL                         0x0
#define NACK_VAL                        0x1

// Board-independent ADC gain constants for the external analog sense paths
// (the TPS546D24A has no READ_IIN register, so ibus must go through the MCU ADC;
// vcore is also read through the ADC for consistency with the previous design).
#define GAIN_IBUS_SAMPLE                (50.0f)
#define GAIN_VCORE_SAMPLE               (2.0f)   // TODO: re-verify divider ratio on the new sense circuit

// VOUT_MODE we program at init: REL=0 (absolute), MODE=00 (linear), N=-9 (1.953mV/LSB).
// Keep TPS546D24A_VOUT_MODE_N in sync with this byte if the exponent ever changes.
#define TPS546D24A_VOUT_MODE_BYTE       0x17
#define TPS546D24A_VOUT_MODE_N          (-9)

TPS546D24AClass::~TPS546D24AClass(){

}

uint8_t TPS546D24AClass::_read_reg(uint8_t regaddr, uint8_t *data, uint8_t length) {
    esp_err_t ret = ESP_FAIL;
    for (uint8_t attempt = 0; attempt < 2; attempt++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();

        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (this->_i2c_addr << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
        i2c_master_write_byte(cmd, regaddr, ACK_CHECK_EN);

        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (this->_i2c_addr << 1) | I2C_MASTER_READ, ACK_CHECK_EN);

        if (length > 1) {
            i2c_master_read(cmd, data, length - 1, (i2c_ack_type_t)ACK_VAL);
        }
        i2c_master_read_byte(cmd, data + length - 1, (i2c_ack_type_t)NACK_VAL);
        i2c_master_stop(cmd);

        ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
        i2c_cmd_link_delete(cmd);

        if (ret == ESP_OK) return 0;
        if (attempt == 0) delay(10); // chip busy after certain writes NACKs a read within a couple ms; 10ms matches the OPERATION-write precedent below
    }
    LOG_E("TPS546D24A read register 0x%02X failed after retry: %d", regaddr, ret);
    return 1;
}

// SMBus block-read protocol (used by IC_DEVICE_ID/MFR_ID/MFR_MODEL/etc.): unlike a plain
// multi-byte read, the slave prefixes the payload with a byte-count byte that must be
// consumed and NOT treated as payload — _read_reg() above doesn't do this and will shift
// every payload byte by one for these commands.
uint8_t TPS546D24AClass::_read_block(uint8_t regaddr, uint8_t *data, uint8_t data_length) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (this->_i2c_addr << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, regaddr, ACK_CHECK_EN);

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (this->_i2c_addr << 1) | I2C_MASTER_READ, ACK_CHECK_EN);

    uint8_t byte_count = 0;
    i2c_master_read_byte(cmd, &byte_count, (i2c_ack_type_t)ACK_VAL);
    if (data_length > 1) {
        i2c_master_read(cmd, data, data_length - 1, (i2c_ack_type_t)ACK_VAL);
    }
    i2c_master_read_byte(cmd, data + data_length - 1, (i2c_ack_type_t)NACK_VAL);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        LOG_E("TPS546D24A block-read register 0x%02X failed: %d", regaddr, ret);
        return 0xFF;
    }
    return byte_count;
}

void TPS546D24AClass::_write_byte(uint8_t regaddr, uint8_t data) {
    esp_err_t ret = ESP_FAIL;
    for (uint8_t attempt = 0; attempt < 2; attempt++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (this->_i2c_addr << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
        i2c_master_write_byte(cmd, regaddr, ACK_CHECK_EN);
        i2c_master_write_byte(cmd, data, ACK_CHECK_EN);
        i2c_master_stop(cmd);

        ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
        i2c_cmd_link_delete(cmd);

        if (ret == ESP_OK) return;
        if (attempt == 0) delay(10); // 2ms was proven insufficient: OV/UV limit writes right after VOUT_MIN/MAX failed every time even with a retry at 2ms
    }
    LOG_E("TPS546D24A write register 0x%02X failed after retry: %d", regaddr, ret);
}

void TPS546D24AClass::_write_word(uint8_t regaddr, uint16_t data){
    esp_err_t ret = ESP_FAIL;
    for (uint8_t attempt = 0; attempt < 2; attempt++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (this->_i2c_addr << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
        i2c_master_write_byte(cmd, regaddr, ACK_CHECK_EN);
        i2c_master_write_byte(cmd, (uint8_t)(data & 0x00FF), ACK_CHECK_EN);
        i2c_master_write_byte(cmd, (uint8_t)((data & 0xFF00) >> 8), ACK_CHECK_EN);
        i2c_master_stop(cmd);

        ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
        i2c_cmd_link_delete(cmd);

        if (ret == ESP_OK) return;
        if (attempt == 0) delay(10); // 2ms was proven insufficient: OV/UV limit writes right after VOUT_MIN/MAX failed every time even with a retry at 2ms
    }
    LOG_E("TPS546D24A write word register 0x%02X failed after retry: %d", regaddr, ret);
}

void TPS546D24AClass::_write_cmd(uint8_t cmd) {
    esp_err_t ret = ESP_FAIL;
    for (uint8_t attempt = 0; attempt < 2; attempt++) {
        i2c_cmd_handle_t command = i2c_cmd_link_create();

        i2c_master_start(command);
        i2c_master_write_byte(command, (this->_i2c_addr << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
        i2c_master_write_byte(command, cmd, ACK_CHECK_EN);
        i2c_master_stop(command);

        ret = i2c_master_cmd_begin(I2C_MASTER_NUM, command, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
        i2c_cmd_link_delete(command);

        if (ret == ESP_OK) return;
        if (attempt == 0) delay(10);
    }
    LOG_E("TPS546D24A write command 0x%02X failed after retry: %d", cmd, ret);
}

// Address-only probe (no data) across the whole 7-bit range; used when the configured
// i2c_addr doesn't answer, so bring-up can see where the master actually is on the bus.
void TPS546D24AClass::_scan_bus(void){
    LOG_W("[TPS546D24A] scanning I2C bus 0x08-0x77 for ACKing devices...");
    bool found_any = false;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);
        if (ret == ESP_OK) {
            LOG_W("[TPS546D24A] bus scan: device ACKed at 0x%02X", addr);
            found_any = true;
        }
    }
    if (!found_any) {
        LOG_E("[TPS546D24A] bus scan: nothing on the bus ACKed at all — check power/SDA/SCL wiring, not just the address");
    }
}

// Isolates whether ANY read (not just the 6-byte IC_DEVICE_ID block read) works at
// this->_i2c_addr, and checks the SMBus reserved Alert Response Address (0x0C) —
// if a PMBus device is holding SMBALERT# asserted, ARA read returns its 7-bit address.
void TPS546D24AClass::_check_smbus_alert(void){
    uint8_t status_byte = 0;
    uint8_t ret = this->_read_reg(PMBUS_STATUS_BYTE, &status_byte, 1);
    LOG_W("[TPS546D24A] single-byte STATUS_BYTE (0x78) read at 0x%02X: %s (value=0x%02X)",
          this->_i2c_addr, ret == 0 ? "OK" : "FAIL", status_byte);

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (0x0C << 1) | I2C_MASTER_READ, ACK_CHECK_EN);
    uint8_t ara_byte = 0;
    i2c_master_read_byte(cmd, &ara_byte, (i2c_ack_type_t)NACK_VAL);
    i2c_master_stop(cmd);
    esp_err_t ara_ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);

    if (ara_ret == ESP_OK) {
        // SMBus ARA response byte: top 7 bits = alerting device's address, LSB fixed at 1.
        LOG_W("[TPS546D24A] SMBus ARA (0x0C) responded — alerting device address = 0x%02X (raw=0x%02X)",
              ara_byte >> 1, ara_byte);
    } else {
        LOG_W("[TPS546D24A] SMBus ARA (0x0C) did not respond to a read (ret=%d)", ara_ret);
    }
}

// Standard PMBus SLINEAR11 (L11): 5-bit two's-complement exponent, 11-bit two's-complement mantissa.
uint16_t TPS546D24AClass::_float_to_slinear11(float x){
    if (x <= 0.0f) {
        LOG_W("No negative numbers at this time");
        return 0;
    }
    int32_t e = -16;
    int32_t m = 0;
    while (e <= 15) {
        float scale = powf(2.0f, (float)e);
        float temp = x / scale;
        m = (int32_t)roundf(temp);
        if (m >= 0 && m <= 1023) {
            break;
        }
        e++;
    }
    if (e > 15) {
        LOG_W("Could not find a solution");
        return 0;
    }
    uint16_t mantissa_bits = (uint16_t)m & 0x7FF;
    uint16_t exponent_bits = (uint16_t)(e & 0x1F);
    return (exponent_bits << 11) | mantissa_bits;
}

float TPS546D24AClass::_slinear11_to_float(uint16_t value){
    int32_t exponent = value >> 11;
    int32_t mantissa = value & 0x7ff;
    exponent |= (exponent & 0x10) ? 0xffffffe0 : 0;
    mantissa |= (mantissa & 0x400) ? 0xfffff800 : 0;
    return mantissa * powf(2.0, exponent);
}

// ULINEAR16: unsigned 16-bit mantissa, shared exponent from VOUT_MODE (fixed at
// TPS546D24A_VOUT_MODE_N here, since we explicitly program VOUT_MODE in hw_init()).
uint16_t TPS546D24AClass::_mv_to_ulinear16(uint16_t mv){
    float raw_f = (mv / 1000.0f) * powf(2.0f, -(float)TPS546D24A_VOUT_MODE_N);
    int32_t raw = (int32_t)roundf(raw_f);
    if (raw < 0) raw = 0;
    if (raw > 0xFFFF) raw = 0xFFFF;
    return (uint16_t)raw;
}

uint16_t TPS546D24AClass::_ulinear16_to_mv(uint16_t raw){
    float volts = raw * powf(2.0f, (float)TPS546D24A_VOUT_MODE_N);
    return (uint16_t)roundf(volts * 1000.0f);
}

void TPS546D24AClass::hw_init(void){
    if (this->_vcore_pgood_pin >= 0) {
        pinMode(this->_vcore_pgood_pin, INPUT_PULLUP);
    }

    // Identify the device. Byte[3] is left out of the check: TI's own datasheet
    // (ZHCSMI5A) disagrees with itself between table 7-90 (6Bh) and the block
    // value quoted in 7.6.72's text (6Dh) for this byte, so it isn't a reliable
    // discriminator — the other 5 bytes are consistent across both and are enough
    // to confirm we're talking to a TPS546D24A at this address.
    uint8_t dev_id[6] = {0};
    uint8_t block_count = this->_read_block(PMBUS_IC_DEVICE_ID, dev_id, 6);
    bool id_read_ok = (block_count != 0xFF);
    LOG_D("TPS546D24A IC_DEVICE_ID (block_count=%u): %02X %02X %02X %02X %02X %02X",
          block_count, dev_id[0], dev_id[1], dev_id[2], dev_id[3], dev_id[4], dev_id[5]);
    bool id_ok = id_read_ok && dev_id[0] == 0x54 && dev_id[1] == 0x49 && dev_id[2] == 0x54 &&
                 dev_id[4] == 0x24 && dev_id[5] == 0x41;
    if (!id_ok) {
        LOG_E("TPS546D24A device ID mismatch/no response at I2C 0x%02X — check ADRSEL strap / address", this->_i2c_addr);
        this->_scan_bus();
        this->_check_smbus_alert();
        LOG_E("TPS546D24A skipping the rest of hw_init(); fix the address above and reboot");
        this->_device_ok = false;
        return;
    }
    this->_device_ok = true;
    LOG_I("Found TPS546D24A controller at I2C 0x%02X", this->_i2c_addr);

    // Address the whole stack for every following command (POR default, written defensively).
    this->_write_byte(PMBUS_PHASE, TPS546D24A_PHASE_ALL);

    this->_write_cmd(PMBUS_CLEAR_FAULTS);

    // Only the CONTROL(EN/UVLO) pin governs on/off, high-active, immediate stop —
    // matches the board's GPIO-driven enable line, re-asserted defensively on every boot.
    this->_write_byte(PMBUS_ON_OFF_CONFIG, 0b00010111);

    // OPERATION's software ON bit is ANDed with the CONTROL pin state — measured EN=3.3V
    // at the chip but output stayed off until this was added, so ON_OFF_CONFIG bit3 does
    // NOT actually skip this command on this part. 0x80 = ON, no margining.
    this->_write_byte(PMBUS_OPERATION, 0x80);
    delay(10); // internal command processing busies the chip briefly; the next 2 writes were seen to NACK without this

    // Fixed absolute VOUT format (see TPS546D24A_VOUT_MODE_N docs above).
    this->_write_byte(PMBUS_VOUT_MODE, TPS546D24A_VOUT_MODE_BYTE);

    // Internal feedback divider ratio — must match the VSEL strap's electrical intent.
    this->_write_word(PMBUS_VOUT_SCALE_LOOP, this->_float_to_slinear11(this->_cfg.vout_scale_loop));

    // SYNC role must match single vs. multi-phase stack, or the master treats a missing/
    // mismatched SYNC as a protective-shutdown condition (STATUS_BYTE OFF, no latched fault).
    // 0x10/0xD0 confirmed against a shipped TPS546D24A driver for a series-stacked board.
    this->_write_byte(PMBUS_SYNC_CONFIG, (this->_cfg.num_phases > 1) ? 0xD0 : 0x10);

    // Hardware protective absolute clamps (wide). The tight, board-configured range
    // is enforced in software by set_vcore_range() / set_vcore_voltage() below.
    this->_write_word(PMBUS_VOUT_MIN, this->_mv_to_ulinear16(this->_cfg.vout_min_mv));
    this->_write_word(PMBUS_VOUT_MAX, this->_mv_to_ulinear16(this->_cfg.vout_max_mv));
    delay(10); // back-to-back word writes right after VOUT_MIN/MAX were seen to NACK/timeout at 2ms even with a retry

    // These 4 were never written before, so the chip enforced whatever OV/UV thresholds
    // were left over from POR/NVM defaults (not matched to this rail's 1.7-3.1V window) —
    // symptom seen: a real ~0.75V readout tripped a stale OV_WARN while genuine UV never
    // latched. Ratio-of-rail here (re-applied on every set_vcore_voltage() call below) —
    // a fixed offset from vout_min_mv was tried first and got silently clamped by the chip
    // to ~82-88% of VOUT_MAX (readback: 2549/2729mV against a requested 1500/1600mV),
    // so the limits must track the actual commanded rail, not the wide hw clamp window.
    this->_write_vout_limit_ratios(this->_cfg.vout_min_mv);

    // Soft-start ramp time.
    this->_write_word(PMBUS_TON_RISE, this->_float_to_slinear11(this->_cfg.ton_rise_ms));

    // Temperature — warn 10 °C below fault (matches tps53647 driver convention).
    this->_write_word(PMBUS_OT_WARN_LIMIT,  this->_float_to_slinear11(this->_cfg.tfault - 10.0f));
    this->_write_word(PMBUS_OT_FAULT_LIMIT, this->_float_to_slinear11(this->_cfg.tfault));

    // Output current — PHASE=0xFF: the chip auto-divides the written total by the
    // number of phases in the stack, so cfg values here are stack-wide totals.
    this->_write_word(PMBUS_IOUT_OC_WARN_LIMIT,  this->_float_to_slinear11(this->_cfg.iwarn_total));
    this->_write_word(PMBUS_IOUT_OC_FAULT_LIMIT, this->_float_to_slinear11(this->_cfg.ifault_total));

    // Reconfiguring VOUT_MIN/MAX/limits above while OPERATION was already ON can latch
    // transient STATUS_VOUT/IOUT bits (sticky until cleared) that don't reflect the final
    // config — clear them so debugPrint()/is_vcore_ready() start from a clean state.
    this->_write_cmd(PMBUS_CLEAR_FAULTS);

    // ── Read-back verification ────────────────────────────────────────────────
    {
        uint16_t rb_scale = 0, rb_vmin = 0, rb_vmax = 0, rb_ton_rise = 0;
        uint16_t rb_oc_warn = 0, rb_oc_fault = 0, rb_ot_warn = 0, rb_ot_fault = 0;
        uint16_t rb_ov_fault = 0, rb_ov_warn = 0, rb_uv_warn = 0, rb_uv_fault = 0;
        uint16_t rb_stack = 0;
        uint8_t  rb_sync = 0;
        this->_read_reg(PMBUS_VOUT_SCALE_LOOP,        (uint8_t*)&rb_scale,    2);
        this->_read_reg(PMBUS_VOUT_MIN,               (uint8_t*)&rb_vmin,     2);
        this->_read_reg(PMBUS_VOUT_MAX,               (uint8_t*)&rb_vmax,     2);
        this->_read_reg(PMBUS_TON_RISE,               (uint8_t*)&rb_ton_rise, 2);
        this->_read_reg(PMBUS_IOUT_OC_WARN_LIMIT,     (uint8_t*)&rb_oc_warn,  2);
        this->_read_reg(PMBUS_IOUT_OC_FAULT_LIMIT,    (uint8_t*)&rb_oc_fault, 2);
        this->_read_reg(PMBUS_OT_WARN_LIMIT,          (uint8_t*)&rb_ot_warn,  2);
        this->_read_reg(PMBUS_OT_FAULT_LIMIT,         (uint8_t*)&rb_ot_fault, 2);
        this->_read_reg(PMBUS_VOUT_OV_FAULT_LIMIT,    (uint8_t*)&rb_ov_fault, 2);
        this->_read_reg(PMBUS_VOUT_OV_WARN_LIMIT,     (uint8_t*)&rb_ov_warn,  2);
        this->_read_reg(PMBUS_VOUT_UV_WARN_LIMIT,     (uint8_t*)&rb_uv_warn,  2);
        this->_read_reg(PMBUS_VOUT_UV_FAULT_LIMIT,    (uint8_t*)&rb_uv_fault, 2);
        this->_read_reg(PMBUS_MFR_SPECIFIC_28,        (uint8_t*)&rb_stack,    2);
        this->_read_reg(PMBUS_SYNC_CONFIG,            &rb_sync,               1);
        LOG_W("[TPS546D24A] readback: SCALE_LOOP=%.3f VOUT_MIN=%dmV VOUT_MAX=%dmV TON_RISE=%.2fms",
              this->_slinear11_to_float(rb_scale), this->_ulinear16_to_mv(rb_vmin), this->_ulinear16_to_mv(rb_vmax),
              this->_slinear11_to_float(rb_ton_rise));
        LOG_W("[TPS546D24A] readback: IOUT_OC_WARN=%.1fA IOUT_OC_FAULT=%.1fA OT_WARN=%.1fC OT_FAULT=%.1fC STACK_CONFIG=0x%04X SYNC_CONFIG=0x%02X",
              this->_slinear11_to_float(rb_oc_warn), this->_slinear11_to_float(rb_oc_fault),
              this->_slinear11_to_float(rb_ot_warn), this->_slinear11_to_float(rb_ot_fault), rb_stack, rb_sync);
        LOG_W("[TPS546D24A] readback: VOUT_OV_FAULT=%dmV VOUT_OV_WARN=%dmV VOUT_UV_WARN=%dmV VOUT_UV_FAULT=%dmV",
              this->_ulinear16_to_mv(rb_ov_fault), this->_ulinear16_to_mv(rb_ov_warn),
              this->_ulinear16_to_mv(rb_uv_warn), this->_ulinear16_to_mv(rb_uv_fault));
    }
}

int8_t TPS546D24AClass::detect_phase(void){
    if (!this->_device_ok) return -1;
    uint16_t stack_cfg = 0;
    if (this->_read_reg(PMBUS_MFR_SPECIFIC_28, (uint8_t*)&stack_cfg, 2) != 0) return -1;
    uint8_t slave_count = stack_cfg & 0x0F;   // BCX_STOP field: 0=independent .. 3=3 slaves
    return (int8_t)(1 + slave_count);
}

bool TPS546D24AClass::is_vcore_ready(void){
    if (this->_vcore_pgood_pin >= 0) {
        if (digitalRead(this->_vcore_pgood_pin) == HIGH) {
            delay(1);
            return (digitalRead(this->_vcore_pgood_pin) == HIGH);
        }
        return false;
    }
    if (!this->_device_ok) return false;
    // No PGOOD GPIO wired — fall back to STATUS_WORD bit11 (0 = VOUT within regulation window).
    uint16_t status_word = 0;
    this->_read_reg(PMBUS_STATUS_WORD, (uint8_t*)&status_word, 2);
    return (status_word & 0x0800) == 0;
}

bool TPS546D24AClass::is_dc_pluged(void){
    // always return true, only dc input supported
    return true;
}

void TPS546D24AClass::set_vdd_1v8(power_state_t state){
    if(-1 == this->_asic_pwr_en_pins.pwr_vdd_1v8) return;
    digitalWrite(this->_asic_pwr_en_pins.pwr_vdd_1v8, (state == PWR_OFF) ? LOW : HIGH);
}

void TPS546D24AClass::set_pll_0v8(power_state_t state){
    if(-1 == this->_asic_pwr_en_pins.pwr_pll_0v8) return;
    digitalWrite(this->_asic_pwr_en_pins.pwr_pll_0v8, (state == PWR_OFF) ? LOW : HIGH);
}

void TPS546D24AClass::set_vcore_status(power_state_t state){
    if(-1 != this->_asic_pwr_en_pins.pwr_vcore) {
        digitalWrite(this->_asic_pwr_en_pins.pwr_vcore, (state == PWR_OFF) ? LOW : HIGH);
    }
    // Re-assert OPERATION at the exact moment CONTROL is toggled -- don't rely on the
    // hw_init()-time write still being in effect once CONTROL actually goes high later
    // (measured EN=3.3V at the chip but STATUS_BYTE stayed OFF until this was added).
    if (this->_device_ok) {
        if (state != PWR_OFF) this->_write_cmd(PMBUS_CLEAR_FAULTS); // matches vendor sequence: clear right before re-enabling
        this->_write_byte(PMBUS_OPERATION, (state == PWR_OFF) ? 0x00 : 0x80);
    }
}

void TPS546D24AClass::set_vcore_voltage(uint16_t req_mv){
    if(req_mv == 0) {
        this->set_vcore_status(PWR_OFF);
        return;
    }

    // req_mv is the user-facing per-die voltage. Clamp to the per-die range first, then
    // scale by the series count to form the actual regulator rail voltage.
    uint16_t per_die_mv = (req_mv <= this->_vcore_min_mv) ? this->_vcore_min_mv : ((req_mv >= this->_vcore_max_mv) ? this->_vcore_max_mv : req_mv);
    uint16_t rail_mv = (uint16_t)(per_die_mv * this->_vcore_series_count);
    uint16_t raw = this->_mv_to_ulinear16(rail_mv);
    this->_write_word(PMBUS_VOUT_COMMAND, raw);
    // OV/UV limits must track the commanded rail — a static window based on vout_min/max_mv
    // gets silently clamped by the chip (see hw_init() comment) instead of NACKing.
    this->_write_vout_limit_ratios(rail_mv);
    LOG_W("TPS546D24A VOUT_COMMAND -> %dmV/die x%u = %dmV rail (raw 0x%04X)",
          per_die_mv, this->_vcore_series_count, rail_mv, raw);
}

void TPS546D24AClass::_write_vout_limit_ratios(uint16_t rail_mv){
    this->_write_word(PMBUS_VOUT_OV_FAULT_LIMIT, this->_mv_to_ulinear16((uint16_t)(rail_mv * 1.25f)));
    this->_write_word(PMBUS_VOUT_OV_WARN_LIMIT,  this->_mv_to_ulinear16((uint16_t)(rail_mv * 1.10f)));
    this->_write_word(PMBUS_VOUT_UV_WARN_LIMIT,  this->_mv_to_ulinear16((uint16_t)(rail_mv * 0.90f)));
    this->_write_word(PMBUS_VOUT_UV_FAULT_LIMIT, this->_mv_to_ulinear16((uint16_t)(rail_mv * 0.75f)));
}

void TPS546D24AClass::set_vcore_range(uint16_t min_mv, uint16_t max_mv){
    this->_vcore_min_mv = min_mv;
    this->_vcore_max_mv = max_mv;
    LOG_I("Vcore range from %dmv to %dmv", this->_vcore_min_mv, this->_vcore_max_mv);
}

uint32_t TPS546D24AClass::get_vbus(void){
    if (!this->_device_ok) return 0;
    // PMBus path: READ_VIN (0x88) → SLINEAR11 → V → mV
    uint16_t raw = 0;
    this->_read_reg(PMBUS_READ_VIN, (uint8_t*)&raw, 2);
    float vin_v = this->_slinear11_to_float(raw);
    uint32_t vin_mv = (uint32_t)(vin_v * 1000.0f);
    LOG_D("Vbus PMBus 0x%04X -> %.3f V -> %u mV", raw, vin_v, vin_mv);
    return vin_mv;
}

uint32_t TPS546D24AClass::get_ibus(void){
    // ADC path (active) — measured true INPUT current via the
    // board's external shunt-sense ADC on the DC input side:
    uint32_t vadc = this->get_ibus_adc();   // mV, from the MCU ADC
    float shunt_mv = (float)vadc / GAIN_IBUS_SAMPLE;
    uint32_t current_ma = (uint32_t)(shunt_mv / this->_cfg.reg_ibus_sample);
    LOG_D("Ibus ADC raw %umV -> %.2fmV shunt -> %umA", vadc, shunt_mv, current_ma);
    return current_ma;

    // // PMBus path (commented out for rollback): the TPS546D24A has NO READ_IIN
    // // register, so there is no true input-current telemetry. READ_IOUT (0x8C)
    // // reports the stack OUTPUT current (SLINEAR11, PHASE=0xFF => total across
    // // phases) — NOT the input bus current.
    // if (!this->_device_ok) return 0;
    // uint16_t raw = 0;
    // this->_read_reg(PMBUS_READ_IOUT, (uint8_t*)&raw, 2);
    // float iout_a = this->_slinear11_to_float(raw);
    // uint32_t iout_ma = (uint32_t)(iout_a * 1000.0f);
    // LOG_D("Iout PMBus 0x%04X -> %.3f A -> %u mA", raw, iout_a, iout_ma);
    // return iout_ma;
}

uint32_t TPS546D24AClass::get_vcore(void){
    // ADC path (commented out for rollback):
    //   uint32_t vadc     = this->get_vcore_adc();
    //   uint32_t vcore_mv = (uint32_t)(vadc * GAIN_VCORE_SAMPLE);
    //   LOG_D("[TPS546D24A] ADC vcore %u mV (x%.1f gain -> %u mV)", vadc, GAIN_VCORE_SAMPLE, vcore_mv);
    //   return vcore_mv;

    // PMBus path: READ_VOUT (0x8B) → ULINEAR16 (VOUT_MODE N=-9) → rail mV → per-die mV
    if (!this->_device_ok) return 0;
    uint16_t raw = 0;
    this->_read_reg(PMBUS_READ_VOUT, (uint8_t*)&raw, 2);
    uint32_t rail_mv = (uint32_t)this->_ulinear16_to_mv(raw);
    uint32_t per_die_mv = (rail_mv + this->_vcore_series_count / 2) / this->_vcore_series_count;
    LOG_D("[TPS546D24A] Vcore PMBus 0x%04X -> %u mV rail / %u = %u mV/die",
          raw, rail_mv, this->_vcore_series_count, per_die_mv);
    return per_die_mv;
}

float TPS546D24AClass::get_temperature(void) {
    if (!this->_device_ok) return NAN;
    uint8_t data[2] = {0, 0};
    if (_read_reg(PMBUS_READ_TEMPERATURE_1, data, 2) != 0) return NAN;
    uint16_t raw = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    return _slinear11_to_float(raw);   // PHASE=0xFF -> hottest phase in the stack
}

bool TPS546D24AClass::is_oc_fault(void){
    if (!this->_device_ok) return false;
    uint8_t status_iout = 0;
    this->_read_reg(PMBUS_STATUS_IOUT, &status_iout, 1);
    return (status_iout & 0x80) != 0;  // bit7: OC_FAULT (sticky/latched)
}

bool TPS546D24AClass::is_oc_warn(void){
    if (!this->_device_ok) return false;
    uint8_t status_iout = 0;
    this->_read_reg(PMBUS_STATUS_IOUT, &status_iout, 1);
    return (status_iout & 0x20) != 0;  // bit5: OC_WARN (sticky/latched)
}

bool TPS546D24AClass::is_ot_fault(void){
    if (!this->_device_ok) return false;
    uint8_t status_temp = 0;
    this->_read_reg(PMBUS_STATUS_TEMPERATURE, &status_temp, 1);
    return (status_temp & 0x80) != 0;  // bit7: OT_FAULT (sticky/latched)
}

bool TPS546D24AClass::is_ot_warn(void){
    if (!this->_device_ok) return false;
    uint8_t status_temp = 0;
    this->_read_reg(PMBUS_STATUS_TEMPERATURE, &status_temp, 1);
    return (status_temp & 0x40) != 0;  // bit6: OT_WARN (sticky/latched)
}

void TPS546D24AClass::clear_faults(void){
    if (!this->_device_ok) return;
    this->_write_cmd(PMBUS_CLEAR_FAULTS);
}

void TPS546D24AClass::debugPrint(void){
    uint16_t raw = 0;

    this->_read_reg(PMBUS_READ_IOUT, (uint8_t*)&raw, 2);
    float iout = this->_slinear11_to_float(raw);
    this->_read_reg(PMBUS_READ_VOUT, (uint8_t*)&raw, 2);
    float vout = this->_ulinear16_to_mv(raw) / 1000.0f;
    this->_read_reg(PMBUS_VOUT_COMMAND, (uint8_t*)&raw, 2);
    float vout_cmd = this->_ulinear16_to_mv(raw) / 1000.0f;

    uint16_t status_word        = 0;
    uint8_t  status_iout        = 0;
    uint8_t  status_vout        = 0;
    uint8_t  status_input       = 0;
    uint8_t  status_temp        = 0;
    uint8_t  status_mfr         = 0;
    uint16_t raw_temp           = 0;
    uint16_t raw_iout_oc_fault_limit = 0;
    this->_read_reg(PMBUS_STATUS_WORD,            (uint8_t*)&status_word, 2);
    this->_read_reg(PMBUS_STATUS_IOUT,            &status_iout,           1);
    this->_read_reg(PMBUS_STATUS_VOUT,            &status_vout,           1);
    this->_read_reg(PMBUS_STATUS_INPUT,           &status_input,          1);
    this->_read_reg(PMBUS_STATUS_TEMPERATURE,     &status_temp,           1);
    this->_read_reg(PMBUS_STATUS_MFR_SPECIFIC,    &status_mfr,            1);
    this->_read_reg(PMBUS_READ_TEMPERATURE_1,     (uint8_t*)&raw_temp,    2);
    this->_read_reg(PMBUS_IOUT_OC_FAULT_LIMIT,    (uint8_t*)&raw_iout_oc_fault_limit, 2);
    float temp_c = this->_slinear11_to_float(raw_temp);
    float iout_limit_a = this->_slinear11_to_float(raw_iout_oc_fault_limit);

    char buf[720];
    snprintf(buf, sizeof(buf),
        "\n-----------TPS546D24A OC MONITOR-----------"
        "\n  VOUT = %.3f V (cmd %.3f V)  IOUT = %.2f A  (stack limit: %.1f A)"
        "\n  TEMP = %.1f \xc2\xb0" "C (hottest phase)"
        "\n  STATUS_VOUT  = 0x%02X  [OV_FAULT:%d  OV_WARN:%d  UV_WARN:%d  UV_FAULT:%d  MIN_MAX_CLAMP:%d  TON_MAX_FAULT:%d]"
        "\n  STATUS_IOUT  = 0x%02X  [OC_FAULT:%d  OC_WARN:%d  bit4(reserved_per_datasheet):%d]"
        "\n  STATUS_INPUT = 0x%02X  [VIN_OV_FAULT:%d  VIN_UV_FAULT:%d  IIN_OC_FAULT:%d]"
        "\n  STATUS_TEMP = 0x%02X  [OT_FAULT:%d  OT_WARN:%d]"
        "\n  STATUS_MFR_SPECIFIC = 0x%02X  [POR_FAULT:%d  SELF_FAULT:%d]"
        "\n  STATUS_WORD = 0x%04X"
        "\n  PGOOD_GPIO(pin %d) = %s"
        "\n------------------------------------------",
        vout, vout_cmd,
        iout, iout_limit_a,
        temp_c,
        status_vout, (status_vout >> 7) & 1, (status_vout >> 6) & 1, (status_vout >> 5) & 1, (status_vout >> 4) & 1, (status_vout >> 3) & 1, (status_vout >> 2) & 1,
        status_iout, (status_iout >> 7) & 1, (status_iout >> 5) & 1, (status_iout >> 4) & 1,
        status_input, (status_input >> 7) & 1, (status_input >> 4) & 1, (status_input >> 2) & 1,
        status_temp, (status_temp >> 7) & 1, (status_temp >> 6) & 1,
        status_mfr, (status_mfr >> 7) & 1, (status_mfr >> 6) & 1,
        status_word,
        this->_vcore_pgood_pin,
        (this->_vcore_pgood_pin < 0) ? "n/a (using STATUS_WORD fallback)" : (digitalRead(this->_vcore_pgood_pin) == HIGH ? "HIGH (good)" : "LOW (not good)"));
    LOG_W("%s", buf);
}

void TPS546D24AClass::dump(void) {
    uint16_t raw16 = 0;
    uint8_t  raw8  = 0;

    LOG_W("========== TPS546D24A REGISTER DUMP ==========");

    this->_read_reg(PMBUS_READ_VIN, (uint8_t*)&raw16, 2);
    LOG_W("READ_VIN         (0x88): 0x%04X (%.3f V)", raw16, this->_slinear11_to_float(raw16));

    this->_read_reg(PMBUS_READ_VOUT, (uint8_t*)&raw16, 2);
    LOG_W("READ_VOUT        (0x8B): 0x%04X (%.3f V)", raw16, this->_ulinear16_to_mv(raw16) / 1000.0f);

    this->_read_reg(PMBUS_VOUT_COMMAND, (uint8_t*)&raw16, 2);
    LOG_W("VOUT_COMMAND     (0x21): 0x%04X (%.3f V)", raw16, this->_ulinear16_to_mv(raw16) / 1000.0f);

    this->_read_reg(PMBUS_READ_IOUT, (uint8_t*)&raw16, 2);
    LOG_W("READ_IOUT        (0x8C): 0x%04X (%.3f A)", raw16, this->_slinear11_to_float(raw16));

    this->_read_reg(PMBUS_READ_TEMPERATURE_1, (uint8_t*)&raw16, 2);
    LOG_W("READ_TEMP_1      (0x8D): 0x%04X (%.1f C)", raw16, this->_slinear11_to_float(raw16));

    this->_read_reg(PMBUS_VOUT_SCALE_LOOP, (uint8_t*)&raw16, 2);
    LOG_W("VOUT_SCALE_LOOP  (0x29): 0x%04X (%.3f)", raw16, this->_slinear11_to_float(raw16));

    this->_read_reg(PMBUS_IOUT_OC_WARN_LIMIT, (uint8_t*)&raw16, 2);
    LOG_W("IOUT_OC_WARN     (0x4A): 0x%04X (%.1f A)", raw16, this->_slinear11_to_float(raw16));

    this->_read_reg(PMBUS_IOUT_OC_FAULT_LIMIT, (uint8_t*)&raw16, 2);
    LOG_W("IOUT_OC_FAULT    (0x46): 0x%04X (%.1f A)", raw16, this->_slinear11_to_float(raw16));

    this->_read_reg(PMBUS_OT_WARN_LIMIT, (uint8_t*)&raw16, 2);
    LOG_W("OT_WARN_LIMIT    (0x51): 0x%04X (%.1f C)", raw16, this->_slinear11_to_float(raw16));

    this->_read_reg(PMBUS_OT_FAULT_LIMIT, (uint8_t*)&raw16, 2);
    LOG_W("OT_FAULT_LIMIT   (0x4F): 0x%04X (%.1f C)", raw16, this->_slinear11_to_float(raw16));

    this->_read_reg(PMBUS_OPERATION, &raw8, 1);
    LOG_W("OPERATION        (0x01): 0x%02X", raw8);

    this->_read_reg(PMBUS_ON_OFF_CONFIG, &raw8, 1);
    LOG_W("ON_OFF_CONFIG    (0x02): 0x%02X", raw8);

    this->_read_reg(PMBUS_VOUT_MODE, &raw8, 1);
    LOG_W("VOUT_MODE        (0x20): 0x%02X", raw8);

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

    this->_read_reg(PMBUS_MFR_SPECIFIC_28, (uint8_t*)&raw16, 2);
    LOG_W("STACK_CONFIG     (0xEC): 0x%04X (phases = %d)", raw16, 1 + (raw16 & 0x0F));

    this->_read_reg(PMBUS_SYNC_CONFIG, &raw8, 1);
    LOG_W("SYNC_CONFIG      (0xE4): 0x%02X", raw8);

    {
        uint8_t dev_id[6] = {0};
        this->_read_reg(PMBUS_IC_DEVICE_ID, dev_id, 6);
        LOG_W("IC_DEVICE_ID     (0xAD): %02X %02X %02X %02X %02X %02X",
              dev_id[0], dev_id[1], dev_id[2], dev_id[3], dev_id[4], dev_id[5]);
    }

    LOG_W("=============================================");
}

static float _tps546d24a_temp_cb(void *ctx) {
    return static_cast<TPS546D24AClass*>(ctx)->get_temperature();
}

void tps546d24a_register_vcore_temp_hal(TPS546D24AClass *inst) {
    temp_hal_register_vcore(_tps546d24a_temp_cb, inst);
}
