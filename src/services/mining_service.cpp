// What: Concrete mining bring-up service for the current phase-1 architecture.
// Why: The project needs a real core-business skeleton before stratum/network
// code is migrated, but that skeleton should still remain hardware-agnostic.
// Role: Moves the active ASIC path from power-wait into ASIC standby using only
// abstract board drivers and shared runtime state.
// Benefit: Establishes the service seam that later mining executors, stratum,
// and web controls will plug into instead of touching BSPs directly.
#include "services/mining_service.h"

#include <Arduino.h>

namespace nm::services {

void MiningService::start(
    const bsp::Board& board,
    const config::AppConfig& config,
    state::RuntimeState& runtime,
    system::EventFlags& events) {
    _board = &board;
    _config = &config;
    _runtime = &runtime;
    _events = &events;

    runtime.mining.expected_asic_count = board.mining_profile().asic_count;
    runtime.mining.target_freq_mhz = config.mining.target_freq_mhz;
    runtime.mining.applied_freq_mhz = 0;
    runtime.mining.detected_asic_count = 0;
    runtime.mining.transport_ready = false;
    runtime.mining.bringup_complete = false;
    runtime.mining.last_transition_ms = millis();
    _last_probe_attempt_ms = 0;

    if (board.drivers().asic == nullptr) {
        _set_phase(state::MiningPhase::Disabled, "asic absent");
        return;
    }

    _set_phase(state::MiningPhase::WaitPower, "wait power");
}

void MiningService::poll() {
    if (_board == nullptr || _config == nullptr || _runtime == nullptr || _events == nullptr) {
        return;
    }

    // Phase-1 temporary scheduler:
    // Keep mining bring-up quick and non-blocking in the current single-loop
    // architecture. Real work submission, socket I/O, and share processing must
    // later move to dedicated tasks/executors rather than being added here.
    switch (_runtime->mining.phase) {
        case state::MiningPhase::Disabled:
        case state::MiningPhase::Running:
        case state::MiningPhase::Fault:
            _publish_asic_status();
            break;

        case state::MiningPhase::WaitPower:
            _publish_asic_status();
            // Legacy-compatible sequencing:
            // probe/count must run on PLL/VDD only, before Vcore is enabled.
            _set_phase(state::MiningPhase::Probe, "probe chips");
            break;

        case state::MiningPhase::Probe: {
            _publish_asic_status();

            const uint32_t now_ms = millis();
            if (_last_probe_attempt_ms != 0 && (now_ms - _last_probe_attempt_ms) < 1000) {
                break;
            }

            _last_probe_attempt_ms = now_ms;
            const uint8_t detected = _board->drivers().asic->probe_count();
            _publish_asic_status();

            if (detected == 0) {
                _set_phase(state::MiningPhase::Probe, "probe retry");
                break;
            }

            _runtime->mining.detected_asic_count = detected;

            if (_board->drivers().power != nullptr) {
                const uint32_t vbus_mv = _read_vbus_mv();
                if (_board->policies().vbus_min_required_mv > 0 &&
                    vbus_mv < _board->policies().vbus_min_required_mv) {
                    _set_phase(state::MiningPhase::WaitVbus, "wait vbus");
                    break;
                }

                _board->drivers().power->set_vcore_mv(_config->mining.target_vcore_mv);
                _board->drivers().power->set_rail_enabled(drivers::PowerRail::Vcore, true);
                _set_phase(state::MiningPhase::WaitVcore, "wait vcore");
            } else {
                _set_phase(state::MiningPhase::Bringup, "bringup asic");
            }
            break;
        }

        case state::MiningPhase::WaitVbus: {
            _publish_asic_status();

            // Legacy note:
            // the old multi-threaded flow also waited for WiFi before enabling
            // Vcore. This phase only restores the board-defined VBUS gate for
            // now because the new network-ready gate has not been migrated yet.
            // Do not fold this back into Probe/WaitVcore; keep it as the seam
            // where the future WiFi/system readiness policy will be reattached.
            const uint32_t vbus_mv = _read_vbus_mv();
            if (_board->policies().vbus_min_required_mv > 0 &&
                vbus_mv < _board->policies().vbus_min_required_mv) {
                break;
            }

            _board->drivers().power->set_vcore_mv(_config->mining.target_vcore_mv);
            _board->drivers().power->set_rail_enabled(drivers::PowerRail::Vcore, true);
            _set_phase(state::MiningPhase::WaitVcore, "wait vcore");
            break;
        }

        case state::MiningPhase::WaitVcore:
            _publish_asic_status();
            if (_power_ready()) {
                _set_phase(state::MiningPhase::Bringup, "bringup asic");
            }
            break;

        case state::MiningPhase::Bringup:
            if (!_board->drivers().asic->bringup(
                    _config->mining.target_freq_mhz,
                    _runtime->mining.detected_asic_count)) {
                _fail("asic bringup failed");
                break;
            }

            _publish_asic_status();
            _runtime->mining.applied_freq_mhz = _config->mining.target_freq_mhz;
            _set_phase(state::MiningPhase::Standby, "standby");
            break;

        case state::MiningPhase::Standby:
            _publish_asic_status();
            break;
    }
}

uint32_t MiningService::_read_vbus_mv() {
    if (_board == nullptr || _board->drivers().power == nullptr) {
        return 0;
    }

    const uint32_t vbus_mv = _board->drivers().power->read_vbus_mv();
    if (_runtime != nullptr) {
        _runtime->power.vbus_mv = vbus_mv;
    }
    return vbus_mv;
}

void MiningService::_set_phase(state::MiningPhase phase, const char* message) {
    if (_runtime == nullptr || _events == nullptr) {
        return;
    }

    if (_runtime->mining.phase == phase && _runtime->mining.message == message) {
        return;
    }

    _runtime->mining.phase = phase;
    _runtime->mining.message = message;
    _runtime->mining.last_transition_ms = millis();
    _events->set(system::Event::MiningStateChanged);
}

void MiningService::_publish_asic_status() {
    if (_board == nullptr || _runtime == nullptr || _board->drivers().asic == nullptr) {
        return;
    }

    const auto status = _board->drivers().asic->status();
    _runtime->mining.transport_ready = status.transport_ready;
    _runtime->mining.bringup_complete = status.bringup_complete;
    _runtime->mining.detected_asic_count = status.detected_asic_count;
    if (status.target_freq_mhz > 0) {
        _runtime->mining.applied_freq_mhz = status.target_freq_mhz;
    }
}

bool MiningService::_power_ready() const {
    if (_board == nullptr) {
        return false;
    }

    if (_board->drivers().power == nullptr) {
        return true;
    }

    return _board->drivers().power->is_vcore_ready();
}

void MiningService::_fail(const char* message) {
    _publish_asic_status();
    if (_runtime != nullptr) {
        _runtime->mining.bringup_complete = false;
    }
    _set_phase(state::MiningPhase::Fault, message);
}

}  // namespace nm::services
