#ifndef _GLOBAL_H
#define _GLOBAL_H
#include <Arduino.h>
#include "board.h"
#include "nm_pwr.h"
#include "connection.h"
#include "tmp102.h"
#include "stratum.h"
#include "miner.h"
#include "market.h"
#include <deque>
#include <map>

#define HAS_VERSION_CHECK_FEATURE 0 //enable/disable version check feature

#define CURRENT_FW_VERSION      "v2.9.11"

#define CURRENT_HW_VERSION      "v1.1.1"

#define NVS_SAVE_INTERVAL       (60*60)  //second

#define WIFI_RSSI_STRONG        (-60)

#define WIFI_RSSI_GOOD          (-70)

#define ASIC_ALIVE_TIMEOUT      (1000*60*3)//3 minutes

#define STRATUM_ALIVE_TIMEOUT   (1000*60*3)//3 minutes

#define MARKET_UPDATE_INTERVAL  (1000*5)  // ms

#define MARKET_TIMEOUT          (MARKET_UPDATE_INTERVAL * 3) // ms

#define BOARD_LOW_POWER         (10.0f)   //Watt

#define ASIC_TEMP_DANGER        (75.0f)

#define ASIC_TEMP_NORMAL        (50.0f)

#define VCORE_TEMP_DANGER       (90.0f)

#define VCORE_TEMP_LOW          (50.0f)

#define BOARD_MCU_DANGER        (60.0f)

#define HISTORY_DEEPTH          (1000*3600*24) // history depth, how long to keep the history, in seconds

#define HISTORY_SAMPLE_INTERVAL (2) // history sample interval, in seconds


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
    TASK_PRIORITY_MARKET   ,
    TASK_PRIORITY_CONFIG_MONITOR,
    TASK_PRIORITY_STRATUM  ,
    TASK_PRIORITY_MINER_TX ,
    TASK_PRIORITY_MINER_RX     //highest priority
};

typedef struct{
    float       mcu;
    float       vcore;
    float       asic;
}temp_info_t;


typedef struct{
    bool        is_auto_speed;
    bool        self_test;
    uint16_t    speed;//%
    uint16_t    rpm;//RPM
    float       target_temp;  //asic temp
}fan_info_t;

typedef struct{
    bool     flip;
    bool     auto_screen;
    uint16_t brightness;
    uint16_t brightness_last;
}screen_info_t;

typedef struct{
    bool   enable;
    bool   sleep;
    bool   sleep_last;
}led_info_t;

typedef struct{
    String    model;
    uint16_t  frequency_req;//MHz
    uint16_t  vcore_req;//mV
    uint16_t  vcore_measured;//mV
    uint16_t  job_frq_ms;//ms
    uint16_t  vcore_min;//mV
    uint16_t  vcore_max;//mV
    uint16_t  diff_thr_init; //difficulty threshold
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
    String              filename;//name
    bool                running;
    int                 progress;
}ota_info_t;

typedef struct{
    double              best_session;
    double              best_ever;
    double              pool;
    double              last;
    double              network;
}diff_info_t;


typedef struct{
    uint32_t             times;          // count of hashrate samples
    uint32_t             dura;           // duration of hashrate samples, seconds
    uint16_t             max_x_hr;       // max x axis in hashrate distribution
    uint16_t             max_x_bars;     // count of max x axis
    std::map<uint16_t, uint8_t> dist_map;//<x, y> x:scale_x, y:percentage of hashrate in this scale, range from 0 to 100
}hash_dist_t;

// ["hashRate","temp","vrTemp","power","voltage","current","coreVoltageActual","fanspeed","fanrpm","wifiRSSI","freeram","freepsram","timestamp"],
typedef struct{
    String         hashrate;      // hashrate, GH/s
    String         asic_temp;     // asic temperature, C
    String         vcore_temp;    // vcore temperature, C
    String         pbus;          // power, W
    String         vbus;          // voltage, V
    String         ibus;          // current, A
    uint16_t       vcore;         // vcore measured, mV
    uint16_t       fanspeed;      // fan speed, %
    uint16_t       fanrpm;        // fan rpm, RPM
    int8_t         wifi_rssi;     // wifi rssi, dBm
    uint32_t       free_ram;      // free ram, Kbytes
    uint32_t       free_psram;    // free psram, Kbytes
    uint64_t       epoch;         // timestamp, milliseconds since epoch
}history_node_t;

typedef struct{
    float           block_proximity; // block share, percentage 0-100%
    float           share_diff;      // share difficulty
    float           net_diff;        // network difficulty
    uint64_t        epoch;           // timestamp, milliseconds since epoch
}proximity_node_t;

typedef struct{
    uint64_t            utc;        // UTC timestamp in seconds since epoch
    String              timezone;
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
}miner_status_t;

typedef struct{
    fan_info_t       fan;
    screen_info_t    screen;
    led_info_t       led;
}preference_info_t;

typedef struct{
    struct{
        String              coin;//mining coin, BTC, BCH, XEC, DGB, for price display purpose
        String              hostname;
        String              fw_version;
        String              fw_latest_release;
        String              hw_version;
        String              hw_model;
        String              devcie_code;
    }base;

    struct{
        uint8_t             torch_pin; // fan tachometer pin
        uint8_t             pwm_pin;   // fan pwm control pin
        uint16_t            self_test_rpm_thr; // RPM, minimum RPM when fan is at full speed in self-test
    }fan_spec;

    struct{
        uint8_t             user_pin; // user button as recover to factory default
        uint8_t             boot_pin; // boot button as UI page switch
    }btn_spec;

    connect_info_t      connection;
    preference_info_t   preference;
}board_info_t;

typedef struct{
    uint16_t            vbus;//mV
    uint16_t            ibus;//mA
    temp_info_t         temp;
    asic_info_t         asic;
    miner_status_t      miner;
    ota_info_t          ota;
    SemaphoreHandle_t   reboot_xsem;
    SemaphoreHandle_t   nvs_save_xsem;// save status to NVS signal
}board_status_t;

typedef struct{
    hash_dist_t         hr_dist_page;
}board_ui_spec_t;


typedef String axe_ip_t;

typedef String axe_info_t;

typedef std::map<axe_ip_t, axe_info_t> swarm_map_t;


typedef struct{
    board_info_t        info;
    board_status_t      status;
    board_ui_spec_t     ui;
    swarm_map_t         swarm;
    MarketClass         *market;
    NMAxePowerClass     *power;
    StratumClass        *stratum;
    AsicMinerClass      *miner;
}board_sal_t;


extern board_sal_t g_board;
#endif
