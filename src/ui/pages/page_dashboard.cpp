// What: Dashboard-page base implementation for the new UI framework.
// Why: Dashboard content is a cross-cut of power, thermal, and cooling state
// that should not be hard-coded inside the UI runtime.
// Role: Summarizes telemetry using the shared scaffold layout.
// Benefit: Keeps the future dashboard migration bounded to one page family.
#include "ui/pages/page_dashboard.h"

#include <stdio.h>

namespace nm::ui {

void PageDashboardBase::create_dashboard_page(lv_obj_t* parent, const PageScaffoldMetrics& metrics) {
    create_scaffold(parent, metrics);
    set_title("Dashboard");
    set_subtitle("Power / thermal");
    set_footer("TODO(agent): replace with legacy dashboard page");
}

void PageDashboardBase::render(const PageContext& context) {
    if (!is_created()) {
        return;
    }

    char line[128] = {};
    set_title("Dashboard");
    set_subtitle("Power / thermal");

    snprintf(
        line,
        sizeof(line),
        "vbus %.2fV  ibus %.2fA",
        context.runtime.power.vbus_mv / 1000.0f,
        context.runtime.power.ibus_ma / 1000.0f);
    set_line(0, line);

    snprintf(
        line,
        sizeof(line),
        "vcore %lumV  %s",
        static_cast<unsigned long>(context.runtime.power.vcore_mv),
        context.runtime.power.vcore_ready ? "ready" : "wait");
    set_line(1, line);

    snprintf(line, sizeof(line), "power %lumW", static_cast<unsigned long>(context.runtime.power.power_mw));
    set_line(2, line);

    snprintf(
        line,
        sizeof(line),
        "vrm %.1fC  asic %.1fC",
        context.runtime.thermal.vcore_c,
        context.runtime.thermal.asic_c);
    set_line(3, line);

    if (context.runtime.fan_count > 0u && context.runtime.fans[0].present) {
        snprintf(
            line,
            sizeof(line),
            "fan0 %u%% %urpm",
            static_cast<unsigned>(context.runtime.fans[0].speed_percent),
            static_cast<unsigned>(context.runtime.fans[0].rpm));
    } else {
        snprintf(line, sizeof(line), "fan0 not present");
    }
    set_line(4, line);

    snprintf(
        line,
        sizeof(line),
        "adc:%s dc:%s",
        context.runtime.power.adc_ready ? "yes" : "no",
        context.runtime.power.dc_plugged ? "yes" : "no");
    set_footer(line);
}

}  // namespace nm::ui
