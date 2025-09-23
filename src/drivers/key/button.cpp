#include "OneButton.h"
#include "global.h"
#include "logger.h"
#include "monitor.h"
#include "display.h"
#include "nvs_config.h"

static void recover_factory_cb(void){
  LOG_W("Recover factory settings...");
  clear_g_board();
  delay(500);
  ESP.restart();
}

static void force_config_cb(void){
  LOG_W("Force config...");
  nvs_config_set_u8(NVS_CONFIG_FORCE_CONFIG, true);
  delay(500);
  ESP.restart();
}

static void silence_mode_cb(void){
  static bool toggle = false;
  g_board.info.preference.screen.brightness = (toggle) ? 0 : g_board.info.preference.screen.brightness_last;
  g_board.info.preference.led.sleep         = (toggle) ? true : false;
  g_board.info.preference.led.sleep_last    = g_board.info.preference.led.sleep;
  toggle = !toggle;
}

void button_thread_entry(void *args){
  char *name = (char*)malloc(20);
  strcpy(name, (char*)args);
  LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
  free(name);
  
  OneButton boot_btn(g_board.info.btn_spec.boot_pin, true);
  OneButton user_btn(g_board.info.btn_spec.user_pin, true);

  // link the boot button functions.
  boot_btn.attachClick(ui_switch_next_page_cb);
  boot_btn.attachDoubleClick(silence_mode_cb);
  boot_btn.attachLongPressStart(NULL);
  boot_btn.attachLongPressStop(NULL);
  boot_btn.attachDuringLongPress(force_config_cb);

  // link the user button functions.
  user_btn.attachClick(NULL);
  user_btn.attachDoubleClick(NULL);
  user_btn.attachLongPressStart(NULL);
  user_btn.attachLongPressStop(NULL);
  user_btn.attachDuringLongPress(recover_factory_cb);

  while (true){
    boot_btn.tick();
    user_btn.tick();
    delay(20);
  }
}