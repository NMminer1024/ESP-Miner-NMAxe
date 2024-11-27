#ifndef _GLOBAL_H
#define _GLOBAL_H
#include <Arduino.h>
#include "device.h"
#include "nm_pwr.h"
#include "connection.h"
#include "tmp102.h"
#include "stratum.h"
#include "miner.h"

#define CURRENT_FW_VERSION  "v2.1.142"
#define CURRENT_HW_VERSION  "v1.1.1"


#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 135

#define ASIC_TEMP_DANGER    (65.0f)
#define VCORE_TEMP_DANGER   (90.0f)
#define VCORE_TEMP_LOW      (50.0f)
#define BOARD_MCU_DANGER    (60.0f)

#define FAN_FULL_RPM_MIN    (4800)

#define WIFI_RSSI_STRONG    (-60)
#define WIFI_RSSI_GOOD      (-70)

enum{
    TASK_PRIORITY_FAN      = 1, // lowest priority
    TASK_PRIORITY_PWR      ,
    TASK_PRIORITY_BTN      ,
    TASK_PRIORITY_LED      ,
    TASK_PRIORITY_UI       ,
    TASK_PRIORITY_LVGL_DRV ,
    TASK_PRIORITY_WS       ,
    TASK_PRIORITY_MONITOR  ,
    TASK_PRIORITY_STRATUM  ,
    TASK_PRIORITY_MARKET   ,
    TASK_PRIORITY_CONFIG_MONITOR,
    TASK_PRIORITY_MINER_TX ,
    TASK_PRIORITY_MINER_RX     //highest priority
};


typedef struct{
    String      fw_version;
    String      hw_version;
    String      hw_model;
    String      devcie_code;
    float       temp_mcu;
    float       temp_vcore;
    uint16_t    vbus;//mV
    uint16_t    ibus;//mA
}board_info_t;

typedef struct{
    bool        is_auto_speed;
    bool        invert_ploarity;
    bool        self_test;
    uint16_t    speed;//%
    uint16_t    rpm;//RPM
}fan_info_t;

typedef struct{
    bool   flip;
}screen_info_t;

typedef struct{
    bool   indicator;
}led_info_t;

typedef struct{
    String    type;
    uint16_t  frequency_req;//MHz
    uint16_t  vcore_req;//mV
    uint16_t  vcore_measured;//mV
    float     temp;
}asic_info_t;

typedef struct{
    wifi_info_t    wifi; 
    pool_info_t    pool;
    stratum_info_t stratum;
}connect_info_t;

typedef struct{
    String   firmware;//name
    bool     ota_running;
    int      progress;
}ota_info_t;

typedef struct{
    uint32_t share_rejected;
    uint32_t share_accepted;
    uint64_t uptime;
    double   hashrate;
    uint16_t block_hits;
    double   best_session;
    double   best_ever;
    double   pool_diff;
    double   last_diff;
    double   network_diff;
    SemaphoreHandle_t  nvs_save_xsem;//save status to NVS signal
    SemaphoreHandle_t  update_xsem;  //miner status update signal
}miner_status_t;

typedef struct{
    float       price;
    bool        connected;
}market_info_t;

typedef struct{
    board_info_t     board;
    fan_info_t       fan;
    screen_info_t    screen;
    led_info_t       led;
    asic_info_t      asic;
    connect_info_t   connection;
    miner_status_t   mstatus;
    ota_info_t       ota;
    market_info_t    market;
    NMAxePowerClass  power;
    StratumClass     stratum;
    AsicMinerClass   *miner;
}axe_sal_t;


extern axe_sal_t g_nmaxe;
#endif
