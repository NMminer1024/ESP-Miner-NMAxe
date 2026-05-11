#ifndef _GLOBAL_H_
#define _GLOBAL_H_
#include <Arduino.h>
#include "drivers/power/power_hal.h"
#include "drivers/fan/tmp102.h"
#include "stratum/stratum.h"
#include "mining/miner.h"
#include "market/market.h"
#include <deque>
#include <map>
#include <vector>
#include "drivers/touch/ft6206.h"
#include "lvgl.h"
#include <set>


#define HAS_VERSION_CHECK_FEATURE 0 //enable/disable version check feature

#define BOARD_CURRENT_FW_VERSION        "v3.0.12"
#define BOARD_CURRENT_HW_VERSION        "v1.1.1"
#define BOARD_NVS_SAVE_INTERVAL         (60*60)  //second
#define BOARD_TOUCH_LONG_PRESS_TO_RECOVER   (10)     //seconds, long press duration to enter recover mode

#define MINER_WIFI_RSSI_STRONG          (-60)
#define MINER_WIFI_RSSI_GOOD            (-70)
#define MINER_ASIC_ALIVE_TIMEOUT        (1000*60*3)//3 minutes
#define MINER_STRATUM_ALIVE_TIMEOUT     (1000*60*5)//5 minutes
#define MINER_MARKET_UPDATE_INTERVAL    (1000*30)  // ms
#define MINER_MARKET_CONNECT_TIMEOUT    (MINER_MARKET_UPDATE_INTERVAL * 3) // ms
#define MINER_HISTORY_SAMPLE_DEEPTH     (1000*3600*24)  // history depth, how long to keep the history, in seconds
#define MINER_HISTORY_SAMPLE_INTERVAL   (5)             // history sample interval, in seconds
#define MINER_HISTORY_MAX_SIZE          (20000)         // hard cap on deque element count to prevent PSRAM exhaustion
#define MINER_WIFI_CONFIG_TIMEOUT       (60*5)      // seconds
#define MINER_LOG_SUMMARY_INTERVAL      (1000*60*1) // 1 minutes

enum{
    TASK_PRIORITY_APHORISM   = 1, // lowest priority
    TASK_PRIORITY_FAN      , 
    TASK_PRIORITY_CONFIG   ,
    TASK_PRIORITY_APP_TICK ,
    TASK_PRIORITY_DISPLAY  ,
    TASK_PRIORITY_SCAN     ,
    TASK_PRIORITY_SWARM    ,
    TASK_PRIORITY_MARKET   ,
    TASK_PRIORITY_WS       ,
    TASK_PRIORITY_ASIC_CNT ,
    TASK_PRIORITY_ASIC_INIT,
    TASK_PRIORITY_DAEMON   ,
    TASK_PRIORITY_PWR      ,
    TASK_PRIORITY_BTN      ,
    TASK_PRIORITY_TOUCH    ,  
    TASK_PRIORITY_MONITOR  , 
    TASK_PRIORITY_UI       ,
    TASK_PRIORITY_LVGL_DRV ,
    TASK_PRIORITY_LED      ,
    TASK_PRIORITY_WIFI     ,
    TASK_PRIORITY_STRATUM  ,
    TASK_PRIORITY_MINER_TX ,
    TASK_PRIORITY_MINER_RX     //highest priority
};

