#include <WiFi.h>
#include "global.h"
#include "logger.h"
#include "display.h"
#include "market.h"
#include "http_server.h"
#include "nvs_config.h"
#include "github.h"
#include "thread_entry.h"

TaskHandle_t fanTask, ledTask, btnTask, uiTask, touchTask, monitorTask, swarmTask, marketTask, daemonTask, stratumTask, minerTxTask, minerRxTask;
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
    
    board->info.connection.pool_primary.ssl         = ((stratum_pri.indexOf("ssl") != -1) || (stratum_pri.indexOf("tls") != -1));
    board->info.connection.pool_primary.url         = stratum_pri.substring(stratum_pri.indexOf(":") + 3, stratum_pri.lastIndexOf(":"));
    board->info.connection.pool_primary.port        = stratum_pri.substring(stratum_pri.lastIndexOf(":") + 1, stratum_pri.length()).toInt();
    board->info.connection.pool_fallback.ssl        = ((stratum_fb.indexOf("ssl") != -1) || (stratum_fb.indexOf("tls") != -1));
    board->info.connection.pool_fallback.url        = stratum_fb.substring(stratum_fb.indexOf(":") + 3, stratum_fb.lastIndexOf(":"));
    board->info.connection.pool_fallback.port       = stratum_fb.substring(stratum_fb.lastIndexOf(":") + 1, stratum_fb.length()).toInt();
    board->info.connection.pool_use                 = board->info.connection.pool_primary;
    board->info.base.fw_version                     = CURRENT_FW_VERSION;
    board->info.base.hw_version                     = CURRENT_HW_VERSION;
    board->info.base.devcie_code                    = gen_device_code();
    board->info.base.fw_latest_release              = "";
    board->info.connection.stratum_primary.user     = String(nvs_config_get_string(NVS_CONFIG_STRATUM_USER_PRIMARY, (String(PRIMARY_USER) + "." + board->info.spec.name + "_" + board->info.base.devcie_code.substring(0, 5)).c_str()));
    board->info.connection.stratum_primary.pwd      = String(nvs_config_get_string(NVS_CONFIG_STRATUM_PASS_PRIMARY, PRIMARY_POOL_PWD));
    board->info.connection.stratum_fallback.user    = String(nvs_config_get_string(NVS_CONFIG_STRATUM_USER_FALLBACK, (String(FALLBACK_USER) + "." + board->info.spec.name + "_" + board->info.base.devcie_code.substring(0, 5)).c_str()));
    board->info.connection.stratum_fallback.pwd     = String(nvs_config_get_string(NVS_CONFIG_STRATUM_PASS_FALLBACK, FALLBACK_POOL_PWD));
    board->info.connection.stratum_use              = board->info.connection.stratum_primary;
    board->info.connection.wifi.reconnect_xsem      = xSemaphoreCreateCounting(1, 0);
    board->info.connection.wifi.force_cfg_xsem      = xSemaphoreCreateCounting(1, 0);
    board->info.connection.wifi.softap_param.ip     = IPAddress(192, 168, 4, 1);
    board->info.connection.wifi.softap_param.pwd    = "12345678";
    board->info.connection.wifi.softap_param.ssid   = String(nvs_config_get_string(NVS_CONFIG_AP_SSID, (board->info.spec.name + "_" + board->info.base.devcie_code.substring(0, 5)).c_str())); 

    board->info.connection.force_config             = nvs_config_get_u8(NVS_CONFIG_FORCE_CONFIG, false);
    board->info.connection.client_connected         = false;
    board->info.connection.wifi.conn_param.ssid     = String(nvs_config_get_string(NVS_CONFIG_WIFI_SSID, "NMTech-2.4G"));
    board->info.connection.wifi.conn_param.pwd      = String(nvs_config_get_string(NVS_CONFIG_WIFI_PASS, "NMMiner2048"));
    board->info.base.hostname                       = String(nvs_config_get_string(NVS_CONFIG_HOSTNAME, board->info.connection.wifi.softap_param.ssid.c_str()));
    board->info.connection.stratum_update           = millis();
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
    board->status.miner.history_mutex               = xSemaphoreCreateMutex();
    board->status.miner.block_proximity_mutex       = xSemaphoreCreateMutex();
    board->status.miner.update_xsem                 = xSemaphoreCreateCounting(1, 0);
    board->status.brightness_update_xsem            = xSemaphoreCreateCounting(1, 0);
    board->status.miner.hits                        = nvs_config_get_u16(NVS_CONFIG_BLOCK_HITS, 0);
    board->status.miner.last_hits                   = board->status.miner.hits;
    board->status.ota.running                       = false;
    board->status.ota.progress                      = 0;
    board->status.ota.filename                      = "";
    board->status.miner.diff.best_ever              = strtoull(nvs_config_get_string(NVS_CONFIG_BEST_EVER, "0"), NULL, 10);
    board->status.ui.page.last                      = nvs_config_get_u8(NVS_CONFIG_UI_LAST_PAGE, UI_PAGE_MINER);
    board->status.ui.page.current                   = board->status.ui.page.last;
    board->status.ui.page.save_xsem                 = xSemaphoreCreateCounting(1, 0);
    board->status.ui.touch.xsem                     = xSemaphoreCreateCounting(1, 0);
    board->status.ui.touch.evt                      = TOUCH_NONE_EVT;
    board->status.miner.uptime_ever                 = nvs_config_get_u64(NVS_CONFIG_UPTIME, 0);
    board->status.time.tz                           = String(nvs_config_get_string(NVS_CONFIG_TIMEZONE, "8.0"));
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
    AxePowerHal* power_instance                     = board->info.spec.create_power_instance(config.pwr.en_pins, config.pwr.adc_pins, config.pwr.vcore_regulator_pin, config.pwr.pgood_pin, config.pwr.dc_plug_pin);
    board->power                                    = new AxePowerClass(power_instance);
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
    board->stratum                                  = new StratumClass(board->info.connection.pool_use, board->info.connection.stratum_use, 10);
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

