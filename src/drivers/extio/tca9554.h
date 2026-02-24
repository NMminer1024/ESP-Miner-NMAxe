#ifndef __TCA9554__H__
#define __TCA9554__H__
#include <Arduino.h>

#define TCA9554_IO_0                    (uint8_t)(1<<0)
#define TCA9554_IO_1                    (uint8_t)(1<<1)
#define TCA9554_IO_2                    (uint8_t)(1<<2)
#define TCA9554_IO_3                    (uint8_t)(1<<3)
#define TCA9554_IO_4                    (uint8_t)(1<<4)
#define TCA9554_IO_5                    (uint8_t)(1<<5)
#define TCA9554_IO_6                    (uint8_t)(1<<6)
#define TCA9554_IO_7                    (uint8_t)(1<<7)


void tca9554_set_io_level(uint8_t iobit, uint8_t level);
void tca9554_init();

#endif // __TCA9554__H__