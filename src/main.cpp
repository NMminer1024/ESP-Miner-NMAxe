#include <WiFi.h>
#include "global.h"
#include "logger.h"
#include "display.h"
#include "monitor.h"
#include "button.h"
#include "fan.h"
#include "Wire.h"
#include "led.h"
#include "market.h"
#include "axe_http_server.h"
#include "axe_nvs_config.h"

TaskHandle_t fanTask, ledTask, pwrTask, asicinitTask, btnTask, uiTask, monitorTask, stratumTask, minerTxTask, minerRxTask;

axe_sal_t g_nmaxe = {
  .power = NMAxePowerClass(
      {
          .pwr_0v8 = NM_AXE_POWER_BM1366_VPLL_0V8_ENABLE_PIN,
          .pwr_1v8 = NM_AXE_POWER_BM1366_VDD_1V8_ENABLE_PIN,
          .pwr_vcore = NM_AXE_POWER_BM1366_VCORE_ENABLE_PIN
      },
      {
          .vbus = NM_AXE_POWER_BM1366_VBUS_ADC_PIN,
          .ibus = NM_AXE_POWER_BM1366_IBUS_ADC_PIN,
          .vcore = NM_AXE_POWER_BM1366_VCORE_ADC_PIN
      },
      NM_AXE_POWER_BM1366_VCORE_REGULATOR_PWM_PIN,
      NM_AXE_POWER_BM1366_VCORE_P_GOOD_DET_PIN,
      NM_AXE_POWER_BM1366_VBUS_PLUG_SENSE_DET_PIN
  )
};

void setup() {
  String taskName;
  /************************************************************ INIT SERIAL *************************************************************/
  Serial.setTimeout(20);
  Serial.begin(115200);
  /************************************************************** INIT NVS  *************************************************************/
  while(!load_g_nmaxe()){
    LOG_E("Load global parameters failed!");
    delay(1000);
  }
  /************************************************************* INIT I2C ***************************************************************/
  Wire.begin(NM_AXE_IIC_SDA_PIN, NM_AXE_IIC_SCL_PIN);
  /************************************************************ INIT DISPLAY ************************************************************/
  taskName = "(ui)";
  xTaskCreatePinnedToCore(ui_thread_entry, taskName.c_str(), 1024*6, (void*)taskName.c_str(), TASK_PRIORITY_UI, &uiTask, 1);
  delay(10);
  /************************************************************* INIT LOGO **************************************************************/
  logo_print();
  /********************************************************** CREATE LED THREAD *********************************************************/
  taskName = "(led)";
  xTaskCreatePinnedToCore(led_thread_entry, taskName.c_str(), 1024*4, (void*)taskName.c_str(), TASK_PRIORITY_LED, &ledTask, 1);
  delay(10);
  /********************************************************** CREATE BUTTON THREAD *****************************************************/
  taskName = "(button)";
  xTaskCreatePinnedToCore(button_thread_entry, taskName.c_str(), 1024*6, (void*)taskName.c_str(), TASK_PRIORITY_BTN, &btnTask,1);
  delay(10);
  /********************************************************* CREATE FAN THREAD *********************************************************/
  taskName = "(fan)";
  xTaskCreatePinnedToCore(fan_thread_entry, taskName.c_str(), 1024*3, (void*)taskName.c_str(), TASK_PRIORITY_FAN, &fanTask,1);
  delay(10);
  /************************************************************* INIT POWER *************************************************************/
  taskName = "(power)";
  xTaskCreatePinnedToCore(power_thread_entry, taskName.c_str(), 1024*6, (void*)taskName.c_str(), TASK_PRIORITY_PWR, &pwrTask,1);
  xSemaphoreTake(g_nmaxe.power.good_xsem, portMAX_DELAY);
  /************************************************************* INIT ASIC *************************************************************/
  taskName = "(asic_init)";
  xTaskCreatePinnedToCore(miner_asic_init_thread_entry, taskName.c_str(), 1024*6, (void*)taskName.c_str(), TASK_PRIORITY_ASIC_INIT, &asicinitTask,1);
  while (g_nmaxe.miner->get_asic_count() == 0){
    delay(10);
  }
  /************************************************************** INIT WIFI ************************************************************/
  axe_wifi_connecet(g_nmaxe.connection.wifi.conn_param);//blockingly connect to wifi
  /*********************************************************** CREATE MARKET THREAD ***************************************************/
  taskName = "(market)";
  xTaskCreatePinnedToCore(market_thread_entry, taskName.c_str(), 1024*6, (void*)taskName.c_str(), TASK_PRIORITY_MARKET, NULL, 0);
  while (!g_nmaxe.market.connected){
    delay(10);
  }
  /********************************************************** CREATE STRATUM THREAD ***************************************************/
  taskName = "(stratum)";
  xTaskCreatePinnedToCore(stratum_thread_entry, taskName.c_str(), 1024*12, (void*)taskName.c_str(), TASK_PRIORITY_STRATUM, &stratumTask, 0);
  delay(10);
  /********************************************************** CREATE MONITOR THREAD ***************************************************/
  taskName = "(monitor)";
  xTaskCreatePinnedToCore(monitor_thread_entry, taskName.c_str(), 1024*6, (void*)taskName.c_str(), TASK_PRIORITY_MONITOR, &monitorTask,1);
  delay(10);
  /********************************************************** CREATE MINER TX THREAD **************************************************/
  taskName = "(asic_tx)";
  xTaskCreatePinnedToCore(miner_asic_tx_thread_entry, taskName.c_str(), 1024*7, (void*)taskName.c_str(), TASK_PRIORITY_MINER_TX, &minerTxTask,1);
  delay(10);
  /*********************************************************  CREATE MINER RX THREAD **************************************************/
  taskName = "(asic_rx)";
  xTaskCreatePinnedToCore(miner_asic_rx_thread_entry, taskName.c_str(), 1024*6, (void*)taskName.c_str(), TASK_PRIORITY_MINER_RX, &minerRxTask,0);
  delay(10);
}


void loop() {
  //WiFi monitor
  if(xSemaphoreTake(g_nmaxe.connection.wifi.reconnect_xsem, 500) == pdTRUE){
    LOG_W("WiFi reconnecting..");
    WiFi.begin(g_nmaxe.connection.wifi.conn_param.ssid.c_str(), g_nmaxe.connection.wifi.conn_param.pwd.c_str());
  }
}

