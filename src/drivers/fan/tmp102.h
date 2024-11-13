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

float get_vcore_temperature();
float get_asic_temperature();
float get_mcu_temperature();
#endif 