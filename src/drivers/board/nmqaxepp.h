#ifndef __NMAXEPP_BOARD_H_
#define __NMAXEPP_BOARD_H_
#include "BM1370.h"
#include "power_hal.h"

inline BMxxx* create_qaxepp_asic_instance(HardwareSerial& serial, uint32_t baud, uint8_t rx, uint8_t tx, uint8_t rst) {
    return new BM1370(serial, baud, rx, tx, rst);
}

#endif // __NMAXEPP_BOARD_H_