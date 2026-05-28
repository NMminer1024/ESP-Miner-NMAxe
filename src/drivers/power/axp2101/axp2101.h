#pragma once
#include <Arduino.h>

// ── AXP2101 I2C address ───────────────────────────────────────────────────────
#define AXP2101_IIC_ADDR            (uint8_t)(0x34)

// ── Power-path / charger registers ───────────────────────────────────────────
#define AXP2101_REG_VSYS_MIN        0x14   // VSYS minimum DPM threshold (bits[6:4]: 000=4.1V, step 100mV)
#define AXP2101_REG_VINDPM          0x15   // VBUS input voltage DPM      (bits[3:0]: 0x00=3.88V, step 80mV)
#define AXP2101_REG_VBUSLIM         0x16   // VBUS input current limit    (bits[2:0]: 000=100mA .. 100=1500mA)
#define AXP2101_REG_DCDC_OVP_UVP    0x23   // DC-DC OVP/UVP triggered PMIC shutdown enable
#define AXP2101_REG_FAST_PWRON0     0x28   // Fast power-on sequence config 0 (DC4/DC3/DC2/DC1)
#define AXP2101_REG_FAST_PWRON1     0x29   // Fast power-on sequence config 1 (ALDO3/ALDO2/ALDO1/DC5)
#define AXP2101_REG_ITERM_CHG       0x63   // Charger termination current (bits[3:0]: 0x8=200mA)

// ── Rail enable registers ─────────────────────────────────────────────────────
#define AXP2101_REG_DCDC_EN         0x80   // DC-DC enable (bits[4:1] = DC5/4/3/2)
#define AXP2101_REG_LDO_EN          0x90   // LDO enable   (ALDO1-4, BLDO1-2, CPUSLDO, DLDO1)
#define AXP2101_REG_DLDO2_EN        0x91   // DLDO2 enable (bit0)

// ── Voltage output registers ──────────────────────────────────────────────────
#define AXP2101_REG_DC2_VOL         0x83
#define AXP2101_REG_DC3_VOL         0x84
#define AXP2101_REG_DC4_VOL         0x85
#define AXP2101_REG_DC5_VOL         0x86
#define AXP2101_REG_ALDO1_VOL       0x92
#define AXP2101_REG_ALDO2_VOL       0x93
#define AXP2101_REG_ALDO3_VOL       0x94
#define AXP2101_REG_ALDO4_VOL       0x95
#define AXP2101_REG_BLDO1_VOL       0x96
#define AXP2101_REG_BLDO2_VOL       0x97
#define AXP2101_REG_CPUSLDO_VOL     0x98
#define AXP2101_REG_DLDO1_VOL       0x99
#define AXP2101_REG_DLDO2_VOL       0x9A

// ── Voltage register values ───────────────────────────────────────────────────
// DC-DC converters: bits[6:0], step 10mV from 500mV
#define AXP2101_DC2_1000MV          0x32
#define AXP2101_DC3_3300MV          0x69
#define AXP2101_DC4_1000MV          0x32
// DC5: bits[4:0], step 100mV from 1400mV
#define AXP2101_DC5_3300MV          0x13
// LDO outputs: bits[4:0], step 100mV from 500mV
#define AXP2101_LDO_3300MV          0x1C
#define AXP2101_BLDO1_1500MV        0x0A
#define AXP2101_BLDO2_2800MV        0x17
#define AXP2101_CPUSLDO_1000MV      0x0A

// ── Named field values ────────────────────────────────────────────────────────
#define AXP2101_VSYS_MIN_4V1        (uint8_t)(0b000 << 4)  // 4.1 V
#define AXP2101_VINDPM_3V88         0x00                   // 3.88 V (lowest)
#define AXP2101_VBUS_1500MA         0x04                   // 1500 mA
#define AXP2101_DCDC_OVP_UVP_OFF    0x00                   // disable DC-DC OVP/UVP shutdown
#define AXP2101_FAST_PWRON_ALL_DIS  0xFF                   // disable fast power-on for all rails
#define AXP2101_ITERM_200MA         0x08                   // 200 mA termination

// ── API ───────────────────────────────────────────────────────────────────────
bool axp2101_init(void);
bool axp2101_read_reg(uint8_t reg, uint8_t *val);
bool axp2101_write_reg(uint8_t reg, uint8_t val);
bool axp2101_update_reg(uint8_t reg, uint8_t mask, uint8_t value);
