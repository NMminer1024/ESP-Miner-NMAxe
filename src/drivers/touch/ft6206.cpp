#include "ft6206.h"
#include "logger.h"

FT6206Class::FT6206Class() {
    _threshold = 20;
}

uint8_t FT6206Class::readRegister8(uint8_t reg) {
    uint8_t data = 0;
    esp_err_t ret = i2c_master_register_read(FT62XX_DEFAULT_ADDR, reg, &data, 1);
    if (ret != ESP_OK) {
        // LOG_E("FT6206: Failed to read reg 0x%02x, error 0x%x", reg, ret);
        return 0;
    }
    return data;
}

void FT6206Class::writeRegister8(uint8_t reg, uint8_t val) {
    esp_err_t ret = i2c_master_register_write_byte(FT62XX_DEFAULT_ADDR, reg, val);
    if (ret != ESP_OK) {
        // LOG_E("FT6206: Failed to write reg 0x%02x = 0x%02x, error 0x%x", reg, val, ret);
    }
}

bool FT6206Class::begin(uint8_t threshold) {
    _threshold = threshold;
    
    // Try to read vendor ID register (0xA8) - should return 0x11 for FT6206
    uint8_t vendorID = readRegister8(0xA8);
    if (vendorID != 0x11) {
        // LOG_E("FT6206: Invalid vendor ID 0x%02x (expected 0x11)", vendorID);
        return false;
    }
    
    // Read chip ID register (0xA3) - should return 0x06 for FT6206
    uint8_t chipID = readRegister8(0xA3);
    if (chipID != 0x06 && chipID != 0x64) {  // 0x64 for some variants
        // LOG_E("FT6206: Invalid chip ID 0x%02x (expected 0x06 or 0x64)", chipID);
        return false;
    }
    
    // Set threshold
    writeRegister8(FT62XX_REG_THRESHHOLD, _threshold);
    
    LOG_D("FT6206: Initialized successfully (vendor=0x%02x, chip=0x%02x, threshold=%d)", vendorID, chipID, _threshold);
    return true;
}

bool FT6206Class::touched() {
    uint8_t touches = readRegister8(FT62XX_REG_NUMTOUCHES);
    return (touches > 0);
}

uint8_t FT6206Class::getTouches() {
    return readRegister8(FT62XX_REG_NUMTOUCHES);
}

TS_Point FT6206Class::getPoint(uint8_t n) {
    TS_Point point;
    point.x = 0;
    point.y = 0;
    point.z = 0;
    
    if (n > 1) return point;  // FT6206 only supports 2 touch points
    
    // Read touch data (6 bytes per touch point starting at 0x03)
    uint8_t data[6];
    uint8_t startReg = 0x03 + (n * 6);
    
    esp_err_t ret = i2c_master_register_read(FT62XX_DEFAULT_ADDR, startReg, data, 6);
    if (ret != ESP_OK) {
        // LOG_E("FT6206: Failed to read touch point %d", n);
        return point;
    }
    
    // Parse touch data
    // Byte 0: Event flag (high 4 bits) + X position high 4 bits (low 4 bits)
    // Byte 1: X position low 8 bits
    // Byte 2: Touch ID (high 4 bits) + Y position high 4 bits (low 4 bits)  
    // Byte 3: Y position low 8 bits
    // Byte 4: Weight (pressure)
    // Byte 5: Misc
    
    point.x = ((data[0] & 0x0F) << 8) | data[1];
    point.y = ((data[2] & 0x0F) << 8) | data[3];
    point.z = data[4];  // Weight/pressure
    
    return point;
}
