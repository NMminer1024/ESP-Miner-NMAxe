#include <NTPClient.h>
#include <WiFiUdp.h>
#include "nvs_config.h"
#include "monitor.h"
#include "logger.h"
#include "helper.h"
#include "global.h"
#include "tmp102.h"

#define UDP_BOARDCAST_ADDR    IPAddress(255,255,255,255)
#define UDP_BOARDCAST_PORT    (12345)
#define SWARM_OFFLINE_TIMEOUT (3*60*1000) //3min

static WiFiUDP*                udp_client;

void monitor_thread_entry(void *args){
  char *name = (char*)malloc(20);
  strcpy(name, (char*)args);
  LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
  free(name);
  
  //wait for first job cache ready forever when process start
  xSemaphoreTake(g_nmaxe.stratum->new_job_xsem, portMAX_DELAY);

  delay(500);//necessary delay for first job cache ready

  while(true){
      //thread delay 1000ms
      delay(1000);
      
      g_nmaxe.mstatus.uptime_ever++;
      g_nmaxe.mstatus.uptime_session++;

      //update temperature and power status
      if(g_nmaxe.mstatus.uptime_session % 1 == 0){
        static uint8_t temp_cnt = 0;

        //update power status
        g_nmaxe.board.vbus          = (temp_cnt % 2 == 0) ? g_nmaxe.power->get_vbus() : g_nmaxe.board.vbus;
        g_nmaxe.board.ibus          = (temp_cnt % 2 == 0) ? g_nmaxe.power->get_ibus() : g_nmaxe.board.ibus;
        g_nmaxe.board.efficiency    = ((temp_cnt % 2 == 0) && g_nmaxe.mstatus.hashrate._3m > 0) ? (g_nmaxe.board.vbus * g_nmaxe.board.ibus/1e6) / (g_nmaxe.mstatus.hashrate._3m/1e12) : g_nmaxe.board.efficiency;
        g_nmaxe.asic.vcore_measured = (temp_cnt % 2 == 0) ? g_nmaxe.power->get_vcore() : g_nmaxe.asic.vcore_measured;
        //update board temperature
        g_nmaxe.temp.mcu    = (temp_cnt % 30 == 0) ? (int8_t)get_mcu_temperature() : g_nmaxe.temp.mcu;
        g_nmaxe.temp.vcore  = (temp_cnt % 2 == 0) ? (int8_t)get_vcore_temperature() : g_nmaxe.temp.vcore;
        g_nmaxe.temp.asic   = (temp_cnt % 2 == 0) ? (int8_t)get_asic_temperature() : g_nmaxe.temp.asic;

        temp_cnt++;
        //update wifi rssi
        g_nmaxe.connection.wifi.status_param.rssi = WiFi.RSSI();
        //give miner update signal
        xSemaphoreGive(g_nmaxe.mstatus.update_xsem);
      }
      
      //status check
      if(g_nmaxe.mstatus.uptime_session % 2 == 0){
        //check mcu temperature status
        if(g_nmaxe.temp.mcu > BOARD_MCU_DANGER){
          LOG_W("MCU temp is too high, restart...");
          delay(1000);
          ESP.restart();
        }
        //check vcore temperature status
        if(g_nmaxe.temp.vcore > VCORE_TEMP_DANGER || g_nmaxe.temp.asic > ASIC_TEMP_DANGER){
          uint16_t vcore_now = g_nmaxe.power->get_vcore();
          if(vcore_now >= 1200)vcore_now -= 100;
          else{
            static uint8_t cnt = 0;
            if(++cnt > 5){
              LOG_W("Vcore temp reach danger for 5 seconds, restart...");
              delay(1000);
              ESP.restart();
            }
          }
          g_nmaxe.power->set_vcore_voltage(vcore_now);
          LOG_W("Vcore temp reach danger %.1fC, decrease vcore to %d", g_nmaxe.temp.vcore, vcore_now);
        }
        //check fan status
        static uint8_t fan_err_cnt = 0;
        if(g_nmaxe.preference.fan.rpm <= 1000){
          fan_err_cnt++;
          if(fan_err_cnt > 5){//avoid some noise
            LOG_W("Fan rpm is too low, restart miner...");
            ESP.restart();
          }
        }else fan_err_cnt = 0;

        //check power status
        static uint8_t pwr_err_cnt = 0;
        if((g_nmaxe.board.vbus * g_nmaxe.board.ibus / 1000.0 / 1000.0) < BOARD_LOW_POWER){
          LOG_W("Power %0.1fW is too low...", g_nmaxe.board.vbus * g_nmaxe.board.ibus / 1000.0 / 1000.0);
          if(++pwr_err_cnt > 5){
            LOG_W("Power is too low, restart miner...");
            ESP.restart();
          }
        }else pwr_err_cnt = 0;

        //check hashrate
        static uint8_t hr_err_cnt = 0;
        if(g_nmaxe.mstatus.hashrate._3m <= 1){
          if(++hr_err_cnt > 60*5){
            LOG_W("Hashrate is too low, restart miner...");
            ESP.restart();
          }
        }else hr_err_cnt = 0;
      }
      
      //print summary to log
      if(g_nmaxe.mstatus.uptime_session % 60 == 0){
        LOG_I(" ============%s=========== ",g_nmaxe.board.fw_version.c_str());
        LOG_I("|            Summary           |");
        LOG_I("+------------Uptime------------+");
        LOG_I("|%s | %s |", convert_uptime_to_string(g_nmaxe.mstatus.uptime_session).c_str(), convert_uptime_to_string(g_nmaxe.mstatus.uptime_ever).c_str());
        LOG_I("+-----------HashRate-----------+");
        LOG_I("|   3m    |    30m   |    1h   |");
        LOG_I("|%-4sH/s| %-4sH/s|%-4sH/s|", 
              formatNumber(g_nmaxe.mstatus.hashrate._3m, 4).c_str(), 
              formatNumber(g_nmaxe.mstatus.hashrate._30m, 4).c_str(),
              formatNumber(g_nmaxe.mstatus.hashrate._1h, 4).c_str());
        LOG_I("+----------Difficulty----------+");
        LOG_I("|From boot| Best ever| Network |");
        LOG_I("| %-6s |  %-5s | %-7s |", 
              formatNumber(g_nmaxe.mstatus.diff.best_session, 5).c_str(), 
              formatNumber(g_nmaxe.mstatus.diff.best_ever, 5).c_str(),
              formatNumber(g_nmaxe.mstatus.diff.network, 5).c_str());
        LOG_I("+---Free heap-----Efficiency---+");
        LOG_I("|    %-3sKB   |   %-3sJ/TH   |", formatNumber(ESP.getFreeHeap() / 1024.0f, 4).c_str(), formatNumber(g_nmaxe.board.efficiency, 4).c_str());
        LOG_I(" ============================== ");
      }
      
      //save status to NVS
      static uint64_t last_save_time = g_nmaxe.mstatus.uptime_ever;
      if(g_nmaxe.mstatus.uptime_ever - last_save_time > NVS_SAVE_INTERVAL){
        xSemaphoreGive(g_nmaxe.mstatus.nvs_save_xsem);
      }
      
      //save some status to NVS
      if(xSemaphoreTake(g_nmaxe.mstatus.nvs_save_xsem, 0) == pdTRUE){
          nvs_config_set_string(NVS_CONFIG_BEST_EVER, String(g_nmaxe.mstatus.diff.best_ever).c_str());
          nvs_config_set_u16(NVS_CONFIG_BLOCK_HITS, g_nmaxe.mstatus.block_hits);
          nvs_config_set_u64(NVS_CONFIG_UPTIME, g_nmaxe.mstatus.uptime_ever);
          last_save_time = g_nmaxe.mstatus.uptime_ever;
          LOG_W("Save diff best ever [%s], block hits [%d], uptime [%s]", formatNumber(g_nmaxe.mstatus.diff.best_ever, 4).c_str(), g_nmaxe.mstatus.block_hits, convert_uptime_to_string(g_nmaxe.mstatus.uptime_ever).c_str());
      }
  
      //check ota status and reboot
      if(xSemaphoreTake(g_nmaxe.ota.reboot_xsem, 0) == pdTRUE){
        ESP.restart();
      }
  }
}

