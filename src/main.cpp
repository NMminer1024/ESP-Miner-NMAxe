#include <WiFi.h>
#include "global.h"
#include "logger.h"
#include "display.h"
#include "market.h"
#include "http_server.h"
#include "nvs_config.h"
#include "github.h"
#include "thread_entry.h"

TaskHandle_t fanTask, ledTask, btnTask, touchTask, wsTask, monitorTask, swarmTask, marketTask, daemonTask, stratumTask, minerTxTask, minerRxTask, powerTask, lvglTask, uiTask;
board_sal_t  g_board;

bool board_init(IN BoardSpecConfig config, OUT board_sal_t *board){
    /*************************************************** Specific parameters among different board ***************************************/
    board->info.spec.name                           = config.name;
    board->info.spec.asic                           = config.asic;
    board->info.spec.tft                            = config.tft;
    board->info.spec.spi                            = config.spi;
    board->info.spec.ui                             = config.ui;
    board->info.spec.fans                           = config.fans;
    board->info.spec.btn                            = config.btn;  
    board->info.spec.pwr                            = config.pwr;
    board->info.spec.led                            = config.led;
    board->info.spec.iic                            = config.iic;
    board->info.spec.create_asic_instance           = config.create_asic_instance;
    board->info.spec.create_power_instance          = config.create_power_instance;
    board->info.spec.preference                     = config.preference;
    /*************************************************** Same parameters among different board ***************************************/
    String stratum_pri                              = String(nvs_config_get_string(NVS_CONFIG_STRATUM_URL_PRIMARY,  PRIMARY_POOL_URL));
    String stratum_fb                               = String(nvs_config_get_string(NVS_CONFIG_STRATUM_URL_FALLBACK, FALLBACK_POOL_URL));
    
    board->info.connection.pool.primary.ssl         = ((stratum_pri.indexOf("ssl") != -1) || (stratum_pri.indexOf("tls") != -1));
    board->info.connection.pool.primary.url         = stratum_pri.substring(stratum_pri.indexOf(":") + 3, stratum_pri.lastIndexOf(":"));
    board->info.connection.pool.primary.port        = stratum_pri.substring(stratum_pri.lastIndexOf(":") + 1, stratum_pri.length()).toInt();
    board->info.connection.pool.fallback.ssl        = ((stratum_fb.indexOf("ssl") != -1) || (stratum_fb.indexOf("tls") != -1));
    board->info.connection.pool.fallback.url        = stratum_fb.substring(stratum_fb.indexOf(":") + 3, stratum_fb.lastIndexOf(":"));
    board->info.connection.pool.fallback.port       = stratum_fb.substring(stratum_fb.lastIndexOf(":") + 1, stratum_fb.length()).toInt();
    board->info.connection.pool.use                 = board->info.connection.pool.primary;
    board->info.base.fw_version                     = BOARD_CURRENT_FW_VERSION;
    board->info.base.hw_version                     = BOARD_CURRENT_HW_VERSION;
    board->info.base.devcie_code                    = gen_device_code();
    board->info.base.fw_latest_release              = "";
    board->info.connection.stratum.primary.user     = String(nvs_config_get_string(NVS_CONFIG_STRATUM_USER_PRIMARY, (String(PRIMARY_USER) + "." + board->info.spec.name + "_" + board->info.base.devcie_code.substring(0, 5)).c_str()));
    board->info.connection.stratum.primary.pwd      = String(nvs_config_get_string(NVS_CONFIG_STRATUM_PASS_PRIMARY, PRIMARY_POOL_PWD));
    board->info.connection.stratum.fallback.user    = String(nvs_config_get_string(NVS_CONFIG_STRATUM_USER_FALLBACK, (String(FALLBACK_USER) + "." + board->info.spec.name + "_" + board->info.base.devcie_code.substring(0, 5)).c_str()));
    board->info.connection.stratum.fallback.pwd     = String(nvs_config_get_string(NVS_CONFIG_STRATUM_PASS_FALLBACK, FALLBACK_POOL_PWD));
    board->info.connection.stratum.use              = board->info.connection.stratum.primary;
    board->status.wifi.reconnect_xsem               = xSemaphoreCreateCounting(1, 0);

    board->info.connection.wifi.ap.ip               = IPAddress(192, 168, 4, 1);
    board->info.connection.wifi.ap.info.pwd         = "12345678";
    board->info.connection.wifi.ap.info.ssid        = String(nvs_config_get_string(NVS_CONFIG_AP_SSID, (board->info.spec.name + "_" + board->info.base.devcie_code.substring(0, 5)).c_str())); 

    board->status.wifi.force_config_required                   = nvs_config_get_u8(NVS_CONFIG_FORCE_CONFIG, false);
    board->status.wifi.client_connected               = false;
    board->info.connection.wifi.sta.ssid              = String(nvs_config_get_string(NVS_CONFIG_WIFI_SSID, "NMTech-2.4G"));
    board->info.connection.wifi.sta.pwd               = String(nvs_config_get_string(NVS_CONFIG_WIFI_PASS, "NMMiner2048"));
    board->info.base.hostname                         = String(nvs_config_get_string(NVS_CONFIG_HOSTNAME, board->info.connection.wifi.ap.info.ssid.c_str()));
    board->status.miner.stratum_update                = millis();
    board->status.preference.fan.is_auto_speed        = nvs_config_get_u16(NVS_CONFIG_AUTO_FAN_SPEED, board->info.spec.preference.fan.is_auto_speed);
    board->status.preference.fan.target_temp          = String(nvs_config_get_string(NVS_CONFIG_ASIC_TARGET_TEMP, String(board->info.spec.preference.asic.target_temp).c_str())).toFloat();
    board->status.preference.screen.flip              = nvs_config_get_u8(NVS_CONFIG_FLIP_SCREEN, board->info.spec.preference.screen.flip);
    board->status.preference.screen.auto_rolling      = nvs_config_get_u8(NVS_CONFIG_AUTO_SCREEN, board->info.spec.preference.screen.auto_rolling);
    board->status.preference.screen.brightness        = nvs_config_get_u8(NVS_CONFIG_SCREEN_BRIGHTNESS, board->info.spec.preference.screen.brightness);
    board->status.preference.led.enable               = nvs_config_get_u8(NVS_CONFIG_LED_INDICATOR, board->info.spec.preference.led.enable);
    board->status.preference.led.sleep                = false;
    board->status.preference.led.sleep_last           = board->status.preference.led.sleep;
    board->info.base.coin_price                     = String(nvs_config_get_string(NVS_CONFIG_PRICE_DISPLAY_COIN, "BTC"));
    board->info.base.coin_price.toUpperCase();
    
    board->status.reboot_xsem                       = xSemaphoreCreateCounting(1, 0);
    board->status.nvs_save_xsem                     = xSemaphoreCreateCounting(1, 0);
    board->status.brightness_update_xsem            = xSemaphoreCreateCounting(1, 0);
    board->status.recover_factory_xsem              = xSemaphoreCreateCounting(1, 0);
    board->status.force_config_xsem                 = xSemaphoreCreateCounting(1, 0);
    board->status.init_evt                          = xEventGroupCreate();
    board->status.sys_evt                           = xEventGroupCreate();

    board->status.miner.history_mutex               = xSemaphoreCreateMutex();
    board->status.miner.block_proximity_mutex       = xSemaphoreCreateMutex();
    board->status.miner.update_xsem                 = xSemaphoreCreateCounting(1, 0);
    board->status.miner.hits                        = nvs_config_get_u16(NVS_CONFIG_BLOCK_HITS, 0);
    board->status.miner.last_hits                   = board->status.miner.hits;
    board->status.ota.running                       = false;
    board->status.ota.progress                      = 0;
    board->status.ota.filename                      = "";
    board->status.miner.diff.best_ever              = strtoull(nvs_config_get_string(NVS_CONFIG_BEST_EVER, "0"), NULL, 10);
    board->status.ui.page.countdown.timeout         = BOARD_TOUCH_LONG_PRESS_TO_RECOVER;
    board->status.ui.page.last                      = nvs_config_get_u8(NVS_CONFIG_UI_LAST_PAGE, UI_PAGE_MINER);
    board->status.ui.page.current                   = UI_PAGE_LOADING;
    board->status.ui.page.save_xsem                 = xSemaphoreCreateCounting(1, 0);
    board->status.ui.page.list                      = {NULL, NULL, NULL, NULL, NULL, NULL, NULL};
    board->status.ui.lvgl.drv_xMutex                = xSemaphoreCreateMutex();
    board->status.touch.evt                         = TOUCH_NONE_EVT;
    board->status.miner.uptime_ever                 = nvs_config_get_u64(NVS_CONFIG_UPTIME, 0);
    board->status.time.tz                           = String(nvs_config_get_string(NVS_CONFIG_TIMEZONE, "8.0"));
    board->status.time.format.time                  = nvs_config_get_u8(NVS_CONFIG_TIME_FORMAT, 24);
    board->status.time.format.date                  = nvs_config_get_string(NVS_CONFIG_DATE_FORMAT, "YYYY/MM/DD");
    
    // initialize fan statuses
    for(uint8_t i = 0; i < board->info.spec.fans.size(); i++){
        fan_status_t    state;
        state.id        = i;
        state.self_test = false;
        state.speed     = nvs_config_get_u16(NVS_CONFIG_FAN_SPEED, 100);
        state.rpm       = 0;
        board->status.fan.list.push_back(state);
    }
    board->status.fan.count = board->info.spec.fans.size();

    // create touch instance
    board->touch                                   = new FT6206Class();
    if(board->touch == NULL){
        LOG_E("FT6206Class instance creation failed");
        return false;
    }
    // create ASIC instance
    BMxxx* asic_instance                            = board->info.spec.create_asic_instance(*config.asic.com_port, config.asic.com_baud_init, config.asic.rx_pin, config.asic.tx_pin, config.asic.rst_pin);
    if(asic_instance == NULL){
        LOG_E("BMxxx instance creation failed");
        return false;
    }
    // create AsicMinerClass instance
    board->miner                                    = new AsicMinerClass(asic_instance);
    if(board->miner == NULL){
        LOG_E("AsicMinerClass instance creation failed");
        return false;
    }
    // create Power HAL instance
    board->power                                    = board->info.spec.create_power_instance(config.pwr.en_pins, config.pwr.adc_pins, config.pwr.vcore_regulator_pin, config.pwr.pgood_pin, config.pwr.dc_plug_pin);
    if(board->power == NULL){
        LOG_E("AxePower instance creation failed");
        return false;
    }
    // create market instance
    board->market                                   = new MarketClass();
    if(board->market == NULL){
        LOG_E("MarketClass instance creation failed");
        return false;
    }
    // create Stratum instance
    board->stratum                                  = new StratumClass(board->info.connection.pool.use, board->info.connection.stratum.use, 10);
    if(board->stratum == NULL){
        LOG_E("StratumClass instance creation failed");
        return false;
    }

    delay(2000);
    log_w("\r\n            ___          ___         ");
    log_w("\r\n           /\\__\\        /\\__\\    ");
    log_w("\r\n          /::|  |      /::|  |       ");
    log_w("\r\n         /:|:|  |     /:|:|  |       ");
    log_w("\r\n        /:/|:|  |__  /:/|:|__|__     ");
    log_w("\r\n       /:/ |:| /\\__\\/:/ |::::\\__\\");
    log_w("\r\n       \\/__|:|/:/  /\\/__/~~/:/  /  ");
    log_w("\r\n           |:/:/  /       /:/  /     ");
    log_w("\r\n           |::/  /       /:/  /      ");
    log_w("\r\n           /:/  /       /:/  /       ");
    log_w("\r\n           \\/__/        \\/__/      \r\n");
    log_w("         %s - %s\r\n", board->info.spec.name.c_str(), board->info.base.fw_version.c_str());
    return true;
}


