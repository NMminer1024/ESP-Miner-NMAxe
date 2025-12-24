#ifndef _AXE_POWER_HAL_H_
#define _AXE_POWER_HAL_H_
#include <Arduino.h>
#include <esp_adc_cal.h>

typedef enum{
    PWR_OFF = 0,
    PWR_ON  = 1,
} power_state_t;

typedef struct{
    uint8_t pwr_0v8;
    uint8_t pwr_1v8;
    uint8_t pwr_vcore;
} axe_pwr_enable_pin_t;

typedef struct{
    uint8_t vbus;
    uint8_t ibus;
    uint8_t vcore;
} axe_pwr_adc_pin_t;

class AxePowerHal{
private:
    esp_adc_cal_characteristics_t *_vbus_adc_chars;
    esp_adc_cal_characteristics_t *_ibus_adc_chars;
    esp_adc_cal_characteristics_t *_vcore_adc_chars;
protected:
    axe_pwr_enable_pin_t _asic_pwr_enable_pins;
    axe_pwr_adc_pin_t    _asic_pwr_adc_pins;
public:
    AxePowerHal(axe_pwr_enable_pin_t en_pins, axe_pwr_adc_pin_t adc_pins);
    ~AxePowerHal();
    virtual bool init(void) = 0;
    virtual void set_vdd_1v8(power_state_t state) = 0;
    virtual void set_pll_0v8(power_state_t state) = 0;
    virtual void set_vcore_status(power_state_t state) = 0;
    virtual void set_vcore_voltage(uint16_t voltage_mv) = 0;
    virtual void set_vcore_range(uint16_t min_mv, uint16_t max_mv) = 0;
    virtual uint16_t get_vcore_min(void) = 0;
    virtual uint16_t get_vcore_max(void) = 0;
    virtual bool is_vcore_ready(void) = 0;
    virtual bool is_dc_pluged(void) = 0;
    virtual bool is_adc_ready(void) = 0;
    
    uint32_t get_vbus_adc(void);
    uint32_t get_ibus_adc(void);
    uint32_t get_vcore_adc(void);

    virtual uint32_t get_vbus(void) = 0;
    virtual uint32_t get_ibus(void) = 0;
    virtual uint32_t get_vcore(void) = 0;
};
#endif 