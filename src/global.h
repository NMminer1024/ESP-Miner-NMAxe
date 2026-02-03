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
#include "ft6206.h"

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

#define BOARD_MCU_DANGER        (70.0f)

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
    TASK_PRIORITY_TOUCH    ,   
    TASK_PRIORITY_DISPLAY  ,
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
    bool           force_config;
    bool           client_connected;
    wifi_info_t    wifi; 
    
    struct{
        pool_info_t use;
        pool_info_t primary;
        pool_info_t fallback;
    }pool;

    struct{
        stratum_info_t  use;
        stratum_info_t  primary;
        stratum_info_t  fallback;
    }stratum;
}connect_info_t;

typedef struct{
    double              best_session;
    double              best_ever;
    double              pool;
    double              last;
    double              network;
}diff_info_t;

typedef struct{
    struct{
        String              coin_price;//  BTC, BCH, XEC, DGB, for price display purpose
        String              hostname;
        String              fw_version;
        String              hw_version;
        String              fw_latest_release;
        String              devcie_code;
    }base;
    BoardSpecConfig         spec;// board spec config, including asic , pinout, display, power etc.
    connect_info_t          connection;
}board_info_t;



typedef String miner_ip_t;
typedef String miner_info_t;
typedef uint16_t asic_id_t;
typedef struct{
    struct{
        float       percent; // loading percent
        struct{
            String   msg;    // loading message
            uint32_t color;  // message color, RGB format
        }details;
    }loading; // loading page status

    struct{
        uint16_t    timeout; // in seconds
        String      message; // connection info message
    }config; // wifi/config page status

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
        struct{
            uint16_t    time;
            String      date;
        }format;
        uint64_t    utc;    // UTC timestamp in seconds since epoch
        String      tz;     // timezone string, e.g. "8.0" for UTC+8
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
        uint32_t            stratum_update;//ms timestamp of last stratum data received
        std::deque<history_node_t, PsramAllocator<history_node_t>> status_history;// history of status samples
        std::deque<proximity_node_t> block_proximity_history; // history of block proximity (use internal RAM)
        std::map<asic_id_t, uint64_t> asic_rsp_counter; // asic respond counter map
        SemaphoreHandle_t   history_mutex;// mutex for status_history concurrent access protection
        SemaphoreHandle_t   block_proximity_mutex;// mutex for block_proximity_history concurrent access protection
        SemaphoreHandle_t   update_xsem;  // miner status update signal
    }miner;

    struct{
        struct{
            uint8_t             current;     // current ui page index
            uint8_t             last;        //last ui page index, restored on next boot
            SemaphoreHandle_t   save_xsem;   // save current page index
        }page;
        
        struct{
            SemaphoreHandle_t   xsem; // touch event signal
            uint8_t             evt;  // touch event type
        }touch;

    }ui;

    struct{
        std::map<miner_ip_t, miner_info_t> map;           // swarm miners info map
        uint16_t                           total_workers; // total workers in swarm
        float                              total_hr;      // total hash rate in swarm
        float                              best_diff;     // best diff in swarm
    }swarm;

    struct{
        std::vector<fan_status_t>          list;   // support multiple fans
        uint8_t                            count;  // number of fans      
    }fan;

    struct{
        fan_preference_info_t       fan;
        screen_preference_info_t    screen;
        led_preference_info_t       led;
        asic_preference_info_t      asic;
    }preference;

    SemaphoreHandle_t                  reboot_xsem;     // reboot signal
    SemaphoreHandle_t                  nvs_save_xsem;   // save status to NVS signal
    SemaphoreHandle_t                  brightness_update_xsem; // screen brightness update signal
}board_status_t;


typedef struct{
    board_info_t        info;
    board_status_t      status;
    FT6206Class         *touch;
    MarketClass         *market;
    AxePowerClass       *power;
    StratumClass        *stratum;
    AsicMinerClass      *miner;
}board_sal_t;


extern board_sal_t g_board;
#endif
