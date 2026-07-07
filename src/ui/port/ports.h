// What: Public UI port binding surface for display and input devices.
// Why: UI boot should depend on generic "bind this board driver into LVGL"
// calls, not on the details of each LVGL driver registration step.
// Role: Declares the adapter functions that connect BSP drivers to the UI stack.
// Benefit: Keeps LVGL plumbing isolated in one layer and preserves a clean
// boundary between BSP hardware drivers and UI composition code.
#pragma once

#include "drivers/display/display.h"
#include "drivers/touch/touch.h"

namespace nm::ui::port {

bool bind_display(drivers::Display& display);
void bind_input(drivers::Touch* touch);
void poll();

}  // namespace nm::ui::port
