#ifndef _AXE_POWER_H_
#define _AXE_POWER_H_
#include <Arduino.h>
#include "power_hal.h"

class AxePowerClass{
private:
    AxePowerHal *_power_instance;
protected:

public:
    AxePowerClass(AxePowerHal *instance);
    ~AxePowerClass();
    SemaphoreHandle_t  vcore_ready_xsem;//vcore power and vbus are ready

    /** standard power functions **/
    void init(void);
    void set_vdd_1v8(power_state_t state);
    void set_pll_0v8(power_state_t state);
    void set_vcore_status(power_state_t state);
    void set_vcore_voltage(uint16_t req_mv);
    void set_vcore_range(uint16_t min_mv, uint16_t max_mv);

    uint16_t get_vcore_min(void){ return this->_power_instance->get_vcore_min();};
    uint16_t get_vcore_max(void){ return this->_power_instance->get_vcore_max();};

    uint32_t get_vbus(void);
    uint32_t get_ibus(void);
    uint32_t get_vcore(void);
    bool is_vcore_ready(void);
    bool is_dc_pluged(void);
    bool is_adc_ready(void);
};
#endif 