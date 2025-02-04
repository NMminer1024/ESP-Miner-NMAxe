#ifndef _GLOBAL_H
#define _GLOBAL_H
#include <Arduino.h>
#include "device.h"
#include "nm_pwr.h"
#include "connection.h"
#include "tmp102.h"
#include "stratum.h"
#include "miner.h"
#include "market.h"

#define CURRENT_FW_VERSION  "v2.5.02t"
#define CURRENT_HW_VERSION  "v1.1.1"


#define ASIC_TEMP_DANGER    (65.0f)
#define VCORE_TEMP_DANGER   (90.0f)
#define VCORE_TEMP_LOW      (50.0f)
#define BOARD_MCU_DANGER    (60.0f)
#define BOARD_LOW_POWER     (6.0f)   //Watt

#define NVS_SAVE_INTERVAL   (60*60)  //second

#define FAN_FULL_RPM_MIN    (4200)

#define WIFI_RSSI_STRONG    (-60)
#define WIFI_RSSI_GOOD      (-70)

#define GLOBAL_ALIVE_TIMEOUT (1000*60*10)//10 minutes

enum{
    TASK_PRIORITY_FAN      = 1, // lowest priority
    TASK_PRIORITY_SWARM    ,
    TASK_PRIORITY_PWR      ,
    TASK_PRIORITY_ASIC_INIT,
    TASK_PRIORITY_BTN      ,
    TASK_PRIORITY_LED      ,
    TASK_PRIORITY_UI       ,
    TASK_PRIORITY_LVGL_DRV ,
    TASK_PRIORITY_WS       ,
    TASK_PRIORITY_MONITOR  ,
    TASK_PRIORITY_WIFI     ,
    TASK_PRIORITY_MARKET   ,
    TASK_PRIORITY_CONFIG_MONITOR,
    TASK_PRIORITY_STRATUM  ,
    TASK_PRIORITY_MINER_TX ,
    TASK_PRIORITY_MINER_RX     //highest priority
};

typedef enum {
    NMAXE         = 0xa0,
    NMAXE_GAMMA ,
    BOARD_UNKNOWN = 0xff,
} board_model_t;


typedef struct{
    String      hostname;
    String      fw_version;
    String      hw_version;
    String      hw_model;
    String      devcie_code;
    uint16_t    vbus;//mV
    uint16_t    ibus;//mA
}board_info_t;

typedef struct{
    float       mcu;
    float       vcore;
    float       asic;
}temp_info_t;


typedef struct{
    bool        is_auto_speed;
    bool        invert_ploarity;
    bool        self_test;
    uint16_t    speed;//%
    uint16_t    rpm;//RPM
}fan_info_t;

typedef struct{
    bool     flip;
    uint16_t brightness;
}screen_info_t;

typedef struct{
    bool   indicator;
}led_info_t;

typedef struct{
    String    model;
    uint16_t  frequency_req;//MHz
    uint16_t  vcore_req;//mV
    uint16_t  vcore_measured;//mV
}asic_info_t;

typedef struct{
    bool           force_config;
    bool           client_connected;
    uint32_t       stratum_update;//ms
    wifi_info_t    wifi; 

    pool_info_t    pool_primary;
    stratum_info_t stratum_primary;

    pool_info_t    pool_fallback;
    stratum_info_t stratum_fallback;

    pool_info_t    pool_use;
    stratum_info_t stratum_use;
}connect_info_t;

typedef struct{
    String   firmware;//name
    bool     ota_running;
    int      progress;
}ota_info_t;

typedef struct{
    double              best_session;
    double              best_ever;
    double              pool;
    double              last;
    double              network;
}diff_info_t;

typedef struct{
    uint32_t            share_rejected;
    uint32_t            share_accepted;
    uint64_t            uptime_ever;
    uint64_t            uptime_session;
    hashrate_t          hashrate;
    uint16_t            block_hits;
    diff_info_t         diff;
    SemaphoreHandle_t   nvs_save_xsem;//save status to NVS signal
    SemaphoreHandle_t   update_xsem;  //miner status update signal
}miner_status_t;

typedef struct{
    fan_info_t       fan;
    screen_info_t    screen;
    led_info_t       led;
}preference_info_t;

typedef String axe_ip_t;

typedef String axe_info_t;

typedef std::map<axe_ip_t, axe_info_t> swarm_map_t;


typedef struct{
    board_info_t        board;
    temp_info_t         temp;
    preference_info_t   preference;
    asic_info_t         asic;
    connect_info_t      connection;
    miner_status_t      mstatus;
    ota_info_t          ota;
    swarm_map_t         swarm;
    String              coin;
    MarketClass         *market;
    NMAxePowerClass     *power;
    StratumClass        *stratum;
    AsicMinerClass      *miner;
}axe_sal_t;


extern axe_sal_t g_nmaxe;
#endif
