#ifndef _GLOBAL_H_
#define _GLOBAL_H_
#include <Arduino.h>
#include "power.h"
#include "connection.h"
#include "tmp102.h"
#include "stratum.h"
#include "miner.h"
#include "market.h"
#include <deque>
#include <map>
#include <vector>

#define HAS_VERSION_CHECK_FEATURE 0 //enable/disable version check feature

#define CURRENT_FW_VERSION      "v3.0.10"

#define CURRENT_HW_VERSION      "v1.1.1"

#define NVS_SAVE_INTERVAL       (60*60)  //second

#define WIFI_RSSI_STRONG        (-60)

#define WIFI_RSSI_GOOD          (-70)

#define ASIC_ALIVE_TIMEOUT      (1000*60*3)//3 minutes

#define STRATUM_ALIVE_TIMEOUT   (1000*60*3)//3 minutes

#define MARKET_UPDATE_INTERVAL  (1000*5)  // ms

#define MARKET_TIMEOUT          (MARKET_UPDATE_INTERVAL * 3) // ms

#define BOARD_LOW_POWER         (10.0f)   //Watt

#define BOARD_MCU_DANGER        (60.0f)

#define HISTORY_DEEPTH          (1000*3600*24) // history depth, how long to keep the history, in seconds

#define HISTORY_SAMPLE_INTERVAL (2) // history sample interval, in seconds

#define SWARM_UDP_BOARDCAST_ADDR    IPAddress(255,255,255,255)

#define SWARM_UDP_BOARDCAST_PORT    (12345)

#define SWARM_OFFLINE_TIMEOUT       (3*60*1000) //3min

#define WIFI_CONFIG_MODE_TIMEOUT    (60*5) //seconds

enum{
    TASK_PRIORITY_FAN      = 1, // lowest priority
    TASK_PRIORITY_SWARM    ,
    TASK_PRIORITY_PWR      ,
    TASK_PRIORITY_ASIC_INIT,
    TASK_PRIORITY_BTN      ,
    TASK_PRIORITY_LED      ,
    TASK_PRIORITY_UI       ,
    TASK_PRIORITY_LVGL_DRV ,
    TASK_PRIORITY_DAEMON   ,
    TASK_PRIORITY_WS       ,
    TASK_PRIORITY_MONITOR  ,
    TASK_PRIORITY_WIFI     ,
    TASK_PRIORITY_WEB_SERVER,
    TASK_PRIORITY_MARKET   ,
    TASK_PRIORITY_CONFIG_MONITOR,
    TASK_PRIORITY_STRATUM  ,
    TASK_PRIORITY_MINER_TX ,
    TASK_PRIORITY_MINER_RX     //highest priority
};

typedef struct{
    bool        is_auto_speed;
    float       target_temp;  //asic temp
}fan_info_t;

typedef struct{
    bool     flip;
    bool     auto_rolling;// auto rolling screen
    uint16_t brightness;
}screen_info_t;

typedef struct{
    bool   enable;
    bool   sleep;
    bool   sleep_last;
}led_info_t;

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
    double              best_session;
    double              best_ever;
    double              pool;
    double              last;
    double              network;
}diff_info_t;

typedef struct{
    fan_info_t       fan;
    screen_info_t    screen;
    led_info_t       led;
}preference_info_t;

typedef struct{
    struct{
        String              coin_price;//  BTC, BCH, XEC, DGB, for price display purpose
        String              hostname;
        String              fw_version;
        String              hw_version;
        String              hw_model;
        String              fw_latest_release;
        String              devcie_code;
    }base;
    BoardSpecConfig         spec;// board spec config, including asic , pinout, display, power etc.
    connect_info_t          connection;
    preference_info_t       preference;
}board_info_t;



typedef String miner_ip_t;
typedef String miner_info_t;
typedef struct{
    struct{
        uint16_t    vbus;//mV
        uint16_t    ibus;//mA
        uint16_t    vcore;//mV
    }power;

    struct{
        float       mcu;
        float       vcore;
        float       asic;
    }temp;

    struct{
        String      filename;//name
        bool        running;
        int         progress;
    }ota;

    struct{
        uint64_t    utc; // UTC timestamp in seconds since epoch
        String      tz;  // timezone string, e.g. "8.0" for UTC+8
    }time;

    struct{
        uint32_t            share_rejected;
        uint32_t            share_accepted;
        uint64_t            uptime_ever;
        uint64_t            uptime_session;
        hashrate_t          hashrate;
        float               efficiency; // J/TH
        uint16_t            hits;
        uint16_t            last_hits;//record the last hits
        diff_info_t         diff;
        uint32_t            asic_update;  // timestamp of asic respond
        std::deque<history_node_t, PsramAllocator<history_node_t>> status_history;// history of status samples
        std::deque<proximity_node_t> block_proximity_history; // history of block proximity (use internal RAM)
        SemaphoreHandle_t   history_mutex;// mutex for status_history concurrent access protection
        SemaphoreHandle_t   block_proximity_mutex;// mutex for block_proximity_history concurrent access protection
        SemaphoreHandle_t   update_xsem;  // miner status update signal
    }miner;

    struct{
        SemaphoreHandle_t   page_save_xsem; // save current page index
        uint8_t             last_page;      //last ui page index, restored on next boot
    }ui;
    
    std::vector<fan_status_t>          fans;            // support multiple fans
    std::map<miner_ip_t, miner_info_t> swarm;           // swarm miners info map
    SemaphoreHandle_t                  reboot_xsem;     // reboot signal
    SemaphoreHandle_t                  nvs_save_xsem;   // save status to NVS signal
    SemaphoreHandle_t                  brightness_update_xsem; // screen brightness update signal
}board_status_t;


typedef struct{
    board_info_t        info;
    board_status_t      status;
    MarketClass         *market;
    AxePowerClass       *power;
    StratumClass        *stratum;
    AsicMinerClass      *miner;
}board_sal_t;


extern board_sal_t g_board;
#endif
