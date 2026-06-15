#include "app/application.h"

void setup() {
    auto& app = MinerApp::instance();
    app.init();
    app.begin();
}

void loop() {
    delay(1000);
}
