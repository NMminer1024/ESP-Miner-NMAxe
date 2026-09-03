#ifndef _TMP102__H_
#define _TMP102__H_
#include <Arduino.h>


#define TMP102_IIC_ASIC_ADDR                (uint8_t)(0x49)
#define TMP102_IIC_VCORE_ADDR               (uint8_t)(0x48)

#define TMP102_12BIT_RESOLUTION         (float)(0.0625f)//0.0625 degree celsius per bit
#define TMP102_DEV_TMP_ADDR             (uint8_t)(0x00 & 0x03)
#define TMP102_DEV_CFG_ADDR             (uint8_t)(0x01 & 0x03)
#define TMP102_DEV_TLOW_ADDR            (uint8_t)(0x02 & 0x03)
#define TMP102_DEV_THIGH_ADDR           (uint8_t)(0x03 & 0x03)

void tmp102_init();
float get_vcore_temperature();
float get_asic_temperature();

// Logs address/temperature/config register for one channel (chipaddr = TMP102_IIC_*_ADDR).
void tmp102_debug_print(uint8_t chipaddr, const char* label);

// Register TMP102 channels into the temperature HAL
void tmp102_register_vcore_temp_hal(void); // vcore only
void tmp102_register_asic_temp_hal(void);  // asic only
void tmp102_register_temp_hal(void);       // both

#endif 