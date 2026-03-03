#pragma once
#include <stdint.h>
#include "drivers/iic/i2c_master.h"

// // FT6206 I2C address
// #define FT6206_ADDR           0x38
// #define FT6206_REG_NUMTOUCHES 0x02
// #define FT6206_REG_THRESHHOLD 0x80
// #define FT6206_REG_POINTID    0x00


#define FT62XX_DEFAULT_ADDR         0x38 //!< I2C address
#define FT62XX_G_FT5201ID           0xA8 //!< FocalTech's panel ID
#define FT62XX_REG_NUMTOUCHES       0x02 //!< Number of touch points

#define FT62XX_NUM_X                0x33 //!< Touch X position
#define FT62XX_NUM_Y                0x34 //!< Touch Y position

#define FT62XX_REG_MODE             0x00 //!< Device mode, either WORKING or FACTORY
#define FT62XX_REG_CALIBRATE        0x02 //!< Calibrate mode
#define FT62XX_REG_WORKMODE         0x00 //!< Work mode
#define FT62XX_REG_FACTORYMODE      0x40 //!< Factory mode
#define FT62XX_REG_THRESHHOLD       0x80 //!< Threshold for touch detection
#define FT62XX_REG_POINTRATE        0x88 //!< Point rate
#define FT62XX_REG_FIRMVERS         0xA6 //!< Firmware version
#define FT62XX_REG_CHIPID           0xA3 //!< Chip selecting
#define FT62XX_REG_VENDID           0xA8 //!< FocalTech's panel ID

#define FT62XX_VENDID               0x11 //!< FocalTech's panel ID
#define FT6206_CHIPID               0x06 //!< Chip selecting
#define FT6236_CHIPID               0x36 //!< Chip selecting
#define FT6236U_CHIPID              0x64 //!< Chip selecting
#define FT6336U_CHIPID              0x64 //!< Chip selecting

// calibrated for Adafruit 2.8" ctp screen
#define FT62XX_DEFAULT_THRESHOLD    128 //!< Default threshold for touch detection

// Touch point data structure
struct TS_Point {
    int16_t x;
    int16_t y;
    uint8_t z;  // pressure/size
};

class FT6206Class {
private:
    uint8_t _threshold;
    
    uint8_t readRegister8(uint8_t reg);
    void writeRegister8(uint8_t reg, uint8_t val);
    
public:
    FT6206Class();
    
    bool begin(uint8_t threshold = 40);
    bool touched();
    TS_Point getPoint(uint8_t n = 0);
    uint8_t getTouches();
};

