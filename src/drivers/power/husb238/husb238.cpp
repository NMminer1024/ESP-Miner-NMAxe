#include "husb238.h"
#include "Wire.h"
#include "logger.h"

uint8_t husb238_readRegister(uint8_t deviceAddress, uint8_t registerAddress, uint8_t *data, uint8_t length) {
    Wire.beginTransmission(deviceAddress); 
    Wire.write(registerAddress); 
    if (Wire.endTransmission() != 0) { 
        return 1; 
    }

    Wire.requestFrom(deviceAddress, length); 
    uint8_t index = 0;
    while (Wire.available() && index < length) {
        data[index++] = Wire.read(); 
    }

    if (index != length) {
        return 2; 
    }

    return 0; 
}


uint8_t husb238_get_register(uint8_t reg_addr) {
    uint8_t value;
    uint8_t status = husb238_readRegister(HUSB238_IIC_ADDR, reg_addr, &value, 1);
    if(status != 0) {
        LOG_E("husb238 get register 0x%02x failed.code: 0x%02x", reg_addr, status);
    }
    return value;
}

void husb238_writeRegister(uint8_t deviceAddress, uint8_t registerAddress, uint8_t data) {
    Wire.beginTransmission(deviceAddress); 
    Wire.write(registerAddress); 
    Wire.write(data); 
    Wire.endTransmission(); 
}


String parse_current(uint8_t value) {
    switch (value)
    {
    case HUSB238_CURRENT_0_5A:
        return "0.5A";
    case HUSB238_CURRENT_0_7A:
        return "0.7A";
    case HUSB238_CURRENT_1_0A:
        return "1.0A";
    case HUSB238_CURRENT_1_25A:
        return "1.25A";
    case HUSB238_CURRENT_1_5A:
        return "1.5A";
    case HUSB238_CURRENT_1_75A:
        return "1.75A";
    case HUSB238_CURRENT_2_0A:
        return "2.0A";
    case HUSB238_CURRENT_2_25A:
        return "2.25A";
    case HUSB238_CURRENT_2_5A:
        return "2.5A";
    case HUSB238_CURRENT_2_75A:
        return "2.75A";
    case HUSB238_CURRENT_3_0A:
        return "3.0A";
    case HUSB238_CURRENT_3_25A:
        return "3.25A";
    case HUSB238_CURRENT_3_5A:
        return "3.5A";
    case HUSB238_CURRENT_4_0A:
        return "4.0A";
    case HUSB238_CURRENT_4_5A:
        return "4.5A";
    case HUSB238_CURRENT_5_0A:
        return "5.0A";
    default:
        return "Unknown";
    }
}



void husb238_monitor_entry(void *arg){

    while(1){
        uint8_t vaule = husb238_get_register(HUSB238_PD_STATUS0_ADDR);
        LOG_I("status0: 0x%02x", vaule);

        vaule = husb238_get_register(HUSB238_PD_STATUS1_ADDR);
        LOG_I("status1: 0x%02x", vaule);

        vaule = husb238_get_register(HUSB238_SRC_PDO_5V_ADDR);
        LOG_I("SRC_PDO_5V dectect : %s, current: %s", (vaule & 0x80 == 0x80) ? "yes" : "no", parse_current(vaule & 0x0f).c_str());

        vaule = husb238_get_register(HUSB238_SRC_PDO_9V_ADDR);
        LOG_I("SRC_PDO_9V dectect : %s, current: %s", (vaule & 0x80 == 0x80) ? "yes" : "no", parse_current(vaule & 0x0f).c_str());

        vaule = husb238_get_register(HUSB238_SRC_PDO_12V_ADDR);
        LOG_I("SRC_PDO_12V dectect: %s, current: %s", (vaule & 0x80 == 0x80) ? "yes" : "no", parse_current(vaule & 0x0f).c_str());

        vaule = husb238_get_register(HUSB238_SRC_PDO_15V_ADDR);
        LOG_I("SRC_PDO_15V dectect: %s, current: %s", (vaule & 0x80 == 0x80) ? "yes" : "no", parse_current(vaule & 0x0f).c_str());

        vaule = husb238_get_register(HUSB238_SRC_PDO_18V_ADDR);
        LOG_I("SRC_PDO_18V dectect: %s, current: %s", (vaule & 0x80 == 0x80) ? "yes" : "no", parse_current(vaule & 0x0f).c_str());

        vaule = husb238_get_register(HUSB238_SRC_PDO_20V_ADDR);
        LOG_I("SRC_PDO_20V dectect: %s, current: %s", (vaule & 0x80 == 0x80) ? "yes" : "no", parse_current(vaule & 0x0f).c_str());

        vaule = husb238_get_register(HUSB238_SRC_PDO_ADDR);
        LOG_I("pdo    : 0x%02x", vaule);

        vaule = husb238_get_register(HUSB238_GO_COMMAND_ADDR);
        LOG_I("go     : 0x%02x", vaule);

        LOG_W("-------------------------------------------------");
        delay(1000);
    }
}