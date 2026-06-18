#ifndef CRC___H_
#define CRC___H_
#include <Arduino.h>

#define CRC5_MASK 0x1F

uint8_t crc5(uint8_t *data, uint8_t len);
uint16_t crc16_false(uint8_t *buffer, uint16_t len);


#endif



