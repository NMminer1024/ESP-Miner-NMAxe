// What: BM1370 ASIC driver entry for the new BSP-first framework.
// Why: Gamma already knows it carries BM1370 silicon, so the board should bind
// a concrete BM1370 driver instead of a generic null placeholder.
// Role: Owns the BM1370 UART transport and reset-line bring-up needed before
// the mining protocol migration is completed.
// Benefit: The hardware identity is now explicit in the driver tree and BSP,
// while higher layers still depend only on the generic `Asic` abstraction.
#pragma once

#include <stdint.h>

#include "drivers/asic/asic.h"
#include "hal/uart/uart_port.h"

namespace nm::drivers {

struct Bm1370UartConfig {
    hal::uart::UartPort* port = nullptr;
    uint32_t init_baud = 115200;
    uint32_t work_baud = 1000000;
    int8_t reset_pin = -1;

    Bm1370UartConfig() = default;
    Bm1370UartConfig(
        hal::uart::UartPort* port_value,
        uint32_t init_baud_value,
        uint32_t work_baud_value,
        int8_t reset_pin_value)
        : port(port_value),
          init_baud(init_baud_value),
          work_baud(work_baud_value),
          reset_pin(reset_pin_value) {}
};

class Bm1370Asic final : public Asic {
public:
    Bm1370Asic(const char* asic_name, const Bm1370UartConfig& config)
        : _name(asic_name), _config(config) {}

    bool init() override;
    uint8_t probe_count() override;
    bool bringup(uint16_t target_freq_mhz, uint8_t expected_asic_count) override;
    const char* name() const override { return _name; }
    AsicStatus status() const override { return _status; }

    uint32_t init_baud() const { return _config.init_baud; }
    uint32_t work_baud() const { return _config.work_baud; }
    hal::uart::UartPort* port() const { return _config.port; }

private:
    void _reset_chip();
    bool _configure_uart();
    void _configure_reset_pin();
    void _clear_port_cache();
    size_t _send_raw(const uint8_t* data, size_t size);
    size_t _receive(uint8_t* data, size_t size, uint32_t timeout_ms);
    void _send_command_packet(uint8_t header, const uint8_t* data, uint8_t size);
    void _set_version_mask(uint32_t version_mask);

    const char* _name = "bm1370";
    Bm1370UartConfig _config{};
    AsicStatus _status{};
    bool _initialized = false;
};

}  // namespace nm::drivers
