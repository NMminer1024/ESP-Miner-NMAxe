
#include "global.h"
#include "utils/logger/logger.h"
#include "drivers/displays/display.h"
#include "nvs/nvs_config.h"

void recover_factory_cb(void){
  static bool first = true;
  if(!first) return;
  xSemaphoreGive(g_board.status.recover_factory_xsem);
  first = false;
}

void force_config_cb(void){
  static bool first = true;
  if(!first) return;
  xSemaphoreGive(g_board.status.force_config_xsem);
  first = false;
}

// void silence_mode_cb(void){
//   static bool toggle = false;
//   static uint16_t last_brightness = g_board.status.preference.screen.brightness;
//   g_board.status.preference.screen.brightness = (toggle) ? 0 : last_brightness;
//   g_board.status.preference.led.sleep         = (toggle) ? true : false;
//   g_board.status.preference.led.sleep_last    = g_board.status.preference.led.sleep;
//   toggle = !toggle;
//   last_brightness = (g_board.status.preference.screen.brightness == 0) ? last_brightness : g_board.status.preference.screen.brightness;
//   xSemaphoreGive(g_board.status.brightness_update_xsem);
// }
