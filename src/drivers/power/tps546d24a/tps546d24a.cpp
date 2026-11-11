#include "tps546d24a.h"
#include "utils/logger/logger.h"
#include "drivers/temp/temp_hal.h"
#include <driver/i2c.h>
#include <string.h>

#define I2C_MASTER_NUM                  I2C_NUM_0
#define I2C_MASTER_TIMEOUT_MS           1000
#define ACK_CHECK_EN                    0x1
#define ACK_VAL                         0x0
#define NACK_VAL                        0x1

// Board-independent ADC gain constants for the external analog sense paths
// (the TPS546D24A has no READ_IIN register, so ibus must go through the MCU ADC;
// vcore is also read through the ADC for consistency with the previous design).
#define GAIN_IBUS_SAMPLE                (50.0f)
#define GAIN_VBUS_SAMPLE                (6.1f)   // input divider ratio (same as TPS53355/TPS53647 boards)
#define GAIN_VCORE_SAMPLE               (2.0f)   // TODO: re-verify divider ratio on the new sense circuit

// VOUT_MODE we program at init: REL=0 (absolute), MODE=00 (linear), N=-9 (1.953mV/LSB).
// Keep TPS546D24A_VOUT_MODE_N in sync with this byte if the exponent ever changes.
#define TPS546D24A_VOUT_MODE_BYTE       0x17
#define TPS546D24A_VOUT_MODE_N          (-9)

