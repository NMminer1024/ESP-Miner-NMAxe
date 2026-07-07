// What: Miner-page base implementation for the new UI framework.
// Why: The app already publishes mining phase and probe state through
// `RuntimeState`, so the miner page can now bind to those abstractions.
// Role: Shows a lightweight miner-state summary on the shared scaffold.
// Benefit: The mining UI now has a stable slot in the page tree before the
// legacy assets and exact coordinates are migrated.
#include "ui/pages/page_miner.h"

#include <stdio.h>

namespace nm::ui {

namespace {

const char* mining_phase_text(state::MiningPhase phase) {
    switch (phase) {
        case state::MiningPhase::Disabled:
            return "disabled";
        case state::MiningPhase::WaitPower:
            return "wait-power";
        case state::MiningPhase::Probe:
            return "probe";
        case state::MiningPhase::WaitVbus:
            return "wait-vbus";
        case state::MiningPhase::WaitVcore:
            return "wait-vcore";
        case state::MiningPhase::Bringup:
            return "bringup";
        case state::MiningPhase::Standby:
            return "standby";
        case state::MiningPhase::Running:
            return "running";
        case state::MiningPhase::Fault:
            return "fault";
    }

    return "unknown";
}

}  // namespace

void PageMinerBase::create_miner_page(lv_obj_t* parent, const PageScaffoldMetrics& metrics) {
    create_scaffold(parent, metrics);
    set_title("Miner");
    set_subtitle("Runtime state");
    set_footer("TODO(agent): replace with legacy miner page");
}

void PageMinerBase::render(const PageContext& context) {
    if (!is_created()) {
        return;
    }

    char line[128] = {};
    set_title("Miner");
    set_subtitle(mining_phase_text(context.runtime.mining.phase));

    snprintf(
        line,
        sizeof(line),
        "target %uMHz %umV",
        static_cast<unsigned>(context.config.mining.target_freq_mhz),
        static_cast<unsigned>(context.config.mining.target_vcore_mv));
    set_line(0, line);

    snprintf(line, sizeof(line), "applied %uMHz", static_cast<unsigned>(context.runtime.mining.applied_freq_mhz));
    set_line(1, line);

    snprintf(
        line,
        sizeof(line),
        "asic detected %u/%u",
        static_cast<unsigned>(context.runtime.mining.detected_asic_count),
        static_cast<unsigned>(context.runtime.mining.expected_asic_count));
    set_line(2, line);

    snprintf(line, sizeof(line), "transport %s", context.runtime.mining.transport_ready ? "ready" : "wait");
    set_line(3, line);

    snprintf(
        line,
        sizeof(line),
        "%s %u x %u",
        context.board.traits().board_name != nullptr ? context.board.traits().board_name : context.board.key(),
        static_cast<unsigned>(context.board.mining_profile().asic_family),
        static_cast<unsigned>(context.board.mining_profile().asic_count));
    set_line(4, line);

    set_footer(context.runtime.mining.message);
}

}  // namespace nm::ui
