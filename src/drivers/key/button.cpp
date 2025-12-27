
#include "global.h"
#include "logger.h"
#include "display.h"
#include "nvs_config.h"

void recover_factory_cb(void){
  LOG_W("Recover factory settings...");
  clear_g_board();
  delay(500);
  ESP.restart();
}

void force_config_cb(void){
  LOG_W("Force config...");
  nvs_config_set_u8(NVS_CONFIG_FORCE_CONFIG, true);
  delay(500);
  ESP.restart();
}

void silence_mode_cb(void){
  static bool toggle = false;
  static uint16_t last_brightness = g_board.info.preference.screen.brightness;
  g_board.info.preference.screen.brightness = (toggle) ? 0 : last_brightness;
  g_board.info.preference.led.sleep         = (toggle) ? true : false;
  g_board.info.preference.led.sleep_last    = g_board.info.preference.led.sleep;
  toggle = !toggle;
  last_brightness = (g_board.info.preference.screen.brightness == 0) ? last_brightness : g_board.info.preference.screen.brightness;
  xSemaphoreGive(g_board.status.brightness_xsem);
}
