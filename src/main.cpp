#include <Arduino.h>

#include "app/application.h"

void setup() {
    nm::Application::instance().setup();
}

void loop() {
    nm::Application::instance().loop();
    delay(10);
}