/**********************************************************Thread Pool ***************************************************************/
static const thread_config_t thread_pool[] = {
    {"(display)",   display_thread_entry,           1024*5,   TASK_PRIORITY_DISPLAY,    1, NULL,          10,  0},
    {"(lvgl)",      lvgl_tick_thread_entry,         1024*5,   TASK_PRIORITY_LVGL_DRV,   1, &lvglTask,     10,  0},
    {"(ui)",        ui_thread_entry,                1024*5,   TASK_PRIORITY_UI,         1, &uiTask,       10,  0},
    {"(touch)",     touch_thread_entry,             1024*4,   TASK_PRIORITY_TOUCH,      0, &touchTask,    10,  0},
    {"(led)",       led_thread_entry,               1024*3,   TASK_PRIORITY_LED,        1, &ledTask,      10,  0},
    {"(button)",    button_thread_entry,            1024*3,   TASK_PRIORITY_BTN,        1, &btnTask,      10,  0},
    {"(webserver)", webserver_thread_entry,         1024*5,   TASK_PRIORITY_WS,         1, &wsTask,       10,  0},
    {"(wifi)",      wifi_connect_thread_entry,      1024*6,   TASK_PRIORITY_WIFI,       1, NULL,          10,  0},
    {"(daemon)",    daemon_thread_entry,            1024*3,   TASK_PRIORITY_DAEMON,     0, &daemonTask,   10,  0},
    {"(power)",     power_thread_entry,             1024*7,   TASK_PRIORITY_PWR,        1, &powerTask,    10,  0},
    {"(asic_cnt)",  miner_asic_count_thread_entry,  1024*5,   TASK_PRIORITY_ASIC_CNT,   1, NULL,          10,  0},
    {"(asic_init)", miner_asic_init_thread_entry,   1024*5,   TASK_PRIORITY_ASIC_INIT,  1, NULL,          10,  0},
    {"(fan)",       fan_thread_entry,               1024*5,   TASK_PRIORITY_FAN,        0, &fanTask,      10,  0},
    {"",            NULL,                           0,        0,                        0, NULL,          0,   INIT_EVENT_ASIC_COUNTED | INIT_EVENT_WIFI_STA_CONNECTED | INIT_EVENT_FAN_READY},
    {"(swarm)",     swarm_thread_entry,             1024*7,   TASK_PRIORITY_SWARM,      0, &swarmTask,    10,  0},
    {"(market)",    market_thread_entry,            1024*6,   TASK_PRIORITY_MARKET,     0, &marketTask,   10,  0},
    {"(stratum)",   stratum_thread_entry,           1024*11,  TASK_PRIORITY_STRATUM,    1, &stratumTask,  10,  0},
    {"(monitor)",   monitor_thread_entry,           1024*4,   TASK_PRIORITY_MONITOR,    1, &monitorTask,  10,  0},
    {"(asic_tx)",   miner_asic_tx_thread_entry,     1024*5,   TASK_PRIORITY_MINER_TX,   1, &minerTxTask,  10,  0},
    {"(asic_rx)",   miner_asic_rx_thread_entry,     1024*5,   TASK_PRIORITY_MINER_RX,   0, &minerRxTask,  10,  0},
};