// RAII guard for _bus_mtx (recursive — public methods may call other public
// methods, e.g. set_vcore_voltage(0) -> set_vcore_status()). Take failures are
// NOT retried: the caller gets a safe default and the 50ms power loop / next
// API call retries. Warn is throttled so a contended bus cannot flood the log.
class TpsBusGuard {
public:
    TpsBusGuard(SemaphoreHandle_t m, uint32_t timeout_ms)
        : _m(m), _ok(m != nullptr && xSemaphoreTakeRecursive(m, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        if (!_ok) {
            static uint32_t last_warn = 0;
            uint32_t now = millis();
            if (now - last_warn >= 1000) {
                LOG_W("TPS546D24A bus lock busy (%ums), skipping this access", timeout_ms);
                last_warn = now;
            }
        }
    }
    ~TpsBusGuard() { if (_ok) xSemaphoreGiveRecursive(_m); }
    bool ok() const { return _ok; }
private:
    SemaphoreHandle_t _m;
    bool _ok;
};
// Default 200ms: covers the longest normal holder (set_vcore_voltage ~5 txns,
// debugPrint ~14 txns) with margin; power loop runs at 50ms so skipping one
// round on timeout is harmless. NEVER use portMAX_DELAY here.
#define TPS_BUS_LOCK(timeout_ms)  TpsBusGuard _bus_guard(this->_bus_mtx, (timeout_ms))
#define TPS_BUS_LOCK_DEFAULT()    TPS_BUS_LOCK(200)

TPS546D24AClass::~TPS546D24AClass(){
    if (this->_bus_mtx) {
        vSemaphoreDelete(this->_bus_mtx);
        this->_bus_mtx = nullptr;
    }
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
    esp_err_t ret = ESP_FAIL;
    uint8_t byte_count = 0;
    for (uint8_t attempt = 0; attempt < 2; attempt++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();

        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (this->_i2c_addr << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
        i2c_master_write_byte(cmd, regaddr, ACK_CHECK_EN);

        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (this->_i2c_addr << 1) | I2C_MASTER_READ, ACK_CHECK_EN);

        i2c_master_read_byte(cmd, &byte_count, (i2c_ack_type_t)ACK_VAL);
        if (data_length > 1) {
            i2c_master_read(cmd, data, data_length - 1, (i2c_ack_type_t)ACK_VAL);
        }
        i2c_master_read_byte(cmd, data + data_length - 1, (i2c_ack_type_t)NACK_VAL);
        i2c_master_stop(cmd);

        ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
        i2c_cmd_link_delete(cmd);

        if (ret == ESP_OK) return byte_count;
        if (attempt == 0) delay(10); // same NACK-right-after-write recovery window as _read_reg()
    }
    LOG_E("TPS546D24A block-read register 0x%02X failed after retry: %d", regaddr, ret);
    return 0xFF;
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
    // 3 attempts with escalating backoff. A chip-busy NACK clears within tens of ms, so the
    // 100ms third attempt distinguishes timing (would succeed) from a semantic reject
    // (INVDATA — still fails). Measured: delay(10)/delay(50) after VOUT_COMMAND did NOT
    // clear the OV/UV limit-write rejections, and STATUS_CML=0x40 (INVDATA) pointed at the
    // value, not the bus.
    const uint16_t backoff_ms[3] = {0, 10, 100};
    for (uint8_t attempt = 0; attempt < 3; attempt++) {
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
        if (attempt < 2) delay(backoff_ms[attempt + 1]);
    }
    // Forensics for the OV/UV limit writes: quote the chip's CURRENT VOUT_COMMAND and the
    // value it actually holds at regaddr, plus STATUS_CML. INVDATA(0x40)+a VOUT_COMMAND that
    // differs from what we just wrote = the chip re-based the percentage on a not-yet-settled
    // command; INVDATA with matching command = our ratio is genuinely out of the enforced range.
    uint8_t cml = 0;
    this->_read_reg(PMBUS_STATUS_CML, &cml, 1);
    uint16_t rb_cmd = 0, rb_val = 0;
    this->_read_reg(PMBUS_VOUT_COMMAND, (uint8_t*)&rb_cmd, 2);
    this->_read_reg(regaddr,            (uint8_t*)&rb_val, 2);
    // Silenced to LOG_D on purpose (2026-11-05): the OV/UV limit writes fail with
    // CML=0x40 (INVDATA) on Nexus whenever VOUT_COMMAND moves — the chip rejects the
    // ratio-encoded values, but regulation itself is fine (VOUT_COMMAND lands, mining
    // normal, OC/OT limits are separate registers and unaffected). Kept at debug level
    // so the forensics (CML / cmd / held) stay available when LOG_D is enabled.
    LOG_D("TPS546D24A write word 0x%02X=0x%04X failed: %d (CML=0x%02X cmd=%dmV held=%dmV)",
          regaddr, data, ret, cml, this->_ulinear16_to_mv(rb_cmd), this->_ulinear16_to_mv(rb_val));
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
    // Boot runs before the periodic readers exist, so this take is effectively
    // uncontended; 1000ms covers the ID-retry delays inside in case a reader
    // thread was already up (re-init path).
    TPS_BUS_LOCK(1000);
    if (!_bus_guard.ok()) {
        LOG_E("TPS546D24A hw_init: bus lock unavailable, aborting init");
        this->_device_ok = false;
        return;
    }
    if (this->_vcore_pgood_pin >= 0) {
        pinMode(this->_vcore_pgood_pin, INPUT_PULLUP);
    }

    // Identify the device. Byte[3] is left out of the check: TI's own datasheet
    // (ZHCSMI5A) disagrees with itself between table 7-90 (6Bh) and the block
    // value quoted in 7.6.72's text (6Dh) for this byte, so it isn't a reliable
    // discriminator — the other 5 bytes are consistent across both and are enough
    // to confirm we're talking to a TPS546D24A at this address.
    //
    // Retry the ID read: the PMBus interface can still be in POR/MTP-load when the
    // ESP32 gets here (~hundreds of ms after power-on) and transiently NACKs. A miss
    // here would latch _device_ok=false and wedge the boot chain on the temp self-test.
    uint8_t dev_id[6] = {0};
    bool id_ok = false;
    for (uint8_t attempt = 0; attempt < 5 && !id_ok; attempt++) {
        if (attempt) {
            LOG_W("TPS546D24A ID read attempt %u failed, retrying...", attempt);
            delay(100);
        }
        memset(dev_id, 0, sizeof(dev_id));
        uint8_t block_count = this->_read_block(PMBUS_IC_DEVICE_ID, dev_id, 6);
        bool id_read_ok = (block_count != 0xFF);
        LOG_D("TPS546D24A IC_DEVICE_ID (block_count=%u): %02X %02X %02X %02X %02X %02X",
              block_count, dev_id[0], dev_id[1], dev_id[2], dev_id[3], dev_id[4], dev_id[5]);
        id_ok = id_read_ok && dev_id[0] == 0x54 && dev_id[1] == 0x49 && dev_id[2] == 0x54 &&
                dev_id[4] == 0x24 && dev_id[5] == 0x41;
    }
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

    // ON_OFF_CONFIG = 0x18 (PU|CMD): CMD=1 makes the part honor the OPERATION (0x01)
    // ON/OFF command, ANDed with the CONTROL(EN) pin (active-high). The previous 0x17
    // was a straight copy of the TPS53647 value whose bit map differs — its CMD bit
    // (bit3) was 0, so OPERATION=0x00 was silently ignored and the ASIC could never
    // actually be powered down on an overcurrent fault.
    this->_write_byte(PMBUS_ON_OFF_CONFIG, 0x18);

    // OPERATION (0x80 = ON, no margining) is ANDed with the CONTROL pin state. With
    // CMD=1 in ON_OFF_CONFIG above, this command actually gates the output.
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

    // OV/UV limit registers (0x40/0x42/0x43/0x44) are intentionally NOT written here:
    // at this point VOUT_COMMAND is still the POR default (~2.5 V), so deriving limits
    // from vout_min_mv produced UV limits below VOUT_MIN and OV limits below VOUT_COMMAND,
    // which the TPS546D24A rejects with a NACK (the "write word register 0x40/0x42/0x43/
    // 0x44 failed" boot log). They are written correctly, relative to the real commanded
    // rail, in set_vcore_voltage() — which runs before the EN pin is asserted, so the
    // output never comes up with stale POR thresholds.

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
    TPS_BUS_LOCK_DEFAULT();
    if (!_bus_guard.ok()) return -1;
    if (!this->_device_ok) return -1;
    uint16_t stack_cfg = 0;
    if (this->_read_reg(PMBUS_MFR_SPECIFIC_28, (uint8_t*)&stack_cfg, 2) != 0) return -1;
    uint8_t slave_count = stack_cfg & 0x0F;   // BCX_STOP field: 0=independent .. 3=3 slaves
    return (int8_t)(1 + slave_count);
}

bool TPS546D24AClass::is_vcore_ready(void){
    TPS_BUS_LOCK_DEFAULT();
    if (!_bus_guard.ok()) return false;   // "not ready yet" — boot loop just retries
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
    TPS_BUS_LOCK_DEFAULT();
    if (!_bus_guard.ok()) return;        // caller (protection loop / API) retries
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
    if (req_mv == 0) {
        // Powering the rail down invalidates both caches: the next enable reprograms
        // from scratch instead of trusting a stale rail value.
        TPS_BUS_LOCK_DEFAULT();
        if (!_bus_guard.ok()) return;
        this->_limit_rail_mv = 0;
        this->_vout_rail_mv  = 0;
        this->set_vcore_status(PWR_OFF);
        return;
    }

    // req_mv is the user-facing per-die voltage. Clamp to the per-die range first, then
    // scale by the series count to form the actual regulator rail voltage.
    uint16_t per_die_mv = (req_mv <= this->_vcore_min_mv) ? this->_vcore_min_mv : ((req_mv >= this->_vcore_max_mv) ? this->_vcore_max_mv : req_mv);
    uint16_t rail_mv = (uint16_t)(per_die_mv * this->_vcore_series_count);

    // ── Fast path, checked BEFORE taking the bus ──────────────────────────────
    // The correction loop calls this every ~100 ms and its setpoint frequently lands
    // on the same mV it already commanded. Answering that without acquiring the lock
    // is the whole point: the lock is what the temperature thread waits 200 ms for,
    // and taking it just to discover there is nothing to do is pure loss.
    //
    // Reading _vout_rail_mv outside the lock is safe: it is a single uint16_t (an
    // atomic access on Xtensa) that only this function writes, while holding the
    // lock. The worst case is two tasks both seeing the old value and both doing the
    // work — which is exactly the pre-existing behaviour, not a regression.
    if (rail_mv == this->_vout_rail_mv) return;

    // Recursive take: the req_mv==0 path re-enters via set_vcore_status().
    TPS_BUS_LOCK_DEFAULT();
    if (!_bus_guard.ok()) return;
    // Re-check under the lock: another task may have applied this exact value while
    // we were waiting for the semaphore.
    if (rail_mv == this->_vout_rail_mv) return;

    uint16_t raw = this->_mv_to_ulinear16(rail_mv);
    this->_write_word(PMBUS_VOUT_COMMAND, raw);
    this->_vout_rail_mv = rail_mv;

    // OV/UV limits must track the commanded rail — a static window based on
    // vout_min/max_mv gets silently clamped by the chip (see hw_init() comment)
    // instead of NACKing.
    //
    // Re-written only when the rail moved by more than LIMIT_REPROG_MV.
    //
    // The limits are ratios of the rail (x1.25 / x1.10 / x0.90 / x0.75), so a 25-50 mV
    // step moves a 1.25 V OV_FAULT threshold by ~30-60 mV against a protection margin
    // of several hundred mV — reprogramming for it protects nothing. It DOES cost four
    // PMBus transactions plus the settle delay below, which is what keeps the bus busy
    // while the temperature thread waits for its lock. Reported from the field on
    // v3.2.02u: five failed Vcore reads in 7.5 h, every one of them exactly on a round
    // boundary, where the correction loop calls this function on every ~100 ms tick.
    //
    // 25 mV is ~13 VOUT_COMMAND LSBs at N=-9 (1.953 mV), i.e. wider than one correction
    // step (capped at 50 mV, half-error so typically 12-25), so a real switch still
    // tracks while the small trims that dominate steady state are skipped.
    constexpr uint16_t LIMIT_REPROG_MV = 25;
    const uint16_t rail_delta = (uint16_t)((rail_mv > this->_limit_rail_mv)
                                             ? (rail_mv - this->_limit_rail_mv)
                                             : (this->_limit_rail_mv - rail_mv));
    if (this->_limit_rail_mv == 0 || rail_delta > LIMIT_REPROG_MV) {
        // The settle delay belongs to the LIMIT writes, not to VOUT_COMMAND itself
        // (see the measurement quoted in the previous version of this code: without
        // it the four limit writes NACK 100% of the time). Skipping the limits
        // therefore also skips 50 ms of held lock, which is the dominant cost of
        // this function.
        delay(50);
        this->_write_vout_limit_ratios(rail_mv);
        this->_limit_rail_mv = rail_mv;
    }
    LOG_D("TPS546D24A VOUT_COMMAND -> %dmV/die x%u = %dmV rail (raw 0x%04X)",
          per_die_mv, this->_vcore_series_count, rail_mv, raw);
}

void TPS546D24AClass::_write_vout_limit_ratios(uint16_t rail_mv){
    uint16_t ov_fault = (uint16_t)(rail_mv * 1.25f);
    uint16_t ov_warn  = (uint16_t)(rail_mv * 1.10f);
    uint16_t uv_warn  = (uint16_t)(rail_mv * 0.90f);
    uint16_t uv_fault = (uint16_t)(rail_mv * 0.75f);

    // The TPS546D24A NACKs limit writes that fall outside the [VOUT_MIN, VOUT_MAX]
    // hardware clamp window or that break OV_FAULT > OV_WARN > VOUT_COMMAND >
    // UV_WARN > UV_FAULT. A low rail (e.g. 1800 mV at 900 mV/die) otherwise pushes the
    // UV limits below VOUT_MIN and gets rejected. Clamp into the valid window and keep
    // the warn/fault ordering intact before writing.
    uint16_t vmin = this->_cfg.vout_min_mv;
    uint16_t vmax = this->_cfg.vout_max_mv;
    if (ov_fault > vmax) ov_fault = vmax;
    if (ov_warn  > vmax) ov_warn  = vmax;
    if (ov_warn  >= ov_fault) ov_warn = ov_fault - 1;
    if (uv_fault < vmin) uv_fault = vmin;
    if (uv_warn  < vmin) uv_warn  = vmin;
    if (uv_warn  <= uv_fault) uv_warn = uv_fault + 1;

    this->_write_word(PMBUS_VOUT_OV_FAULT_LIMIT, this->_mv_to_ulinear16(ov_fault));
    this->_write_word(PMBUS_VOUT_OV_WARN_LIMIT,  this->_mv_to_ulinear16(ov_warn));
    this->_write_word(PMBUS_VOUT_UV_WARN_LIMIT,  this->_mv_to_ulinear16(uv_warn));
    this->_write_word(PMBUS_VOUT_UV_FAULT_LIMIT, this->_mv_to_ulinear16(uv_fault));
}

void TPS546D24AClass::set_vcore_range(uint16_t min_mv, uint16_t max_mv){
    this->_vcore_min_mv = min_mv;
    this->_vcore_max_mv = max_mv;
    LOG_I("Vcore range from %dmv to %dmv", this->_vcore_min_mv, this->_vcore_max_mv);
}

uint32_t TPS546D24AClass::get_vbus(void){
    // ADC path (active) — measured true INPUT voltage via the board's external
    // resistor-divider sense on the DC input side (do not read the TPS PMBus).
    uint32_t vadc = this->get_vbus_adc();   // mV, from the MCU ADC (post-divider)
    uint32_t vin_mv = (uint32_t)(vadc * GAIN_VBUS_SAMPLE);
    LOG_D("Vbus ADC raw %umV -> %u mV", vadc, vin_mv);
    return vin_mv;

    // // PMBus path (removed per request): READ_VIN (0x88) → SLINEAR11 → V → mV
    // if (!this->_device_ok) return 0;
    // uint16_t raw = 0;
    // this->_read_reg(PMBUS_READ_VIN, (uint8_t*)&raw, 2);
    // float vin_v = this->_slinear11_to_float(raw);
    // uint32_t vin_mv = (uint32_t)(vin_v * 1000.0f);
    // LOG_D("Vbus PMBus 0x%04X -> %.3f V -> %u mV", raw, vin_v, vin_mv);
    // return vin_mv;
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
    TPS_BUS_LOCK_DEFAULT();
    if (!_bus_guard.ok()) return 0;      // same value as the existing I2C-failure path
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
    TPS_BUS_LOCK_DEFAULT();
    if (!_bus_guard.ok()) return NAN;
    if (!this->_device_ok) return NAN;
    uint8_t data[2] = {0, 0};
    if (_read_reg(PMBUS_READ_TEMPERATURE_1, data, 2) != 0) return NAN;
    uint16_t raw = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    return _slinear11_to_float(raw);   // PHASE=0xFF -> hottest phase in the stack
}

bool TPS546D24AClass::is_oc_fault(void){
    TPS_BUS_LOCK_DEFAULT();
    if (!_bus_guard.ok()) return false;  // safe: OC_FAULT is sticky, next 50ms poll sees it
    if (!this->_device_ok) return false;
    uint8_t status_iout = 0;
    this->_read_reg(PMBUS_STATUS_IOUT, &status_iout, 1);
    return (status_iout & 0x80) != 0;  // bit7: OC_FAULT (sticky/latched)
}

bool TPS546D24AClass::is_oc_warn(void){
    TPS_BUS_LOCK_DEFAULT();
    if (!_bus_guard.ok()) return false;  // sticky bit — see is_oc_fault()
    if (!this->_device_ok) return false;
    uint8_t status_iout = 0;
    this->_read_reg(PMBUS_STATUS_IOUT, &status_iout, 1);
    return (status_iout & 0x20) != 0;  // bit5: OC_WARN (sticky/latched)
}

bool TPS546D24AClass::is_ot_fault(void){
    TPS_BUS_LOCK_DEFAULT();
    if (!_bus_guard.ok()) return false;  // sticky bit — see is_oc_fault()
    if (!this->_device_ok) return false;
    uint8_t status_temp = 0;
    this->_read_reg(PMBUS_STATUS_TEMPERATURE, &status_temp, 1);
    return (status_temp & 0x80) != 0;  // bit7: OT_FAULT (sticky/latched)
}

bool TPS546D24AClass::is_ot_warn(void){
    TPS_BUS_LOCK_DEFAULT();
    if (!_bus_guard.ok()) return false;  // sticky bit — see is_oc_fault()
    if (!this->_device_ok) return false;
    uint8_t status_temp = 0;
    this->_read_reg(PMBUS_STATUS_TEMPERATURE, &status_temp, 1);
    return (status_temp & 0x40) != 0;  // bit6: OT_WARN (sticky/latched)
}

void TPS546D24AClass::clear_faults(void){
    TPS_BUS_LOCK_DEFAULT();
    if (!_bus_guard.ok()) return;
    if (!this->_device_ok) return;
    this->_write_cmd(PMBUS_CLEAR_FAULTS);
}

float TPS546D24AClass::get_iout_amps(void){
    TPS_BUS_LOCK_DEFAULT();
    if (!_bus_guard.ok()) return NAN;
    if (!this->_device_ok) return NAN;
    uint16_t raw = 0;
    this->_read_reg(PMBUS_READ_IOUT, (uint8_t*)&raw, 2);
    return this->_slinear11_to_float(raw);
}

int8_t TPS546D24AClass::read_phase_iout_amps(uint8_t phase_idx, float *out){
    TPS_BUS_LOCK_DEFAULT();
    if (!_bus_guard.ok()) return -1;
    if (!this->_device_ok || !out) return -1;
    // PHASE (0x04) selects which device in the BCX stack answers the NEXT PMBus
    // read. hw_init leaves it at 0xFF (whole stack); switch to the phase, read
    // READ_IOUT, then restore 0xFF so every other reader keeps seeing the total.
    // The three-transaction sequence (PHASE / READ_IOUT / PHASE) is atomic under
    // _bus_mtx — no concurrent reader can slip in between and see the wrong phase.
    this->_write_byte(PMBUS_PHASE, phase_idx);
    uint16_t raw = 0;
    int8_t rc = (int8_t)this->_read_reg(PMBUS_READ_IOUT, (uint8_t*)&raw, 2);
    this->_write_byte(PMBUS_PHASE, TPS546D24A_PHASE_ALL);
    if (rc != 0) return rc;
    *out = this->_slinear11_to_float(raw);
    return 0;
}

float TPS546D24AClass::get_oc_limit_amps(void){
    // Report what the CHIP is actually enforcing, not what we asked for.
    // The two can differ (a rejected write, a POR value surviving hw_init, a
    // per-phase adjustment made by the stack), and printing the config constant
    // produced log lines that read as a contradiction:
    //   "Overcurrent FAULT detected! IOUT=64.12A limit=71.0A"
    // A fault line must quote the threshold that actually tripped.
    TPS_BUS_LOCK_DEFAULT();
    if (_bus_guard.ok() && this->_device_ok) {
        uint16_t raw = 0;
        if (this->_read_reg(PMBUS_IOUT_OC_FAULT_LIMIT, (uint8_t*)&raw, 2) == 0) {
            float limit = this->_slinear11_to_float(raw);
            if (limit > 0.0f) return limit;
        }
    }
    return this->_cfg.ifault_total;   // I2C unavailable -> best available value
}

float TPS546D24AClass::get_oc_warn_limit(void){
    // The chip's own OC_WARN threshold (PHASE=0xFF total). hw_threads compares the
    // live READ_IOUT against this to label a warning LIVE vs a stale latched bit.
    TPS_BUS_LOCK_DEFAULT();
    if (_bus_guard.ok() && this->_device_ok) {
        uint16_t raw = 0;
        if (this->_read_reg(PMBUS_IOUT_OC_WARN_LIMIT, (uint8_t*)&raw, 2) == 0) {
            float limit = this->_slinear11_to_float(raw);
            if (limit > 0.0f) return limit;
        }
    }
    return this->_cfg.iwarn_total;
}

float TPS546D24AClass::get_ot_warn_limit_celsius(void){
    TPS_BUS_LOCK_DEFAULT();
    if (_bus_guard.ok() && this->_device_ok) {
        uint16_t raw = 0;
        if (this->_read_reg(PMBUS_OT_WARN_LIMIT, (uint8_t*)&raw, 2) == 0) {
            float limit = this->_slinear11_to_float(raw);
            if (limit > 0.0f) return limit;
        }
    }
    return this->_cfg.tfault - 10.0f;   // hw_init writes warn = fault - 10
}

float TPS546D24AClass::get_ot_limit_celsius(void){
    // Same reasoning as get_oc_limit_amps(): quote the chip's OT_FAULT_LIMIT.
    TPS_BUS_LOCK_DEFAULT();
    if (_bus_guard.ok() && this->_device_ok) {
        uint16_t raw = 0;
        if (this->_read_reg(PMBUS_OT_FAULT_LIMIT, (uint8_t*)&raw, 2) == 0) {
            float limit = this->_slinear11_to_float(raw);
            if (limit > 0.0f) return limit;
        }
    }
    return this->_cfg.tfault;
}

void TPS546D24AClass::debugPrint(void){
    TPS_BUS_LOCK_DEFAULT();
    if (!_bus_guard.ok()) return;
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

    // Only expand the fault/warn bits that are actually set, so the healthy case
    // prints two compact lines. This keeps the whole message well under the
    // logger's 512-byte message buffer (the old verbose dump overflowed it).
    char flags[160];
    flags[0] = '\0';
#define APPEND_FLAG(name) do { \
        int _l = (int)strlen(flags); \
        snprintf(flags + _l, sizeof(flags) - _l, "%s%s", _l ? " " : "", (name)); \
    } while (0)
    if (status_vout  & 0x80) APPEND_FLAG("OV_FAULT");
    if (status_vout  & 0x40) APPEND_FLAG("OV_WARN");
    if (status_vout  & 0x20) APPEND_FLAG("UV_WARN");
    if (status_vout  & 0x10) APPEND_FLAG("UV_FAULT");
    if (status_vout  & 0x08) APPEND_FLAG("MIN_MAX_CLAMP");
    if (status_vout  & 0x04) APPEND_FLAG("TON_MAX");
    if (status_iout  & 0x80) APPEND_FLAG("OC_FAULT");
    if (status_iout  & 0x20) APPEND_FLAG("OC_WARN");
    if (status_input & 0x80) APPEND_FLAG("VIN_OV");
    if (status_input & 0x10) APPEND_FLAG("VIN_UV");
    if (status_input & 0x04) APPEND_FLAG("IIN_OC");
    if (status_temp  & 0x80) APPEND_FLAG("OT_FAULT");
    if (status_temp  & 0x40) APPEND_FLAG("OT_WARN");
    if (status_mfr   & 0x80) APPEND_FLAG("POR");
    if (status_mfr   & 0x40) APPEND_FLAG("SELF");
#undef APPEND_FLAG

    char pgood_str[24];
    if (this->_vcore_pgood_pin < 0) {
        snprintf(pgood_str, sizeof(pgood_str), "n/a");
    } else {
        snprintf(pgood_str, sizeof(pgood_str), "%s(pin%d)",
                 digitalRead(this->_vcore_pgood_pin) == HIGH ? "HIGH" : "LOW",
                 this->_vcore_pgood_pin);
    }

    char buf[384];
    snprintf(buf, sizeof(buf),
        "\nTPS546D24A OC: VOUT=%.3fV(cmd%.3f) IOUT=%.2fA lim=%.1fA T=%.1fC"
        "\nSTAT VO=%02X IO=%02X IN=%02X TP=%02X MF=%02X WORD=%04X PGOOD=%s%s%s",
        vout, vout_cmd, iout, iout_limit_a, temp_c,
        status_vout, status_iout, status_input, status_temp, status_mfr, status_word,
        pgood_str,
        flags[0] ? "\nFLAGS: " : "", flags);
    LOG_W("%s", buf);
}

void TPS546D24AClass::dump(void) {
    TPS_BUS_LOCK(500);   // ~30 transactions, longer than the default window
    if (!_bus_guard.ok()) return;
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
