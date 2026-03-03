#include "app/application.h"

void setup() {
    auto& app = MinerApp::instance();
    app.init();
    app.begin();
}

void loop() {
#if 0
  // for testing only: simulate block hits every 20s
  static uint32_t cnt = 1;
  if(cnt++ % 20 == 0){
    g_board.status.miner.hits++;
  }
#endif


    delay(1000);
}
