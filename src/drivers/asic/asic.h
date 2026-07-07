// What: Minimal ASIC control abstraction for mining-capable BSPs.
// Why: The framework needs a typed hook for the active ASIC path even before the
// real mining implementation is fully wired.
// Role: Defines the board-exported interface for ASIC subsystem ownership.
// Benefit: Lets the BSP reserve a clean seam for mining logic while keeping
// application and UI layers decoupled from ASIC-specific details.
#pragma once

#include <Arduino.h>
#include <esp_err.h>
#include <stdint.h>

namespace nm::drivers {

struct AsicJob {
    uint8_t id = 0;
    uint8_t num_midstates = 0;
    uint8_t starting_nonce[4] = {};
    uint8_t nbits[4] = {};
    uint8_t ntime[4] = {};
    uint8_t merkle_root[32] = {};
    uint8_t prev_block_hash[32] = {};
    uint8_t version[4] = {};
};

struct __attribute__((__packed__)) AsicRawResult {
    uint8_t preamble[2];
    uint32_t nonce;
    uint8_t midstate_num;
    uint8_t job_id;
    uint16_t version;
    uint8_t crc;
};

struct __attribute__((__packed__)) MinerResult {
    AsicRawResult asic;
    uint8_t asic_id = 0;
};

struct AsicStatus {
    bool transport_ready = false;
    bool bringup_complete = false;
    uint8_t detected_asic_count = 0;
    uint16_t target_freq_mhz = 0;
    uint32_t current_difficulty = 0;
};

class Asic {
public:
    virtual ~Asic() = default;
    virtual bool init() = 0;
    virtual uint8_t probe_count() = 0;
    virtual bool bringup(uint16_t target_freq_mhz, uint8_t expected_asic_count, uint32_t initial_difficulty) = 0;
    virtual uint32_t set_job_difficulty(uint32_t difficulty) = 0;
    virtual uint32_t current_difficulty() const = 0;
    virtual bool send_work(const AsicJob& job) = 0;
    virtual esp_err_t wait_for_result(MinerResult& result, uint32_t timeout_ms) = 0;
    virtual bool clear_port_cache() = 0;
    virtual const char* name() const = 0;
    virtual AsicStatus status() const = 0;
};

// Temporary skeleton-only fallback.
// TODO(agent): remove this class after each active BSP is wired to a real ASIC driver.
class NullAsic final : public Asic {
public:
    explicit NullAsic(const char* asic_name) : _name(asic_name) {}

    bool init() override { return true; }
    uint8_t probe_count() override { return 0; }
    bool bringup(uint16_t, uint8_t, uint32_t) override { return false; }
    uint32_t set_job_difficulty(uint32_t) override { return 0; }
    uint32_t current_difficulty() const override { return 0; }
    bool send_work(const AsicJob&) override { return false; }
    esp_err_t wait_for_result(MinerResult&, uint32_t) override { return ESP_ERR_NOT_SUPPORTED; }
    bool clear_port_cache() override { return true; }
    const char* name() const override { return _name; }
    AsicStatus status() const override { return _status; }

private:
    const char* _name = "null-asic";
    AsicStatus _status{};
};

}  // namespace nm::drivers
