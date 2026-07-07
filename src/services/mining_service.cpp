// What: Concrete mining bring-up service for the current phase-1 architecture.
// Why: The project needs a real core-business skeleton before stratum/network
// code is migrated, but that skeleton should still remain hardware-agnostic.
// Role: Moves the active ASIC path from power-wait into ASIC standby using only
// abstract board drivers and shared runtime state.
// Benefit: Establishes the service seam that later mining executors, stratum,
// and web controls will plug into instead of touching BSPs directly.
#include "services/mining_service.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>

#include "app/task_config.h"

namespace nm::services {

namespace {

const char* asic_family_name(bsp::AsicFamily family) {
    switch (family) {
        case bsp::AsicFamily::BM1366:
            return "bm1366";
        case bsp::AsicFamily::BM1370:
            return "bm1370";
        case bsp::AsicFamily::BM1373:
            return "bm1373";
        case bsp::AsicFamily::Unknown:
        default:
            return "asic";
    }
}

void format_temp_value(char* buffer, size_t buffer_size, float value_c) {
    if (buffer == nullptr || buffer_size == 0) {
        return;
    }

    if (isnan(value_c)) {
        snprintf(buffer, buffer_size, "NAN");
        return;
    }

    snprintf(buffer, buffer_size, "%.1f", static_cast<double>(value_c));
}

}  // namespace

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
    _fan_polarity_ran = false;
    _fan_polarity_task = nullptr;
    _fan_polarity_running = false;
    _fan_polarity_complete = false;
    _fan_polarity_inverted = false;
    _fan_polarity_rpm_50 = 0;
    _fan_polarity_rpm_100 = 0;
    _fan_self_test_ran = false;
    _fan_self_test_task = nullptr;
    _fan_self_test_running = false;
    _fan_self_test_complete = false;
    _fan_self_test_passed = false;
    _fan_self_test_rpm = 0;
    _fan_self_test_threshold = 0;
    _asic_result_message[0] = '\0';

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
            _set_boot_loading("ASIC probe   ", 40);
            _set_phase(state::MiningPhase::Probe, "probe chips");
            break;