void setup() {
  String taskName;
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
  /************************************************************ INIT DISPLAY ************************************************************/
  taskName = "(ui)";
  xTaskCreatePinnedToCore(ui_thread_entry, taskName.c_str(), 1024*5, (void*)taskName.c_str(), TASK_PRIORITY_UI, &uiTask, 1);
  delay(10);
  taskName = "(touch)";
  xTaskCreatePinnedToCore(touch_thread_entry, taskName.c_str(), 1024*5, (void*)(&g_board), TASK_PRIORITY_TOUCH, &touchTask, 0);
  delay(10);
  /********************************************************** CREATE LED THREAD *********************************************************/
  taskName = "(led)";
  xTaskCreatePinnedToCore(led_thread_entry, taskName.c_str(), 1024*3, (void*)(&g_board), TASK_PRIORITY_LED, &ledTask, 1);
  delay(10);
  /********************************************************** CREATE BUTTON THREAD *****************************************************/
  taskName = "(button)";
  xTaskCreatePinnedToCore(button_thread_entry, taskName.c_str(), 1024*6, (void*)(&g_board), TASK_PRIORITY_BTN, &btnTask,1);
  delay(10);
  /********************************************************* CREATE FAN THREAD *********************************************************/
  taskName = "(fan)";
  xTaskCreatePinnedToCore(fan_thread_entry, taskName.c_str(), 1024*7, (void*)(&g_board), TASK_PRIORITY_FAN, &fanTask,0);
  delay(10);
  /************************************************************* INIT POWER *************************************************************/
  taskName = "(power)";
  xTaskCreatePinnedToCore(power_thread_entry, taskName.c_str(), 1024*6, (void*)(&g_board), TASK_PRIORITY_PWR, NULL,1);
  xSemaphoreTake(g_board.power->ready_xsem, portMAX_DELAY);
  /************************************************************* INIT ASIC *************************************************************/
  taskName = "(asic_init)";
  xTaskCreatePinnedToCore(miner_asic_init_thread_entry, taskName.c_str(), 1024*7, (void*)(&g_board), TASK_PRIORITY_ASIC_INIT, NULL,1);
  while (g_board.miner->get_asic_count() == 0){
    delay(10);
  }
  if(g_board.miner->get_asic_count() != g_board.info.spec.asic.num_req){
    LOG_E("Detected ASIC count (%d/%d) does not match required ASIC count!!!!", g_board.miner->get_asic_count(), g_board.info.spec.asic.num_req);
  }
  /************************************************************** INIT DAEMON **********************************************************/
  taskName = "(daemon)";
  xTaskCreatePinnedToCore(daemon_thread_entry, taskName.c_str(), 1024*3.5, (void*)(&g_board), TASK_PRIORITY_DAEMON, &daemonTask, 0);
  delay(10);
  /************************************************************** INIT WIFI ************************************************************/
  taskName = "(wifi)";
  xTaskCreatePinnedToCore(wifi_connect_thread_entry, taskName.c_str(), 1024*6, (void*)(&g_board), TASK_PRIORITY_WIFI, NULL, 1);
  while (WL_CONNECTED != g_board.info.connection.wifi.status_param.status){
    delay(10);
  }
  /************************************************************ Version check **********************************************************/
#if HAS_VERSION_CHECK_FEATURE
  ReleaseCheckerClass *releaseChecker = new ReleaseCheckerClass(); 
  g_board.info.base.fw_latest_release = releaseChecker->get_latest_release();

  if(0 == compareVersions(g_board.info.base.fw_version, g_board.info.base.fw_latest_release)){
    LOG_I("Firmware up to date: [%s]", g_board.info.base.fw_latest_release.c_str());
  }
  else if(-2 == compareVersions(g_board.info.base.fw_version, g_board.info.base.fw_latest_release)){
    LOG_W("Get release info failed, please check your network connection.");
  }
  else if(-1 == compareVersions(g_board.info.base.fw_version, g_board.info.base.fw_latest_release)){
    LOG_W("New version available: %s", g_board.info.base.fw_latest_release.c_str());
    delay(1000*5);
  }
  delete releaseChecker;
#endif
  /************************************************************* INIT SWARM ************************************************************/
  taskName = "(swarm)";
  xTaskCreatePinnedToCore(swarm_thread_entry, taskName.c_str(), 1024*7, (void*)(&g_board), TASK_PRIORITY_SWARM, &swarmTask, 0);
  delay(10);
  /*********************************************************** CREATE MARKET THREAD ****************************************************/
  taskName = "(market)";
  xTaskCreatePinnedToCore(market_thread_entry, taskName.c_str(), 1024*6, (void*)(&g_board), TASK_PRIORITY_MARKET, &marketTask, 0);
  delay(10);
  uint32_t start = millis();
  while (0 == g_board.market->lastUpdate){
    if(millis() - start - g_board.market->lastUpdate > MARKET_TIMEOUT){
      LOG_W("Market data update timeout, exiting...");
      delay(1000);
      break;
    }
    LOG_I("Waiting for market data update... %ds elapsed", (millis() - start) / 1000);
    delay(1000);
  }
  if(0 != g_board.market->lastUpdate) LOG_I("Market data updated successfully, last update: %.2fs ago", (millis() - g_board.market->lastUpdate) / 1000.0f);
  delay(500);
  /********************************************************** CREATE STRATUM THREAD ***************************************************/
  taskName = "(stratum)";
  xTaskCreatePinnedToCore(stratum_thread_entry, taskName.c_str(), 1024*11, (void*)(&g_board), TASK_PRIORITY_STRATUM, &stratumTask, 1);
  delay(10);
  /********************************************************** CREATE MONITOR THREAD ***************************************************/
  taskName = "(monitor)";
  xTaskCreatePinnedToCore(monitor_thread_entry, taskName.c_str(), 1024*7, (void*)(&g_board), TASK_PRIORITY_MONITOR, &monitorTask,1);
  delay(500);
  /********************************************************** CREATE MINER TX THREAD **************************************************/
  taskName = "(asic_tx)";
  xTaskCreatePinnedToCore(miner_asic_tx_thread_entry, taskName.c_str(), 1024*5, (void*)(&g_board), TASK_PRIORITY_MINER_TX, &minerTxTask,1);
  delay(10);
  /*********************************************************  CREATE MINER RX THREAD **************************************************/
  taskName = "(asic_rx)";
  xTaskCreatePinnedToCore(miner_asic_rx_thread_entry, taskName.c_str(), 1024*6, (void*)(&g_board), TASK_PRIORITY_MINER_RX, &minerRxTask,0);
  delay(10);
}


