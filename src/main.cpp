#include <WiFi.h>
#include "global.h"
#include "logger.h"
#include "display.h"
#include "monitor.h"
#include "button.h"
#include "fan.h"

#include "led.h"
#include "market.h"
#include "http_server.h"
#include "nvs_config.h"
#include "github.h"

TaskHandle_t fanTask, ledTask, btnTask, uiTask, monitorTask, swarmTask, marketTask, daemonTask, stratumTask, minerTxTask, minerRxTask;

board_sal_t  g_board;

void setup() {
  String taskName;
  /************************************************************ INIT SERIAL *************************************************************/
  Serial.setTimeout(20);
  Serial.begin(115200);
  /************************************************************** INIT NVS  *************************************************************/
  while(!load_g_board()){
    LOG_E("Load global parameters failed!");
    delay(100);
  }
  /************************************************************ INIT DISPLAY ************************************************************/
  taskName = "(ui)";
  xTaskCreatePinnedToCore(ui_thread_entry, taskName.c_str(), 1024*5, (void*)taskName.c_str(), TASK_PRIORITY_UI, &uiTask, 1);
  delay(10);
  /************************************************************* INIT LOGO **************************************************************/
  logo_print();
  /********************************************************** CREATE LED THREAD *********************************************************/
  taskName = "(led)";
  xTaskCreatePinnedToCore(led_thread_entry, taskName.c_str(), 1024*3, (void*)(&g_board), TASK_PRIORITY_LED, &ledTask, 1);
  delay(10);
  /********************************************************** CREATE BUTTON THREAD *****************************************************/
  taskName = "(button)";
  xTaskCreatePinnedToCore(button_thread_entry, taskName.c_str(), 1024*6, (void*)taskName.c_str(), TASK_PRIORITY_BTN, &btnTask,1);
  delay(10);
  /********************************************************* CREATE FAN THREAD *********************************************************/
  taskName = "(fan)";
  xTaskCreatePinnedToCore(fan_thread_entry, taskName.c_str(), 1024*6, (void*)taskName.c_str(), TASK_PRIORITY_FAN, &fanTask,0);
  delay(10);
  /************************************************************* INIT POWER *************************************************************/
  taskName = "(power)";
  xTaskCreatePinnedToCore(power_thread_entry, taskName.c_str(), 1024*6, (void*)taskName.c_str(), TASK_PRIORITY_PWR, NULL,1);
  xSemaphoreTake(g_board.power->good_xsem, portMAX_DELAY);
  /************************************************************* INIT ASIC *************************************************************/
  taskName = "(asic_init)";
  xTaskCreatePinnedToCore(miner_asic_init_thread_entry, taskName.c_str(), 1024*7, (void*)taskName.c_str(), TASK_PRIORITY_ASIC_INIT, NULL,1);
  while (g_board.miner->get_asic_count() == 0){
    delay(10);
  }
  /************************************************************** INIT DAEMON **********************************************************/
  taskName = "(daemon)";
  xTaskCreatePinnedToCore(daemon_thread_entry, taskName.c_str(), 1024*3.5, (void*)taskName.c_str(), TASK_PRIORITY_DAEMON, &daemonTask, 0);
  delay(10);
  /************************************************************** INIT WIFI ************************************************************/
  taskName = "(wifi)";
  xTaskCreatePinnedToCore(wifi_connect_thread_entry, taskName.c_str(), 1024*6, (void*)&g_board.info.connection.wifi.conn_param, TASK_PRIORITY_WIFI, NULL, 1);
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
  xTaskCreatePinnedToCore(swarm_thread_entry, taskName.c_str(), 1024*7, (void*)taskName.c_str(), TASK_PRIORITY_SWARM, &swarmTask, 0);
  delay(10);
  /*********************************************************** CREATE MARKET THREAD ****************************************************/
  taskName = "(market)";
  xTaskCreatePinnedToCore(market_thread_entry, taskName.c_str(), 1024*6, (void*)taskName.c_str(), TASK_PRIORITY_MARKET, &marketTask, 0);
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
  xTaskCreatePinnedToCore(stratum_thread_entry, taskName.c_str(), 1024*11, (void*)taskName.c_str(), TASK_PRIORITY_STRATUM, &stratumTask, 1);
  delay(10);
  /********************************************************** CREATE MONITOR THREAD ***************************************************/
  taskName = "(monitor)";
  xTaskCreatePinnedToCore(monitor_thread_entry, taskName.c_str(), 1024*7, (void*)taskName.c_str(), TASK_PRIORITY_MONITOR, &monitorTask,1);
  delay(500);
  /********************************************************** CREATE MINER TX THREAD **************************************************/
  taskName = "(asic_tx)";
  xTaskCreatePinnedToCore(miner_asic_tx_thread_entry, taskName.c_str(), 1024*5, (void*)taskName.c_str(), TASK_PRIORITY_MINER_TX, &minerTxTask,1);
  delay(10);
  /*********************************************************  CREATE MINER RX THREAD **************************************************/
  taskName = "(asic_rx)";
  xTaskCreatePinnedToCore(miner_asic_rx_thread_entry, taskName.c_str(), 1024*6, (void*)taskName.c_str(), TASK_PRIORITY_MINER_RX, &minerRxTask,0);
  delay(10);
}


void loop() {
  // TaskHandle_t fanTask, ledTask, btnTask, uiTask, monitorTask, swarmTask, marketTask, daemonTask , stratumTask, minerTxTask, minerRxTask;
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

  delay(1000);
}