void setup() {
    BoardSpecConfig config;
    BoardModelType  model;
    /************************************************************ GET BOARD CONFIG *******************************************************/
    model  = get_board_model();
    config = get_board_config(model);
    /************************************************************ INIT SERIAL AND NVS ****************************************************/
    hardware_pre_init(config);
    /******************************************************* INIT BOARD BASED ON CONFIG  *************************************************/
    while(!board_init(config, &g_board)){
        LOG_E("Board initialization failed, retrying in 1s...");
        delay(1000);
    }
    //disable usb uart to fit for typeA port PD , such as Apple divider 3/BC1.2 SDP/CDP/DCP protocol
    if(!g_board.power->is_dc_pluged()) disable_usb_uart();
    /********************************************************* CREATE ALL THREADS ********************************************************/
    for(auto &thread : thread_pool){
        if(thread.entry != NULL){
            // create thread if entry function is defined
            BaseType_t ret = xTaskCreatePinnedToCore(thread.entry, thread.name, thread.stack_size, (void*)(&g_board), thread.priority, thread.handle, thread.core_id);

            if(ret == pdPASS)  LOG_I("Thread %s created on core %d", thread.name, thread.core_id);
            else LOG_E("Failed to create thread %s", thread.name);
            delay(thread.delay_ms);
        }else {
            // if entry function is NULL, treat this config as a synchronization point and wait for specified events before next proceeding
            xEventGroupWaitBits(g_board.status.init_evt, thread.wait_events, pdFALSE, pdTRUE, portMAX_DELAY);
            continue;
        }
    }
}


