// What: Phase-1 BM1370 driver implementation for the new framework.
// Why: Before mining logic is migrated, the framework still needs one place
// that owns BM1370 UART wiring and reset-pin configuration.
// Role: Prepares the serial transport and reset line, but intentionally does
// not yet send BM1370 mining protocol commands.
// Benefit: Gamma now exposes a real chip-specific driver without forcing the
// old mining stack or BM protocol details back into application startup.
#include "drivers/asic/bm1370/bm1370.h"

#include <Arduino.h>

namespace nm::drivers {

bool Bm1370Asic::init() {
    if (_initialized) {
        return true;
    }

    if (!_configure_uart()) {
        return false;
    }

    _configure_reset_pin();
    _clear_port_cache();

    Serial.printf(
        "[asic.bm1370] transport ready name=%s init_baud=%lu work_baud=%lu rx=%d tx=%d rst=%d\n",
        name(),
        static_cast<unsigned long>(_config.init_baud),
        static_cast<unsigned long>(_config.work_baud),
        static_cast<int>(_config.port->rx_pin()),
        static_cast<int>(_config.port->tx_pin()),
        static_cast<int>(_config.reset_pin));

    // TODO(agent): migrate the old BM1370 command protocol here when the
    // mining service layer is introduced. At this stage the driver only owns
    // the physical transport and reset line.
    _initialized = true;
    return true;
}

bool Bm1370Asic::_configure_uart() {
    if (_config.port == nullptr) {
        return false;
    }

    return _config.port->init(_config.init_baud, 20);
}

void Bm1370Asic::_configure_reset_pin() {
    if (_config.reset_pin < 0) {
        return;
    }

    pinMode(_config.reset_pin, OUTPUT);
    digitalWrite(_config.reset_pin, HIGH);
}

void Bm1370Asic::_clear_port_cache() {
    if (_config.port == nullptr) {
        return;
    }

    _config.port->clear_rx();
}

}  // namespace nm::drivers