        case state::MiningPhase::Probe: {
            _publish_asic_status();

            const uint32_t now_ms = millis();
            const uint32_t phase_elapsed_ms = now_ms - _runtime->mining.last_transition_ms;
            if (phase_elapsed_ms < 300u ||
                (_last_probe_attempt_ms != 0 && (now_ms - _last_probe_attempt_ms) < 1000u)) {
                static const char* const kAsicInit[] = {
                    "ASIC probe   ", "ASIC probe.  ", "ASIC probe.. ", "ASIC probe..."
                };
                const uint8_t anim = static_cast<uint8_t>((phase_elapsed_ms / state::kBootMessageMinVisibleMs) % 4u);
                _set_boot_loading(kAsicInit[anim], 40);
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
            const char* chip_name = asic_family_name(_board->mining_profile().asic_family);
            if (detected > 1) {
                snprintf(
                    _asic_result_message,
                    sizeof(_asic_result_message),
                    "Found %u/%u %s",
                    static_cast<unsigned>(detected),
                    static_cast<unsigned>(_runtime->mining.expected_asic_count),
                    chip_name);
            } else {
                snprintf(
                    _asic_result_message,
                    sizeof(_asic_result_message),
                    "Found %u %s",
                    static_cast<unsigned>(detected),
                    chip_name);
            }
            _set_boot_loading(
                _asic_result_message,
                40,
                detected == _runtime->mining.expected_asic_count ? 0x00FF00 : 0xFF0000);
            _set_phase(state::MiningPhase::AsicConfirm, "asic counted");
            break;
        }

        case state::MiningPhase::AsicConfirm:
            _publish_asic_status();
            if (!state::boot_message_equals(_runtime->boot.message, _asic_result_message)) {
                _set_boot_loading(
                    _asic_result_message,
                    40,
                    _runtime->mining.detected_asic_count == _runtime->mining.expected_asic_count ? 0x00FF00 : 0xFF0000);
            }
            if (state::boot_message_equals(_runtime->boot.message, _asic_result_message) &&
                !_runtime->boot.pending_message_valid &&
                millis() - _runtime->boot.message_changed_ms >= 2000u) {
                _set_phase(state::MiningPhase::TempCheck, "Temp check");
            }
            break;

        case state::MiningPhase::TempCheck: {
            const uint32_t elapsed_ms = millis() - _runtime->mining.last_transition_ms;
            static const char* const kTempCheck[] = {
                "Temp check   ", "Temp check.  ", "Temp check.. ", "Temp check..."
            };
            float vcore_c = NAN;
            float asic_c = NAN;
            if (_board->drivers().temp != nullptr) {
                vcore_c = _board->drivers().temp->read_vcore_c();
                asic_c = _board->drivers().temp->read_asic_c();
                _runtime->thermal.vcore_c = vcore_c;
                _runtime->thermal.asic_c = asic_c;
            }
            const uint8_t anim = static_cast<uint8_t>((elapsed_ms / state::kBootMessageMinVisibleMs) % 4u);
            char vcore_text[12] = {};
            char asic_text[12] = {};
            format_temp_value(vcore_text, sizeof(vcore_text), vcore_c);
            format_temp_value(asic_text, sizeof(asic_text), asic_c);
            snprintf(_boot_message, sizeof(_boot_message), "%s %s/%s", kTempCheck[anim], vcore_text, asic_text);
            _set_boot_loading(_boot_message, 50);
            const bool temp_check_passed =
                _board->drivers().temp != nullptr && !isnan(vcore_c) && !isnan(asic_c);
            if (temp_check_passed) {
                _runtime->thermal.ready = true;
                snprintf(_boot_message, sizeof(_boot_message), "Temp OK %s/%s", vcore_text, asic_text);
                _set_boot_loading(_boot_message, 50, 0x00FF00);
                _set_phase(state::MiningPhase::TempConfirm, "temp pass");
                break;
            }

            if (elapsed_ms >= 5000u) {
                const bool blink = (((elapsed_ms / 500u) & 1u) == 0u);
                _runtime->thermal.ready = false;
                snprintf(_boot_message, sizeof(_boot_message), "Temp ERR %s/%s", vcore_text, asic_text);
                _set_boot_loading(_boot_message, 50, blink ? 0xFF0000 : 0xFFFFFF);
            }
            break;
        }

        case state::MiningPhase::TempConfirm:
            _set_boot_loading(_boot_message, 50, 0x00FF00);
            if (millis() - _runtime->mining.last_transition_ms >= 700u &&
                !_runtime->boot.pending_message_valid) {
                _set_phase(state::MiningPhase::FanPolarityCheck, "fan polarity");
            }
            break;

        case state::MiningPhase::FanPolarityCheck: {
            const uint32_t elapsed_ms = millis() - _runtime->mining.last_transition_ms;
            static const char* const kFanPolarity[] = {
                "Fan polarity check   ", "Fan polarity check.  ",
                "Fan polarity check.. ", "Fan polarity check..."
            };
            const uint8_t anim = static_cast<uint8_t>((elapsed_ms / state::kBootMessageMinVisibleMs) % 4u);
            _set_boot_loading(kFanPolarity[anim], 50);
            if (elapsed_ms < state::kBootMessageMinVisibleMs) {
                break;
            }

            if (!_fan_polarity_ran) {
                _start_fan_polarity_task();
                break;
            }

            if (!_fan_polarity_complete) {
                break;
            }

            _set_boot_loading("Fan polarity pass!", 50, 0x00FF00);
            _set_phase(state::MiningPhase::FanPolarityConfirm, "fan polarity pass");
            break;
        }

        case state::MiningPhase::FanPolarityConfirm:
            _set_boot_loading("Fan polarity pass!", 50, 0x00FF00);
            if (state::boot_message_equals(_runtime->boot.message, "Fan polarity pass!") &&
                !_runtime->boot.pending_message_valid &&
                millis() - _runtime->boot.message_changed_ms >= 1000u) {
                _set_phase(state::MiningPhase::FanSelfTest, "fan self-test");
            }
            break;

        case state::MiningPhase::FanSelfTest: {
            auto* fan = !_board->drivers().fans.empty() ? _board->drivers().fans[0] : nullptr;
            const uint16_t threshold = _fan_self_test_threshold != 0
                ? _fan_self_test_threshold
                : (fan != nullptr ? fan->self_test_rpm_threshold() : 0);
            const uint16_t rpm = _fan_self_test_rpm;
            const uint32_t elapsed_ms = millis() - _runtime->mining.last_transition_ms;
            static const char* const kFanTest[] = {
                "Fan test   ", "Fan test.  ", "Fan test.. ", "Fan test..."
            };
            const uint8_t anim = static_cast<uint8_t>((elapsed_ms / state::kBootMessageMinVisibleMs) % 4u);
            snprintf(_boot_message, sizeof(_boot_message), "%s%u/ %urpm", kFanTest[anim], rpm, threshold);
            _set_boot_loading(_boot_message, 50);

            if (elapsed_ms < state::kBootMessageMinVisibleMs) {
                break;
            }

            if (!_fan_self_test_ran) {
                _start_fan_self_test_task();
                break;
            }

            if (_fan_self_test_complete) {
                if (fan != nullptr) {
                    _runtime->fans[0].present = true;
                    _runtime->fans[0].self_test_passed = _fan_self_test_passed;
                    _runtime->fans[0].speed_percent = fan->speed_percent();
                    _runtime->fans[0].rpm = _fan_self_test_rpm;
                    _runtime->fan_count = 1;
                }
                snprintf(
                    _boot_message,
                    sizeof(_boot_message),
                    _fan_self_test_passed ? "Fan Pass! [%u/ %u rpm]" : "Fan Fail! [%u/ %u rpm]",
                    static_cast<unsigned>(_fan_self_test_rpm),
                    static_cast<unsigned>(threshold));
                _set_boot_loading(_boot_message, 50, _fan_self_test_passed ? 0x00FF00 : 0xFF0000);
                _set_phase(state::MiningPhase::FanSelfTestConfirm, "fan self-test pass");
            }
            break;
        }

        case state::MiningPhase::FanSelfTestConfirm:
            _set_boot_loading(
                _boot_message,
                50,
                _runtime->fan_count > 0 && !_runtime->fans[0].self_test_passed ? 0xFF0000 : 0x00FF00);
            if (millis() - _runtime->mining.last_transition_ms >= 1000u &&
                !_runtime->boot.pending_message_valid) {
                if (_board->drivers().power != nullptr) {
                    _set_phase(state::MiningPhase::WaitVcore, "wait vcore");
                } else {
                    _set_phase(state::MiningPhase::Bringup, "bringup asic");
                }
            }
            break;

        case state::MiningPhase::WaitVbus: {
            _publish_asic_status();

            // Kept only as a defensive fallback. The legacy loading state does
            // not return to VBUS after ASIC probe; normal flow gates low VBUS
            // inside WaitVcore while continuing to display "Vcore check...".
            const uint32_t vbus_mv = _read_vbus_mv();
            if (_board->policies().vbus_min_required_mv > 0 &&
                vbus_mv < _board->policies().vbus_min_required_mv) {
                snprintf(
                    _boot_message,
                    sizeof(_boot_message),
                    "Vbus %.1fv(at least%.1fv)",
                    static_cast<double>(vbus_mv) / 1000.0,
                    static_cast<double>(_board->policies().vbus_min_required_mv) / 1000.0);
                const uint32_t elapsed_ms = millis() - _runtime->mining.last_transition_ms;
                const bool blink = (((elapsed_ms / 500u) & 1u) == 0u);
                _set_boot_loading(_boot_message, 20, blink ? 0xFF0000 : 0xFFFFFF);
                break;
            }

            snprintf(
                _boot_message,
                sizeof(_boot_message),
                "Vbus %.1fv.",
                static_cast<double>(vbus_mv) / 1000.0);
            _set_boot_loading(_boot_message, 20, 0x00FF00);
            _board->drivers().power->set_vcore_mv(_config->mining.target_vcore_mv);
            _board->drivers().power->set_rail_enabled(drivers::PowerRail::Vcore, true);
            _set_phase(state::MiningPhase::WaitVcore, "wait vcore");
            break;
        }

        case state::MiningPhase::WaitVcore:
            _publish_asic_status();
            {
                static const char* const kVcoreCheck[] = {
                    "Vcore check   ", "Vcore check.  ", "Vcore check.. ", "Vcore check..."
                };
                const uint32_t elapsed_ms = millis() - _runtime->mining.last_transition_ms;
                const uint8_t anim = static_cast<uint8_t>((elapsed_ms / state::kBootMessageMinVisibleMs) % 4u);
                _set_boot_loading(kVcoreCheck[anim], 60);
            }
            if (_board->drivers().power != nullptr) {
                const uint32_t vbus_mv = _read_vbus_mv();
                if (_board->policies().vbus_min_required_mv > 0 &&
                    vbus_mv < _board->policies().vbus_min_required_mv) {
                    break;
                }

                _board->drivers().power->set_vcore_mv(_config->mining.target_vcore_mv);
                _board->drivers().power->set_rail_enabled(drivers::PowerRail::Vcore, true);
            }
            if (_power_ready()) {
                uint32_t vcore_mv = _runtime->power.vcore_mv;
                if (_board->drivers().power != nullptr) {
                    vcore_mv = _board->drivers().power->read_vcore_mv();
                    _runtime->power.vcore_mv = vcore_mv;
                    _runtime->power.vcore_ready = true;
                }
                snprintf(
                    _boot_message,
                    sizeof(_boot_message),
                    "Vcore %.3fv.",
                    static_cast<double>(vcore_mv) / 1000.0);
                _set_boot_loading(_boot_message, 60, 0x00FF00);
                _set_phase(state::MiningPhase::WaitVcoreConfirm, "vcore ready");
            }
            break;

        case state::MiningPhase::WaitVcoreConfirm:
            _set_boot_loading(_boot_message, 60, 0x00FF00);
            if (millis() - _runtime->mining.last_transition_ms >= 1000u &&
                !_runtime->boot.pending_message_valid) {
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
            _set_phase(state::MiningPhase::PoolConnect, "pool connect");
            break;

        case state::MiningPhase::PoolConnect: {
            _publish_asic_status();
            const uint32_t elapsed_ms = millis() - _runtime->mining.last_transition_ms;
            const bool blink = (((elapsed_ms / 500u) & 1u) == 0u);

            if (_runtime->stratum.subscribed) {
                _set_boot_loading("Pool connected!", 75, 0x00FF00);
                _set_phase(state::MiningPhase::PoolConnectConfirm, "pool connected");
                break;
            }

            if (_runtime->stratum.last_error[0] != '\0') {
                _set_boot_loading(_runtime->stratum.last_error, 75, blink ? 0xFFFFFF : 0xFF0000);
                break;
            }

            static const char* const kPoolConnect[] = {
                "Pool connect   ", "Pool connect.  ", "Pool connect.. ", "Pool connect..."
            };
            const uint8_t anim = static_cast<uint8_t>((elapsed_ms / state::kBootMessageMinVisibleMs) % 4u);
            snprintf(
                _boot_message,
                sizeof(_boot_message),
                "%s[%s]",
                kPoolConnect[anim],
                _runtime->stratum.ssl ? "ssl" : "tcp");
            _set_boot_loading(_boot_message, 75);
            break;
        }

        case state::MiningPhase::PoolConnectConfirm:
            _set_boot_loading("Pool connected!", 75, 0x00FF00);
            if (state::boot_message_equals(_runtime->boot.message, "Pool connected!") &&
                !_runtime->boot.pending_message_valid &&
                millis() - _runtime->boot.message_changed_ms >= state::kBootMessageMinVisibleMs) {
                _set_phase(state::MiningPhase::PoolAuth, "pool auth");
            }
            break;

        case state::MiningPhase::PoolAuth: {
            _publish_asic_status();
            const uint32_t elapsed_ms = millis() - _runtime->mining.last_transition_ms;
            const bool blink = (((elapsed_ms / 500u) & 1u) == 0u);

            if (_runtime->stratum.authorized) {
                _set_boot_loading("Pool authorized!", 85, 0x00FF00);
                _set_phase(state::MiningPhase::PoolAuthConfirm, "pool authorized");
                break;
            }

            if (elapsed_ms >= 6000u) {
                _set_boot_loading("Wrong stratum user!", 85, blink ? 0xFFFFFF : 0xFF0000);
                break;
            }

            static const char* const kPoolAuth[] = {
                "Pool auth   ", "Pool auth.  ", "Pool auth.. ", "Pool auth..."
            };
            const uint8_t anim = static_cast<uint8_t>((elapsed_ms / state::kBootMessageMinVisibleMs) % 4u);
            _set_boot_loading(kPoolAuth[anim], 85);
            break;
        }

        case state::MiningPhase::PoolAuthConfirm:
            _set_boot_loading("Pool authorized!", 85, 0x00FF00);
            if (state::boot_message_equals(_runtime->boot.message, "Pool authorized!") &&
                !_runtime->boot.pending_message_valid &&
                millis() - _runtime->boot.message_changed_ms >= state::kBootMessageMinVisibleMs) {
                _set_phase(state::MiningPhase::PoolJob, "pool job");
            }
            break;

        case state::MiningPhase::PoolJob: {
            _publish_asic_status();
            const uint32_t elapsed_ms = millis() - _runtime->mining.last_transition_ms;
            const bool blink = (((elapsed_ms / 500u) & 1u) == 0u);

            if (_runtime->stratum.job_counter > 0 || _runtime->stratum.job_received) {
                _set_boot_loading("Miner ready!", 100, 0x00FF00);
                _set_phase(state::MiningPhase::ReadyConfirm, "miner ready");
                break;
            }

            if (elapsed_ms >= 60000u) {
                _set_boot_loading("Pool job timeout!", 100, blink ? 0xFFFFFF : 0xFF0000);
                break;
            }

            static const char* const kWaitJob[] = {
                "Waiting pool job   ", "Waiting pool job.  ",
                "Waiting pool job.. ", "Waiting pool job..."
            };
            const uint8_t anim = static_cast<uint8_t>((elapsed_ms / state::kBootMessageMinVisibleMs) % 4u);
            _set_boot_loading(kWaitJob[anim], 100);
            break;
        }

        case state::MiningPhase::ReadyConfirm:
            _publish_asic_status();
            _set_boot_loading("Miner ready!", 100, 0x00FF00);
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

void MiningService::_set_boot_loading(const char* message, uint8_t progress_percent, uint32_t message_color) {
    if (_runtime == nullptr || _events == nullptr) {
        return;
    }

    state::publish_boot_state(
        _runtime->boot,
        _runtime->boot.phase,
        message,
        progress_percent,
        message_color,
        millis());
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

void MiningService::_start_fan_polarity_task() {
    if (_board == nullptr) {
        return;
    }

    auto* fan = !_board->drivers().fans.empty() ? _board->drivers().fans[0] : nullptr;
    _fan_polarity_ran = true;
    _fan_polarity_running = true;
    _fan_polarity_complete = false;
    _fan_polarity_inverted = false;
    _fan_polarity_rpm_50 = 0;
    _fan_polarity_rpm_100 = 0;

    if (fan == nullptr) {
        _fan_polarity_complete = true;
        _fan_polarity_running = false;
        return;
    }

    const BaseType_t ok = xTaskCreatePinnedToCore(
        _fan_polarity_task_entry,
        "(fan_pol)",
        app::kAppServiceTaskStackBytes,
        this,
        app::kTaskPriorityMonitor,
        &_fan_polarity_task,
        app::kTaskCoreUi);
    if (ok != pdPASS) {
        _fan_polarity_task = nullptr;
        _fan_polarity_complete = true;
        _fan_polarity_running = false;
    }
}

void MiningService::_start_fan_self_test_task() {
    if (_board == nullptr) {
        return;
    }

    auto* fan = !_board->drivers().fans.empty() ? _board->drivers().fans[0] : nullptr;
    _fan_self_test_ran = true;
    _fan_self_test_running = true;
    _fan_self_test_complete = false;
    _fan_self_test_passed = false;
    _fan_self_test_rpm = 0;
    _fan_self_test_threshold = fan != nullptr ? fan->self_test_rpm_threshold() : 0;

    if (fan == nullptr) {
        _fan_self_test_passed = true;
        _fan_self_test_complete = true;
        _fan_self_test_running = false;
        return;
    }

    const BaseType_t ok = xTaskCreatePinnedToCore(
        _fan_self_test_task_entry,
        "(fan_test)",
        app::kAppServiceTaskStackBytes,
        this,
        app::kTaskPriorityMonitor,
        &_fan_self_test_task,
        app::kTaskCoreUi);
    if (ok != pdPASS) {
        _fan_self_test_task = nullptr;
        _fan_self_test_passed = false;
        _fan_self_test_complete = true;
        _fan_self_test_running = false;
    }
}

void MiningService::_fan_polarity_task_entry(void* args) {
    auto* self = static_cast<MiningService*>(args);
    if (self == nullptr || self->_board == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    auto* fan = !self->_board->drivers().fans.empty() ? self->_board->drivers().fans[0] : nullptr;
    drivers::FanPolarityDetectResult result{};
    if (fan != nullptr) {
        result = fan->detect_polarity();
    }

    self->_fan_polarity_inverted = result.inverted;
    self->_fan_polarity_rpm_50 = result.rpm_50;
    self->_fan_polarity_rpm_100 = result.rpm_100;
    self->_fan_polarity_running = false;
    self->_fan_polarity_complete = true;
    self->_fan_polarity_task = nullptr;
    vTaskDelete(nullptr);
}

void MiningService::_fan_self_test_task_entry(void* args) {
    auto* self = static_cast<MiningService*>(args);
    if (self == nullptr || self->_board == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    auto* fan = !self->_board->drivers().fans.empty() ? self->_board->drivers().fans[0] : nullptr;
    drivers::FanSelfTestResult result{true, 0};
    if (fan != nullptr) {
        result = fan->run_self_test(_fan_self_test_progress, self);
    }

    self->_fan_self_test_rpm = result.rpm;
    self->_fan_self_test_passed = result.passed;
    self->_fan_self_test_running = false;
    self->_fan_self_test_complete = true;
    self->_fan_self_test_task = nullptr;
    vTaskDelete(nullptr);
}

void MiningService::_fan_self_test_progress(uint16_t rpm, void* ctx) {
    auto* self = static_cast<MiningService*>(ctx);
    if (self == nullptr) {
        return;
    }
    self->_fan_self_test_rpm = rpm;
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
