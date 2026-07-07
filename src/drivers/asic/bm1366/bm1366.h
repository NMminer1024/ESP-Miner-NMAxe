// What: Phase-1 BM1366 ASIC driver entry for the new BSP-first framework.
// Why: NMAxe uses BM1366 silicon, so the board layer needs its own chip-specific
// transport owner instead of impersonating Gamma's BM1370 path.
// Role: Mirrors the current BM1370 phase-1 contract: UART bring-up, reset,
// probe-count, and minimal standby bring-up state publication.
// Benefit: `BOARD_NMAXE` can now boot through its own BSP and product identity
// while the full BM1366 command migration is still pending.
#pragma once

#include <stdint.h>

#include "drivers/asic/asic.h"
#include "hal/uart/uart_port.h"

namespace nm::drivers {

struct Bm1366UartConfig {
    hal::uart::UartPort* port = nullptr;
    uint32_t init_baud = 115200;
    uint32_t work_baud = 1000000;
    int8_t reset_pin = -1;

    Bm1366UartConfig() = default;
    Bm1366UartConfig(
        hal::uart::UartPort* port_value,
        uint32_t init_baud_value,
        uint32_t work_baud_value,
        int8_t reset_pin_value)
        : port(port_value),
          init_baud(init_baud_value),
          work_baud(work_baud_value),
          reset_pin(reset_pin_value) {}
};

class Bm1366Asic final : public Asic {
public:
    Bm1366Asic(const char* asic_name, const Bm1366UartConfig& config)
        : _name(asic_name), _config(config) {}

    bool init() override;
    uint8_t probe_count() override;
    bool bringup(uint16_t target_freq_mhz, uint8_t expected_asic_count) override;
    const char* name() const override { return _name; }
    AsicStatus status() const override { return _status; }

private:
    void _reset_chip();
    bool _configure_uart();
    void _configure_reset_pin();
    void _clear_port_cache();
    size_t _send_raw(const uint8_t* data, size_t size);
    size_t _receive(uint8_t* data, size_t size, uint32_t timeout_ms);
    void _send_command_packet(uint8_t header, const uint8_t* data, uint8_t size);
    void _set_version_mask(uint32_t version_mask);

    const char* _name = "bm1366";
    Bm1366UartConfig _config{};
    AsicStatus _status{};
    bool _initialized = false;
};

}  // namespace nm::drivers