void swarm_thread_entry(void *args){
  char *name = (char*)malloc(20);
  strcpy(name, (char*)args);
  LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
  free(name);

  //malloc udp client
  void* buffer = psramAllocator(sizeof(WiFiUDP));
  if (!buffer) {
      LOG_E("Failed to allocate memory in PSRAM for udp client");
      return;
  }
  udp_client = new(buffer) WiFiUDP();

  //udp status boardcast begin
  udp_client->begin(UDP_BOARDCAST_PORT);

  uint64_t swarm_cnt = 0;
  StaticJsonDocument<1024> jsonDoc;
  char  jsonbuf[1024] = {0,};
  while (true){
    swarm_cnt++;
    delay(100);
    //listen udp status
    if(swarm_cnt % 1 == 0){
      if(g_nmaxe.connection.wifi.status_param.status == WL_CONNECTED){
        int packetSize = udp_client->parsePacket();
        if (packetSize > 0) {
            char *incomingPacket = (char*)malloc(packetSize + 1 );
            memset(incomingPacket, '\0', packetSize + 1);
            int len = udp_client->read(incomingPacket, packetSize);


            char *json_str = (char*)malloc(packetSize + 1);
            memset(json_str, '\0', packetSize + 1);
            memcpy(json_str, incomingPacket, packetSize);
            jsonDoc.clear();
            DeserializationError error = deserializeJson(jsonDoc, json_str);
            if(error) {
              free(json_str);
              free(incomingPacket);
              udp_client->flush();
              continue;
            }
            jsonDoc["Lastseen"] = millis();

            memset(jsonbuf, 0, sizeof(jsonbuf));
            size_t n = serializeJson(jsonDoc, jsonbuf);

            //update swarm list if has this ip
            static std::map<String, uint32_t> last_seen_map;
            if(jsonDoc.containsKey("ip")){
              g_nmaxe.swarm[jsonDoc["ip"].as<String>()] = String(jsonbuf);
              last_seen_map[jsonDoc["ip"].as<String>()] = jsonDoc["Lastseen"];
            }

            //update json string
            for(auto it = g_nmaxe.swarm.begin(); it != g_nmaxe.swarm.end();it++){
              //self status not in last seen map
              if(last_seen_map.find(it->first) == last_seen_map.end()) continue;

              if(deserializeJson(jsonDoc, it->second)) continue;

              jsonDoc["Lastseen"] = millis() - last_seen_map[it->first];
              //remove offline device
              if(jsonDoc["Lastseen"].as<uint32_t>() > SWARM_OFFLINE_TIMEOUT){
                g_nmaxe.swarm.erase(it->first);
                continue;
              }

              memset(jsonbuf, 0, sizeof(jsonbuf));
              size_t n = serializeJson(jsonDoc, jsonbuf);
              it->second = (n>0) ? String(jsonbuf) : "";
            }
            
            free(json_str);
            free(incomingPacket);
            udp_client->flush();
        }
      }
    }
    //status udp broadcast
    if(swarm_cnt % 20 == 0){
      if(g_nmaxe.connection.wifi.status_param.status == WL_CONNECTED){
        jsonDoc.clear();
        jsonDoc["ip"] = g_nmaxe.connection.wifi.status_param.ip.toString();
        jsonDoc["HashRate"] = formatNumber(g_nmaxe.mstatus.hashrate._3m, 5) + "H/s";
        uint32_t share_total = g_nmaxe.mstatus.share_accepted + g_nmaxe.mstatus.share_rejected;
        float share_accepted = (share_total == 0) ? 0:(float)(g_nmaxe.mstatus.share_accepted) / (float)(share_total);
        jsonDoc["Share"] = String(g_nmaxe.mstatus.share_rejected) + "/"+ String(g_nmaxe.mstatus.share_accepted) + "/" + String(share_accepted * 100, 1) + "%";
        jsonDoc["NetDiff"] = formatNumber(g_nmaxe.mstatus.diff.network,4);
        jsonDoc["PoolDiff"] = formatNumber(g_nmaxe.mstatus.diff.pool,4);
        jsonDoc["LastDiff"] = formatNumber(g_nmaxe.mstatus.diff.last,4);
        jsonDoc["BestDiff"] = formatNumber(g_nmaxe.mstatus.diff.best_session,4) + "\r" + formatNumber(g_nmaxe.mstatus.diff.best_ever,4);
        jsonDoc["Valid"] = g_nmaxe.mstatus.block_hits;
        jsonDoc["Temp"] = g_nmaxe.temp.asic;
        jsonDoc["RSSI"] = g_nmaxe.connection.wifi.status_param.rssi;
        jsonDoc["FreeHeap"] = ESP.getFreeHeap() / 1024.0f;
        jsonDoc["Uptime"] = convert_uptime_to_string(g_nmaxe.mstatus.uptime_session) + "\r" + convert_uptime_to_string(g_nmaxe.mstatus.uptime_ever);
        jsonDoc["Version"] = g_nmaxe.board.fw_version;
        jsonDoc["BoardType"] = g_nmaxe.board.hw_model;
        jsonDoc["Power"]     = String(g_nmaxe.board.vbus*g_nmaxe.board.ibus/1000.0/1000.0, 1) + "W";
        jsonDoc["PoolInUse"] = String(g_nmaxe.connection.pool_use.url) + ":" + String(g_nmaxe.connection.pool_use.port);
        static uint32_t last_seen = millis();
        jsonDoc["Lastseen"]  = millis() - last_seen;
        last_seen = millis();

        memset(jsonbuf, 0, sizeof(jsonbuf));
        size_t n = serializeJson(jsonDoc, jsonbuf);
        //broadcast status to udp
        udp_client->beginPacket(UDP_BOARDCAST_ADDR, UDP_BOARDCAST_PORT);
        udp_client->write((uint8_t*)jsonbuf, n);
        udp_client->endPacket();

        //add self to swarm list
        g_nmaxe.swarm[g_nmaxe.connection.wifi.status_param.ip.toString()] = String(jsonbuf);
      }
    }
  }
}