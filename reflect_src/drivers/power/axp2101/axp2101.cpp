#include "axp2101.h"
#include "drivers/iic/i2c_master.h"
#include "utils/logger/logger.h"

// ── Low-level register access (mirrors tca9554.cpp style) ───────────────────

bool axp2101_read_reg(uint8_t reg, uint8_t *val) {
    esp_err_t ret = i2c_master_register_read(AXP2101_IIC_ADDR, reg, val, 1);
    if (ret != ESP_OK) {
        LOG_E("AXP2101 read reg 0x%02X failed: %d", reg, ret);
        return false;
    }
    return true;
}

bool axp2101_write_reg(uint8_t reg, uint8_t val) {
    esp_err_t ret = i2c_master_register_write_byte(AXP2101_IIC_ADDR, reg, val);
    if (ret != ESP_OK) {
        LOG_E("AXP2101 write reg 0x%02X failed: %d", reg, ret);
        return false;
    }
    return true;
}

bool axp2101_update_reg(uint8_t reg, uint8_t mask, uint8_t value) {
    uint8_t cur = 0;
    if (!axp2101_read_reg(reg, &cur)) return false;
    cur = (cur & ~mask) | (value & mask);
    return axp2101_write_reg(reg, cur);
}

// ── Initialization sequence ──────────────────────────────────────────────────

struct axp2101_reg_update_t {
    uint8_t     reg;
    uint8_t     mask;
    uint8_t     value;
    const char *name;
};

static bool _apply_updates(const axp2101_reg_update_t *updates, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (!axp2101_update_reg(updates[i].reg, updates[i].mask, updates[i].value)) {
            LOG_E("AXP2101 [%s] failed", updates[i].name);
            return false;
        }
    }
    return true;
}

bool axp2101_init(void) {
    /* Step 1: raise VBUS current limit BEFORE enabling any rail.
     * Default is 500 mA; raise to 1500 mA to avoid VSYS droop at full load. */
    static const axp2101_reg_update_t power_path[] = {
        {AXP2101_REG_VBUSLIM,      0x07, AXP2101_VBUS_1500MA,        "VBUS limit 1500mA"},
    };
    if (!_apply_updates(power_path, 1)) {
        LOG_E("AXP2101 power-path config failed");
        return false;
    }

    /* Step 2: configure power-path / charger and set rail voltages. */
    static const axp2101_reg_update_t voltage_cfg[] = {
        {AXP2101_REG_VSYS_MIN,     0x70, AXP2101_VSYS_MIN_4V1,       "vsys_min 4.1V"},
        {AXP2101_REG_FAST_PWRON0,  0xFF, AXP2101_FAST_PWRON_ALL_DIS, "fast_pwron DC4/3/2/1 off"},
        {AXP2101_REG_FAST_PWRON1,  0xFF, AXP2101_FAST_PWRON_ALL_DIS, "fast_pwron ALDO/DC5 off"},
        {AXP2101_REG_ITERM_CHG,    0x0F, AXP2101_ITERM_200MA,        "iterm 200mA"},
        {AXP2101_REG_DCDC_OVP_UVP, 0xFF, AXP2101_DCDC_OVP_UVP_OFF,  "dcdc ovp/uvp off"},
        {AXP2101_REG_VINDPM,       0x0F, AXP2101_VINDPM_3V88,        "vindpm 3.88V"},
        /* Rail voltages — adjust to match board schematic as needed */
        {AXP2101_REG_BLDO1_VOL,    0x1F, AXP2101_BLDO1_1500MV,      "BLDO1 1500mV"},
        {AXP2101_REG_DC2_VOL,      0x7F, AXP2101_DC2_1000MV,        "DC2 1000mV"},
        {AXP2101_REG_DC3_VOL,      0x7F, AXP2101_DC3_3300MV,        "DC3 3300mV"},
        {AXP2101_REG_DC4_VOL,      0x7F, AXP2101_DC4_1000MV,        "DC4 1000mV"},
        {AXP2101_REG_DC5_VOL,      0x1F, AXP2101_DC5_3300MV,        "DC5 3300mV"},
        {AXP2101_REG_ALDO1_VOL,    0x1F, AXP2101_LDO_3300MV,        "ALDO1 3300mV"},
        {AXP2101_REG_ALDO2_VOL,    0x1F, AXP2101_LDO_3300MV,        "ALDO2 3300mV"},
        {AXP2101_REG_ALDO3_VOL,    0x1F, AXP2101_LDO_3300MV,        "ALDO3 3300mV"},
        {AXP2101_REG_ALDO4_VOL,    0x1F, AXP2101_LDO_3300MV,        "ALDO4 3300mV"},
        {AXP2101_REG_BLDO2_VOL,    0x1F, AXP2101_BLDO2_2800MV,      "BLDO2 2800mV"},
        {AXP2101_REG_CPUSLDO_VOL,  0x1F, AXP2101_CPUSLDO_1000MV,    "CPUSLDO 1000mV"},
        {AXP2101_REG_DLDO1_VOL,    0x1F, AXP2101_LDO_3300MV,        "DLDO1 3300mV"},
        {AXP2101_REG_DLDO2_VOL,    0x1F, AXP2101_LDO_3300MV,        "DLDO2 3300mV"},
    };
    if (!_apply_updates(voltage_cfg, sizeof(voltage_cfg) / sizeof(voltage_cfg[0]))) {
        return false;
    }

    /* Step 3: enable output rails. */
    static const axp2101_reg_update_t enable_cfg[] = {
        {AXP2101_REG_DCDC_EN,   0x1E, 0x1E, "enable DC2-DC5"},
        {AXP2101_REG_LDO_EN,    0xFF, 0xFF, "enable ALDO/BLDO/CPUSLDO/DLDO1"},
        {AXP2101_REG_DLDO2_EN,  0x01, 0x01, "enable DLDO2"},
    };
    if (!_apply_updates(enable_cfg, sizeof(enable_cfg) / sizeof(enable_cfg[0]))) {
        return false;
    }

    LOG_I("AXP2101 init OK — power rails enabled");
    return true;
}