// init event flags
enum{
    INIT_EVENT_FAN_POLARITY_DETECT   = (1 << 0),   // fan polarity detected, ready for fan thread to start
    INIT_EVENT_FAN_READY             = (1 << 1),   // fan initialized and self-test done
    INIT_EVENT_WIFI_STA_CONNECTED    = (1 << 2),   // wifi sta mode initialized and connected
    INIT_EVENT_WIFI_AP_READY         = (1 << 3),   // wifi ap mode initialized and ready for client connection
    INIT_EVENT_ASIC_COUNTED          = (1 << 4),   // asic counting done
    INIT_EVENT_VBUS_READY            = (1 << 5),   // vbus ready
    INIT_EVENT_VDD_VPLL_READY        = (1 << 6),   // vdd and pll ready
    INIT_EVENT_VCORE_READY           = (1 << 7),   // vcore ready
    INIT_EVENT_SCREEN_READY          = (1 << 8),   // screen initialized and ready
    INIT_EVENT_LVGL_READY            = (1 << 9),   // lvgl display driver ready
    INIT_EVENT_UI_READY              = (1 << 10),   // UI initialized and ready
    INIT_EVENT_MINER_READY           = (1 << 11),  // miner initialized and ready
};

// system event flags
enum{
    SYS_EVENT_MINER_BLOCK_HIT            = (1 << 0),   // miner hit a block
    SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED   = (1 << 1),   // miner achieved a new high difficulty
    SYS_EVENT_MINER_VCORE_TEMP_UPDATE    = (1 << 2),   // miner vcore temperature update, used to trigger backlight effect
    SYS_EVENT_MINER_ASIC_TEMP_UPDATE     = (1 << 3),   // miner asic temperature update, used to trigger backlight effect
    SYS_EVENT_SCREEN_SAVER_TRIGGERED     = (1 << 4),   // screen saver triggered, used to dim the screen
    SYS_EVENT_FIND_NEIGHBOR_TRIGGERED    = (1 << 5),   // Notify to start screen blink when got this event, which indicates where is the neighbor miner
};

typedef struct {
    const char*     name;           // thread name
    TaskFunction_t  entry;          // thread entry function
    uint32_t        stack_size;     // stack size in bytes
    UBaseType_t     priority;       // thread priority
    BaseType_t      core_id;        // core to run the thread on (0 or 1)
    TaskHandle_t*   handle;         // pointer to store the created task handle (can be NULL if not needed)
    uint32_t        delay_ms;       // delay after thread creation in milliseconds
    EventBits_t     wait_events;    // event bits to wait for before creating this thread (0 if no waiting needed)
} thread_config_t;



typedef struct{
    String ssid;
    String pwd;
}wifi_conn_info_t;

