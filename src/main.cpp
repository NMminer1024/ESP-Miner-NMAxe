// What: Arduino framework entry shim for the NMAxe application.
// Why: The framework requires global `setup()` and `loop()` symbols, but the
// real boot flow should live in project code instead of being scattered here.
// Role: Forwards control into `nm::Application`, which owns BSP init and UI boot.
// Benefit: Keeps the runtime entry stable while the internal architecture stays modular.
#include <Arduino.h>

#include "app/application.h"

void setup() {
    nm::Application::instance().setup();
}

void loop() {
    nm::Application::instance().loop();
}