void loop() {
  // TaskHandle_t fanTask, ledTask, btnTask, uiTask, monitorTask, swarmTask, marketTask, daemonTask , stratumTask, minerTxTask, minerRxTask;

  // for testing only: simulate block hits every 20s
  // static uint32_t cnt = 1;
  // if(cnt++ % 20 == 0){
  //   g_board.status.miner.hits++;
  // }

  uint32_t last = millis();
  while(millis() - last < 1000*1){
      static uint16_t brightness      = g_board.status.preference.screen.brightness, last_brightness = brightness;   
      static float    x = 0;
      if(g_board.status.miner.last_hits != g_board.status.miner.hits){//screen blink if block hit
          brightness = 100*(1 + sin(x))/2;
          x+=0.1;
          tft_bl_ctrl(brightness);
      } else brightness = g_board.status.preference.screen.brightness;
      delay(10);
  }

#if 0
  static uint32_t start = millis();
  static UBaseType_t highWaterMark = uxTaskGetStackHighWaterMark(NULL);
  static char *taskName = pcTaskGetName(NULL);
  LOG_W("=======================================");
  if(fanTask != NULL) {
      highWaterMark = uxTaskGetStackHighWaterMark(fanTask);
      taskName = pcTaskGetName(fanTask);
      LOG_I("%s Stack High Water Mark: %u", taskName, highWaterMark);
  }
  if(ledTask != NULL) {
      highWaterMark = uxTaskGetStackHighWaterMark(ledTask);
      taskName = pcTaskGetName(ledTask);
      LOG_I("%s Stack High Water Mark: %u", taskName, highWaterMark);
  }
  if(uiTask != NULL) {
      highWaterMark = uxTaskGetStackHighWaterMark(uiTask);
      taskName = pcTaskGetName(uiTask);
      LOG_I("%s Stack High Water Mark: %u", taskName, highWaterMark);
  }
  if(btnTask != NULL) {
      highWaterMark = uxTaskGetStackHighWaterMark(btnTask);
      taskName = pcTaskGetName(btnTask);
      LOG_I("%s Stack High Water Mark: %u", taskName, highWaterMark);
  }
  if(monitorTask != NULL) {
      highWaterMark = uxTaskGetStackHighWaterMark(monitorTask);
      taskName = pcTaskGetName(monitorTask);
      LOG_I("%s Stack High Water Mark: %u", taskName, highWaterMark);
  }
  if(swarmTask != NULL) {
      highWaterMark = uxTaskGetStackHighWaterMark(swarmTask);
      taskName = pcTaskGetName(swarmTask);
      LOG_I("%s Stack High Water Mark: %u", taskName, highWaterMark);
  }
  if(marketTask != NULL) {
      highWaterMark = uxTaskGetStackHighWaterMark(marketTask);
      taskName = pcTaskGetName(marketTask);
      LOG_I("%s Stack High Water Mark: %u", taskName, highWaterMark);
  }
  if(daemonTask != NULL) {
      highWaterMark = uxTaskGetStackHighWaterMark(daemonTask);
      taskName = pcTaskGetName(daemonTask);
      LOG_I("%s Stack High Water Mark: %u", taskName, highWaterMark);
  }
  if(stratumTask != NULL) {
      highWaterMark = uxTaskGetStackHighWaterMark(stratumTask);
      taskName = pcTaskGetName(stratumTask);
      LOG_I("%s Stack High Water Mark: %u", taskName, highWaterMark);
  }
  if(minerTxTask != NULL) {
      highWaterMark = uxTaskGetStackHighWaterMark(minerTxTask);
      taskName = pcTaskGetName(minerTxTask);
      LOG_I("%s Stack High Water Mark: %u", taskName, highWaterMark);
  }
  if(minerRxTask != NULL) {
      highWaterMark = uxTaskGetStackHighWaterMark(minerRxTask);
      taskName = pcTaskGetName(minerRxTask);
      LOG_I("%s Stack High Water Mark: %u", taskName, highWaterMark);
  }
#endif
}