typedef struct{
    struct{
        struct{
            IPAddress          ip;   // softAP ip
            wifi_conn_info_t   info; // softAP wifi info
        }ap;
        wifi_conn_info_t       sta;  // station wifi info
    }wifi;

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
        String              coin_price;     // BTC, BCH, XEC, DGB, for price display purpose
        String              coin_watchlist; // comma-separated watchlist e.g. "BTC,ETH,SOL"
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
        uint16_t    vbus;//mV
        uint16_t    ibus;//mA
        uint16_t    vcore;//mV
    }power;

    struct{
        float       vcore;
        float       asic;
    }temp;

    struct{
        String      filename;//name
        bool        running;
        int         progress;
        uint32_t    last_progress_ms;  // millis() when progress last advanced; 0 = not started
        String      error;   // last error string; set on failure, cleared on new upload start
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
        diff_info_t         diff;
        uint32_t            asic_update;  // timestamp of asic respond
        uint32_t            stratum_update;//ms timestamp of last stratum data received
        uint32_t            latency;        // ms, latency to pool
        SemaphoreHandle_t   update_xsem;  // miner status update signal
        std::map<asic_id_t, uint64_t> asic_rsp_counter; // asic respond counter map (internal RAM, fixed small size)
        struct{
            std::deque<history_node_t, PsramAllocator<history_node_t>>      deque; // history of status samples
            SemaphoreHandle_t   mutex;// mutex for status_history concurrent access protection
        }status_history;
        struct{
            std::deque<proximity_node_t, PsramAllocator<proximity_node_t>>  deque; // history of block proximity samples
            SemaphoreHandle_t   mutex;// mutex for block_proximity_history concurrent access protection
        }proximity_history;
    }miner;

    struct{
        struct{
            struct{  // loading page status
                float       percent; // loading percent
                struct{
                    String   msg;    // loading message
                    uint32_t color;  // message color, RGB format
                }details;
            }loading; 

            struct{     // wifi/config page status
                uint16_t    timeout; // in seconds
                String      message; // connection info message
            }config; 

            struct{     // counterdown page status
                uint8_t    timeout; // in seconds
            }countdown; 

            std::vector<lv_obj_t*>  list;
            int8_t                  current;     // current ui page index
            int8_t                  last;        //last ui page index, restored on next boot
            SemaphoreHandle_t       save_xsem;   // save current page index
        }page;

        struct {
            SemaphoreHandle_t   drv_xMutex;
        }lvgl;

        uint32_t last_active_ms; // last active timestamp in milliseconds, used for screen saver
    }ui;

    struct{
        uint8_t             evt;  // touch event type
    }touch;

    struct{
        IPAddress   ip;
        IPAddress   gateway;
        IPAddress   subnet;
        IPAddress   dns;
        int         rssi;
        wl_status_t status;
        uint16_t    config_timeout;
        bool                  force_config_required;
        bool                  client_connected;
        SemaphoreHandle_t     reconnect_xsem;
    }wifi;

    struct{
        std::vector<fan_status_t>          list;   // support multiple fans
    }fan;

    struct{
        screen_preference_info_t    screen;
        led_preference_info_t       led;
    }preference;

    struct {
        SemaphoreHandle_t mutex;            // 保护聚合字段和黑名单
        uint32_t          total_workers;
        float             total_hr;
        float             best_session_bd;
        float             best_ever_bd;
        std::set<String>          confirmed_ips;    // 已确认是 NMMiner 的 IP，跨 generation 保留；连续 probe 失败 N 轮才移除
        std::set<String>          probe_blacklist;  // 非 NMMiner IP，本轮 scan 周期内跳过；generation 切换清空
        std::set<String>          gossip_union;     // 从 confirmed_ips 邻居 /alive 收集的补充 IP 池，跨 generation 保留
        std::map<String, uint8_t> probe_fail_cnt;   // 每个 confirmed_ip 的连续 probe 失败次数；满 3 次才下线
        uint32_t                  last_scan_gen;    // 上次处理的 scan_generation，仅用于触发 blacklist 清空
    }swarm;

    struct {
        std::vector<String> alive_ips;       // ICMP 存活 IP 列表
        SemaphoreHandle_t   mutex;
        SemaphoreHandle_t   scan_required;   // 前端刷新时释放，触发新一轮扫描；超时后自动重扫
        uint32_t            last_scan_ms;
        uint32_t            scan_generation; // 每完成一轮完整扫描 +1，通知 swarm 重置记忆
        bool                is_scanning;     // 当前是否正在扫描（供 /alive 接口使用）
        uint16_t            scan_progress;   // 当前扫描进度 0~254
    }neighbor;

    struct { 
        struct Quote { 
            String quote; 
            String author; 
            String keyword;
        };
        SemaphoreHandle_t   mutex;
        std::vector<Quote> pool;
    }aphorism;

    SemaphoreHandle_t                  reboot_xsem;             // reboot signal
    SemaphoreHandle_t                  nvs_save_xsem;           // save status to NVS signal
    SemaphoreHandle_t                  brightness_update_xsem;  // screen brightness update signal
    SemaphoreHandle_t                  recover_factory_xsem;    // recover factory settings signal
    SemaphoreHandle_t                  force_config_xsem;       // force enter config mode signal
    EventGroupHandle_t                 init_evt;                // system initialization event group
    EventGroupHandle_t                 sys_evt;                 // system event group
}board_status_t;


typedef struct{
    board_info_t        info;
    board_status_t      status;
    FT6206Class         *touch;
    MarketClass         *market;
    AxePowerHal         *power;
    StratumClass        *stratum;
    AsicMinerClass      *miner;
}board_sal_t;


extern board_sal_t g_board;
#endif
