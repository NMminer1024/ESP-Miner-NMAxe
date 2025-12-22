#ifndef __NMAXE_BOARD_H_
#define __NMAXE_BOARD_H_
#include "BM1366.h"
#include "power_hal.h"

inline BMxxx* create_axe_asic_instance(HardwareSerial& serial, uint32_t baud, uint8_t rx, uint8_t tx, uint8_t rst) {
    return new BM1366(serial, baud, rx, tx, rst);
}



#endif // __NMAXE_BOARD_H_