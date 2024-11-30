#include "OneButton.h"
#include "global.h"
#include "logger.h"
#include "monitor.h"
#include "display.h"
#include "axe_nvs_config.h"

OneButton boot_btn(NM_AXE_BUTTON_BOOT_PIN, true);
OneButton user_btn(NM_AXE_BUTTON_USER_PIN, true);


static void recover_factory_cb(void){
  LOG_W("Recover factory settings...");
  clear_g_nmaxe();
  delay(500);
  ESP.restart();
}

static void force_config_cb(void){
  LOG_W("Force config...");
  nvs_config_set_u8(NVS_CONFIG_FORCE_CONFIG, true);
  delay(500);
  ESP.restart();
}

void button_thread_entry(void *args){
  char *name = (char*)malloc(20);
  strcpy(name, (char*)args);
  LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
  free(name);
  
  // link the boot button functions.
  boot_btn.attachClick(NULL);
  boot_btn.attachDoubleClick(NULL);
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
    delay(100);
  }
}