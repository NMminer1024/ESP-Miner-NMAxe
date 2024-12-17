#include <NTPClient.h>
#include <WiFiUdp.h>
#include "axe_nvs_config.h"
#include "monitor.h"
#include "logger.h"
#include "helper.h"
#include "global.h"
#include "tmp102.h"

static WiFiUDP        udpStatus;
static const char*    status_udp_addr = "255.255.255.255"; 
static const int      status_udp_port = 12345; 

void monitor_thread_entry(void *args){
  char *name = (char*)malloc(20);
  strcpy(name, (char*)args);
  LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
  free(name);

  //udp status boardcast begin
  udpStatus.begin(status_udp_port);
  //wait for first job cache ready forever when process start
  xSemaphoreTake(g_nmaxe.stratum.new_job_xsem, portMAX_DELAY);
  delay(500);//necessary delay for first job cache ready

  while(true){
      g_nmaxe.mstatus.uptime++;
      //update temperature and power status
      if(g_nmaxe.mstatus.uptime % 1 == 0){
        //update power status
        g_nmaxe.board.vbus = g_nmaxe.power.get_vbus();
        g_nmaxe.board.ibus = g_nmaxe.power.get_ibus();
        g_nmaxe.asic.vcore_measured = g_nmaxe.power.get_vcore();
        //update temperature
        g_nmaxe.board.temp_mcu    = (int8_t)get_mcu_temperature();
        g_nmaxe.board.temp_vcore  = (int8_t)get_vcore_temperature();
        g_nmaxe.asic.temp         = (int8_t)get_asic_temperature();
        //update wifi rssi
        g_nmaxe.connection.wifi.status_param.rssi = WiFi.RSSI();
        //give miner update signal
        xSemaphoreGive(g_nmaxe.mstatus.update_xsem);
      }
      //send status to udp broadcast
      if(g_nmaxe.mstatus.uptime % 2 == 0){
        if(g_nmaxe.connection.wifi.status_param.status == WL_CONNECTED){
          StaticJsonDocument<512> jsonDoc;
          jsonDoc["ip"] = g_nmaxe.connection.wifi.status_param.ip.toString();
          jsonDoc["HashRate"] = formatNumber(g_nmaxe.mstatus.hashrate, 5) + "H/s";
          uint32_t share_total = g_nmaxe.mstatus.share_accepted + g_nmaxe.mstatus.share_rejected;
          float share_accepted = (share_total == 0) ? 0:(float)(g_nmaxe.mstatus.share_accepted) / (float)(share_total);
          jsonDoc["Share"] = String(g_nmaxe.mstatus.share_rejected) + "/"+ String(g_nmaxe.mstatus.share_accepted) + "/" + String(share_accepted * 100, 1) + "%";
          jsonDoc["NetDiff"] = formatNumber(g_nmaxe.mstatus.network_diff,4);
          jsonDoc["PoolDiff"] = formatNumber(g_nmaxe.mstatus.pool_diff,4);
          jsonDoc["LastDiff"] = formatNumber(g_nmaxe.mstatus.last_diff,4);
          jsonDoc["BestDiff"] = formatNumber(g_nmaxe.mstatus.best_ever,4);
          jsonDoc["Valid"] = g_nmaxe.mstatus.block_hits;
          jsonDoc["Temp"] = g_nmaxe.asic.temp;
          jsonDoc["RSSI"] = g_nmaxe.connection.wifi.status_param.rssi;
          jsonDoc["FreeHeap"] = ESP.getFreeHeap() / 1024.0f;
          jsonDoc["Uptime"] = convert_uptime_to_string(g_nmaxe.mstatus.uptime);
          jsonDoc["Version"] = g_nmaxe.board.fw_version;
          jsonDoc["BoardType"] = g_nmaxe.board.hw_model;

          char jsonBuffer[512];
          size_t n = serializeJson(jsonDoc, jsonBuffer);
          udpStatus.beginPacket(status_udp_addr, status_udp_port);
          udpStatus.write((uint8_t*)jsonBuffer,n);
          udpStatus.endPacket();
        }
      }
      //check overheat
      if(g_nmaxe.mstatus.uptime % 5 == 0){
        if(g_nmaxe.board.temp_mcu > BOARD_MCU_DANGER){
          LOG_W("MCU temp is too high, restart...");
          delay(1000);
          ESP.restart();
        }
        if(g_nmaxe.board.temp_vcore > VCORE_TEMP_DANGER || g_nmaxe.asic.temp > ASIC_TEMP_DANGER){
          uint16_t vcore_now = g_nmaxe.power.get_vcore();
          if(vcore_now >= 1200)vcore_now -= 100;
          else{
            static uint8_t cnt = 0;
            if(++cnt > 5){
              LOG_W("Vcore temp reach danger for 5 seconds, restart...");
              delay(1000);
              ESP.restart();
            }
          }
          g_nmaxe.power.set_vcore_voltage(vcore_now);
          LOG_W("Vcore temp reach danger %.1fC, decrease vcore to %d", g_nmaxe.board.temp_vcore, vcore_now);
        }
        static uint8_t fan_cnt = 0;
        if(g_nmaxe.fan.rpm <= 1000){
          fan_cnt++;
          if(fan_cnt > 0){//avoid some noise
            LOG_W("Fan rpm is too low, restart miner...");
            ESP.restart();
          }
        }else fan_cnt = 0;
      }
      //save status to NVS
      static uint64_t last_save_time = g_nmaxe.mstatus.uptime;
      if(g_nmaxe.mstatus.uptime - last_save_time > NVS_SAVE_INTERVAL){
        xSemaphoreGive(g_nmaxe.mstatus.nvs_save_xsem);
      }
      //save some status to NVS
      if(xSemaphoreTake(g_nmaxe.mstatus.nvs_save_xsem, 0) == pdTRUE){
          nvs_config_set_string(NVS_CONFIG_BEST_EVER, String(g_nmaxe.mstatus.best_ever).c_str());
          nvs_config_set_u16(NVS_CONFIG_BLOCK_HITS, g_nmaxe.mstatus.block_hits);
          nvs_config_set_u64(NVS_CONFIG_UPTIME, g_nmaxe.mstatus.uptime);
          last_save_time = g_nmaxe.mstatus.uptime;
          LOG_W("Save diff best ever [%s], block hits [%d], uptime [%s]", formatNumber(g_nmaxe.mstatus.best_ever, 4).c_str(), g_nmaxe.mstatus.block_hits, convert_uptime_to_string(g_nmaxe.mstatus.uptime).c_str());
      }
      //thread delay 1s
      delay(1000);
  }
}