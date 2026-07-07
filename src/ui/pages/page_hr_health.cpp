// What: HR-health page base implementation for the new UI framework.
// Why: The exact legacy hashrate/health UI is not migrated yet, but the page
// slot and data boundary should exist now.
// Role: Shows the best currently available health-oriented runtime summary.
// Benefit: Later mining telemetry additions can target this page abstraction
// instead of reworking routing or layout registration again.
#include "ui/pages/page_hr_health.h"

#include <stdio.h>

namespace nm::ui {

void PageHrHealthBase::create_hr_health_page(lv_obj_t* parent, const PageScaffoldMetrics& metrics) {
    create_scaffold(parent, metrics);
    set_title("HR Health");
    set_subtitle("Mining health");
    set_footer("TODO(agent): replace with legacy hr-health page");
}

void PageHrHealthBase::render(const PageContext& context) {
    if (!is_created()) {
        return;
    }

    char line[128] = {};
    set_title("HR Health");
    set_subtitle("Mining health");

    set_line(0, "hashrate telemetry pending");

    snprintf(
        line,
        sizeof(line),
        "probe %u/%u",
        static_cast<unsigned>(context.runtime.mining.detected_asic_count),
        static_cast<unsigned>(context.runtime.mining.expected_asic_count));
    set_line(1, line);

    snprintf(
        line,
        sizeof(line),
        "transport %s  bringup %s",
        context.runtime.mining.transport_ready ? "ok" : "wait",
        context.runtime.mining.bringup_complete ? "ok" : "wait");
    set_line(2, line);

    snprintf(
        line,
        sizeof(line),
        "thermal %s  buttons %u",
        context.runtime.thermal.ready ? "ready" : "wait",
        static_cast<unsigned>(context.runtime.button_count));
    set_line(3, line);

    snprintf(line, sizeof(line), "fans %u", static_cast<unsigned>(context.runtime.fan_count));
    set_line(4, line);

    set_footer(context.runtime.mining.message);
}

}  // namespace nm::ui
