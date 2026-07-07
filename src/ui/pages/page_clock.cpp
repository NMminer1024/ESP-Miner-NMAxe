// What: Clock-page base implementation for the new UI framework.
// Why: The framework already has time-related config defaults and will later
// grow real RTC/SNTP state, so a dedicated page slot is needed now.
// Role: Shows the current time configuration and idle-tracking summary.
// Benefit: Future clock-page migration stays local to this page family.
#include "ui/pages/page_clock.h"

#include <Arduino.h>
#include <stdio.h>

namespace nm::ui {

void PageClockBase::create_clock_page(lv_obj_t* parent, const PageScaffoldMetrics& metrics) {
    create_scaffold(parent, metrics);
    set_title("Clock");
    set_subtitle("Time config");
    set_footer("TODO(agent): replace with legacy clock page");
}

void PageClockBase::render(const PageContext& context) {
    if (!is_created()) {
        return;
    }

    char line[128] = {};
    const uint32_t idle_s = (millis() - context.ui_state.last_activity_ms) / 1000u;

    set_title("Clock");
    set_subtitle("Time config");

    snprintf(line, sizeof(line), "timezone %s", context.config.time.timezone.c_str());
    set_line(0, line);

    snprintf(line, sizeof(line), "hour format %u", static_cast<unsigned>(context.config.time.hour_format));
    set_line(1, line);

    snprintf(line, sizeof(line), "date %s", context.config.time.date_format.c_str());
    set_line(2, line);

    snprintf(line, sizeof(line), "idle %lus", static_cast<unsigned long>(idle_s));
    set_line(3, line);

    set_line(4, "rtc / sntp wiring pending");
    set_footer(context.runtime.boot.ready ? "system-ready" : "system-booting");
}

}  // namespace nm::ui