void loop() {
  uint32_t last = millis();
  while(millis() - last < 1000*1){
      static uint16_t brightness = g_board.status.preference.screen.brightness;   
      static float    x = 0;
      if(g_board.status.miner.last_hits != g_board.status.miner.hits){//screen blink if block hit
          brightness = 100*(1 + sin(x))/2;
          x+=0.1;
          tft_bl_ctrl(brightness);
      } else brightness = g_board.status.preference.screen.brightness;
      delay(10);
  }
  delay(100);


#if 0
  // for testing only: simulate block hits every 20s
  static uint32_t cnt = 1;
  if(cnt++ % 20 == 0){
    g_board.status.miner.hits++;
  }
#endif



#if 0
static uint32_t start = millis();
if(millis() - start > 1000*2){ 
    start = millis();
    LOG_W("=========== Stack High Water Mark (in bytes) ===========");
    LOG_I("+-----------------+----------+------------+------------+");
    LOG_I("| Task Name       | HWM      | Total Stack| Optimizable|");
    LOG_I("+-----------------+----------+------------+------------+");
    
    for(auto &thread : thread_pool){
        if(thread.handle != NULL && *thread.handle != NULL){
            eTaskState taskState = eTaskGetState(*thread.handle);
            if(taskState == eDeleted) continue;

            char *taskName = pcTaskGetName(*thread.handle);
            if(taskName != NULL && taskName[0] != '\0'){
                UBaseType_t highWaterMark = uxTaskGetStackHighWaterMark(*thread.handle);
                uint32_t totalStack = thread.stack_size; // get total stack size from config
                uint32_t used = totalStack - highWaterMark;
                uint32_t optimizable = highWaterMark > 512 ? highWaterMark - 512 : 0; // keep 512 bytes safety buffer
                
                LOG_I("| %-15s | %8u | %10u | %10u |", 
                      taskName, highWaterMark, totalStack, optimizable);
            }
        }
    }
    LOG_I("+-----------------+----------+------------+------------+");
    LOG_W("Note: Optimizable = HWM - 512 (keeping 512 bytes safety buffer)");
}
#endif
}

