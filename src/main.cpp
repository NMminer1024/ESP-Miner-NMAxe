#include "app/application.h"
#include "utils/reboot_log/reboot_log.h"

void setup() {
    // Persist the previous boot's reboot record before other tasks can touch reboot intent state.
    reboot_log_init();

    auto& app = MinerApp::instance();
    app.init();
    app.begin();
}

void loop() {
    delay(1000);
}
