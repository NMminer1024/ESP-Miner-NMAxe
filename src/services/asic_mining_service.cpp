#include "services/asic_mining_service.h"

#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include <esp_heap_caps.h>
#include <limits>
#include <string.h>

#include "app/firmware_identity.h"
#include "app/task_config.h"
#include "utils/logger/logger.h"
#include "utils/sha/csha256.h"

namespace nm::services {

namespace {

constexpr uint32_t kRxTimeoutMs = 30000;
constexpr uint32_t kSummaryLogIntervalMs = 60000;
constexpr uint32_t kHashrateWindow3mMs = 3 * 60 * 1000u;
constexpr uint32_t kHashrateWindow30mMs = 30 * 60 * 1000u;
constexpr uint32_t kHashrateWindow60mMs = 60 * 60 * 1000u;

uint32_t read_le_u32(const uint8_t* bytes) {
    uint32_t value = 0;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

}  // namespace

void AsicMiningService::start(
    const bsp::Board& board,
    const config::AppConfig& config,
    state::RuntimeState& runtime,
    system::EventFlags& events,
    StratumService& stratum) {
    if (_started) {
        return;
    }
    if (board.drivers().asic == nullptr || !board.drivers().asic->status().bringup_complete) {
        LOG_E("[miner] ASIC mining start rejected, asic not ready");
        return;
    }

    if (_job_mutex == nullptr) {
        _job_mutex = xSemaphoreCreateMutex();
    }
    if (_job_mutex == nullptr) {
        LOG_E("[miner] job mutex create failed");
        return;
    }

    _board = &board;
    _config = &config;
    _runtime = &runtime;
    _events = &events;
    _stratum = &stratum;
    _stop_requested = false;
    _next_job_id = 0;
    _last_target_diff = 0.0;
    _hashrate_s3m = 0.0;
    _hashrate_s30m = 0.0;
    _hashrate_s60m = 0.0;
    _off_3m = 0;
    _off_30m = 0;
    _dedup_job_key = "";
    _submitted_nonces.clear();
    _clear_job_cache();

    const BaseType_t tx_ok = xTaskCreatePinnedToCore(
        _tx_task_entry,
        "(miner-tx)",
        app::kMinerTaskStackBytes,
        this,
        app::kTaskPriorityMinerTx,
        &_tx_task,
        app::kTaskCoreNet);
    const BaseType_t rx_ok = xTaskCreatePinnedToCore(
        _rx_task_entry,
        "(miner-rx)",
        app::kMinerTaskStackBytes,
        this,
        app::kTaskPriorityMinerRx,
        &_rx_task,
        app::kTaskCoreNet);

    if (tx_ok != pdPASS || rx_ok != pdPASS) {
        LOG_E("[miner] failed to create ASIC mining tasks");
        _stop_requested = true;
        if (_tx_task != nullptr) {
            vTaskDelete(_tx_task);
            _tx_task = nullptr;
        }
        if (_rx_task != nullptr) {
            vTaskDelete(_rx_task);
            _rx_task = nullptr;
        }
        return;
    }

    _started = true;
    runtime.mining.asic_mining_started = true;
    LOG_I("[miner] ASIC mining tasks started job_interval=%lums diff_init=%lu",
          static_cast<unsigned long>(board.mining_profile().job_interval_ms),
          static_cast<unsigned long>(board.mining_profile().initial_difficulty));
}

void AsicMiningService::stop() {
    _stop_requested = true;
    if (_board != nullptr && _board->drivers().asic != nullptr) {
        _board->drivers().asic->clear_port_cache();
    }
}

void AsicMiningService::_tx_task_entry(void* args) {
    auto* self = static_cast<AsicMiningService*>(args);
    if (self != nullptr) {
        self->_tx_loop();
    }
    vTaskDelete(nullptr);
}

void AsicMiningService::_rx_task_entry(void* args) {
    auto* self = static_cast<AsicMiningService*>(args);
    if (self != nullptr) {
        self->_rx_loop();
    }
    vTaskDelete(nullptr);
}

void AsicMiningService::_tx_loop() {
    PoolJobData current_job;

    while (!_stop_requested) {
        if (_stratum == nullptr || _board == nullptr || _board->drivers().asic == nullptr) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (xSemaphoreTake(_stratum->new_job_signal(), pdMS_TO_TICKS(1000)) != pdTRUE) {
            continue;
        }

        PoolJobData next_job;
        if (_stratum->pop_job(next_job)) {
            current_job = next_job;
        }
        if (current_job.id.isEmpty()) {
            continue;
        }

        const state::StratumTelemetry st = _stratum_snapshot();
        const double network_diff = _network_diff(current_job.nbits);
        _publish_diff(0.0, st.pool_difficulty, network_diff);
        LOG_W("Job [%s] from %s:%u",
              current_job.id.c_str(),
              st.host,
              static_cast<unsigned>(st.port));

        while (!_stop_requested) {
            const state::StratumTelemetry loop_st = _stratum_snapshot();
            if (!loop_st.subscribed || !loop_st.authorized) {
                _clear_job_cache();
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;
            }

            if (xSemaphoreTake(_stratum->clear_job_signal(), 0) == pdTRUE) {
                _clear_job_cache();
                _stratum->clear_extranonce2();
                LOG_D("Stratum job cache clear...");
            }

            if (!_build_and_send_job(current_job)) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

            const double target_diff = std::min(loop_st.pool_difficulty, static_cast<double>(_board->mining_profile().initial_difficulty));
            if (fabs(target_diff - _last_target_diff) > std::numeric_limits<double>::epsilon()) {
                const uint32_t applied = _board->drivers().asic->set_job_difficulty(static_cast<uint32_t>(target_diff));
                LOG_W("Change asic diff from [%.1f] to [%lu/%.1f] successfully",
                      _last_target_diff,
                      static_cast<unsigned long>(applied),
                      target_diff);
                _last_target_diff = target_diff;
            }

            const uint32_t interval_ms = _board->mining_profile().job_interval_ms > 0
                ? _board->mining_profile().job_interval_ms
                : 1000u;
            if (xSemaphoreTake(_stratum->new_job_signal(), pdMS_TO_TICKS(interval_ms)) == pdTRUE) {
                xSemaphoreGive(_stratum->new_job_signal());
                break;
            }
        }
    }

    _tx_task = nullptr;
    LOG_W("[miner] tx task stopped");
}

void AsicMiningService::_rx_loop() {
    while (!_stop_requested) {
        if (_stratum == nullptr || _board == nullptr || _board->drivers().asic == nullptr) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        const state::StratumTelemetry st = _stratum_snapshot();
        if (!st.subscribed || !st.authorized) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        drivers::MinerResult result{};
        const esp_err_t err = _board->drivers().asic->wait_for_result(result, kRxTimeoutMs);
        if (err != ESP_OK) {
            if (err == ESP_ERR_INVALID_SIZE) {
                LOG_W("Asic response size error.");
            } else if (err == ESP_ERR_TIMEOUT) {
                LOG_W("Asic response timeout.");
            } else if (err == ESP_ERR_INVALID_RESPONSE) {
                LOG_W("Asic response header error.");
            } else {
                LOG_W("Asic response error: %s", esp_err_to_name(err));
            }
            continue;
        }

        JobContext context;
        if (!_find_job(result.asic.job_id, context)) {
            continue;
        }

        const uint32_t version_bits = static_cast<uint32_t>(reverse_uint16(result.asic.version)) << 13;
        const uint32_t base_version = read_le_u32(context.job.version);
        const uint32_t version = version_bits | base_version;
        const double diff = _share_diff(
            version,
            context.job.prev_block_hash,
            context.job.merkle_root,
            read_le_u32(context.job.ntime),
            read_le_u32(context.job.nbits),
            result.asic.nonce);

        if (diff <= std::numeric_limits<double>::epsilon() || std::isnan(diff) || std::isinf(diff)) {
            continue;
        }
        if (diff < _board->drivers().asic->current_difficulty()) {
            continue;
        }
        if (context.pool_job_id.isEmpty() || context.extranonce2.isEmpty()) {
            continue;
        }

        const String cur_job_key = context.pool_job_id + context.extranonce2;
        if (cur_job_key != _dedup_job_key) {
            _submitted_nonces.clear();
            _dedup_job_key = cur_job_key;
        }
        if (_submitted_nonces.count(result.asic.nonce) > 0) {
            LOG_W("Dup nonce 0x%08lx skipped (pool_job=%s)",
                  static_cast<unsigned long>(result.asic.nonce),
                  context.pool_job_id.c_str());
            continue;
        }
        _submitted_nonces.insert(result.asic.nonce);
        _record_nonce(diff);

        const state::StratumTelemetry now_st = _stratum_snapshot();
        const double pool_diff = now_st.pool_difficulty;
        const double network_diff = context.network_diff;
        _publish_diff(diff, pool_diff, network_diff);
        LOG_D("ASIC[%u] nonce 0x%08lx",
              static_cast<unsigned>(result.asic_id),
              static_cast<unsigned long>(result.asic.nonce));

        if (diff < pool_diff) {
            continue;
        }

        _log_share_table(diff, pool_diff, network_diff, result.asic_id);

        const uint32_t version_submit = version ^ base_version;
        if (!_stratum->submit(
                context.pool_job_id,
                context.extranonce2,
                read_le_u32(context.job.ntime),
                result.asic.nonce,
                version_submit)) {
            continue;
        }

        if (network_diff > 0.0 && diff >= network_diff) {
            LOG_W("******************************* Your Are The Chosen One ********************************");
            LOG_I("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!BLOCK FOUND!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
            LOG_I("Nonce       : %08lx", static_cast<unsigned long>(result.asic.nonce));
            LOG_I("Version     : %08lx", static_cast<unsigned long>(version));
            LOG_I("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        }
    }

    _rx_task = nullptr;
    LOG_W("[miner] rx task stopped");
}

bool AsicMiningService::_build_and_send_job(const PoolJobData& pool_job) {
    if (_stratum == nullptr || _board == nullptr || _board->drivers().asic == nullptr) {
        return false;
    }

    const String extranonce2 = _stratum->next_extranonce2();
    drivers::AsicJob job{};
    if (!_build_asic_job(pool_job, extranonce2, job)) {
        return false;
    }

    if (!_board->drivers().asic->send_work(job)) {
        return false;
    }

    JobContext context;
    context.job = job;
    context.pool_job_id = pool_job.id;
    context.extranonce2 = extranonce2;
    context.network_diff = _network_diff(pool_job.nbits);
    if (_job_mutex != nullptr && xSemaphoreTake(_job_mutex, portMAX_DELAY) == pdTRUE) {
        _job_cache[job.id] = context;
        xSemaphoreGive(_job_mutex);
    }
    if (_runtime != nullptr) {
        ++_runtime->mining.asic_job_counter;
    }
    LOG_D("ASIC job [%03u] with ext2 [%s]", static_cast<unsigned>(job.id), extranonce2.c_str());
    return true;
}

bool AsicMiningService::_build_asic_job(const PoolJobData& pool_job, const String& extranonce2, drivers::AsicJob& job) {
    const state::StratumTelemetry st = _stratum_snapshot();
    const String coinbase_str = pool_job.coinb1 + String(st.extranonce1) + extranonce2 + pool_job.coinb2;
    if (coinbase_str.length() == 0 || (coinbase_str.length() % 2) != 0) {
        LOG_E("Failed to build coinbase string");
        return false;
    }

    uint8_t* coinbase = static_cast<uint8_t*>(heap_caps_malloc(coinbase_str.length() / 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (coinbase == nullptr) {
        LOG_E("Failed to allocate coinbase buffer");
        return false;
    }

    uint8_t merkle_root[32] = {};
    size_t res = str_to_byte_array(coinbase_str.c_str(), coinbase_str.length(), coinbase);
    if (res <= 0) {
        heap_caps_free(coinbase);
        LOG_E("Failed to convert coinbase string to byte array");
        return false;
    }
    csha256d(coinbase, coinbase_str.length() / 2, merkle_root);
    heap_caps_free(coinbase);

    uint8_t merkle_concatenated[64] = {};
    JsonArrayConst branch = pool_job.merkle_branch.as<JsonArrayConst>();
    for (JsonVariantConst item : branch) {
        const char* merkle_element = item.as<const char*>();
        uint8_t node[32] = {};
        res = str_to_byte_array(merkle_element, 64, node);
        if (res <= 0) {
            LOG_E("Failed to convert merkle element string to byte array");
            return false;
        }
        memcpy(merkle_concatenated, merkle_root, 32);
        memcpy(merkle_concatenated + 32, node, 32);
        csha256d(merkle_concatenated, sizeof(merkle_concatenated), merkle_root);
    }

    job.id = static_cast<uint8_t>((_next_job_id + _job_id_step()) % 128);
    _next_job_id = job.id;
    job.num_midstates = 0x01;
    *reinterpret_cast<uint32_t*>(job.version) = strtoul(pool_job.version.c_str(), nullptr, 16);

    res = str_to_byte_array(pool_job.prevhash.c_str(), pool_job.prevhash.length(), job.prev_block_hash);
    if (res <= 0) {
        LOG_E("Failed to convert prevhash string to byte array");
        return false;
    }
    reverse_bytes(job.prev_block_hash, sizeof(job.prev_block_hash));

    memcpy(job.merkle_root, merkle_root, sizeof(merkle_root));
    reverse_words(job.merkle_root, sizeof(merkle_root));

    *reinterpret_cast<uint32_t*>(job.ntime) = strtoul(pool_job.ntime.c_str(), nullptr, 16);
    *reinterpret_cast<uint32_t*>(job.nbits) = strtoul(pool_job.nbits.c_str(), nullptr, 16);
    *reinterpret_cast<uint32_t*>(job.starting_nonce) = 0x00000000;
    return true;
}

bool AsicMiningService::_find_job(uint8_t asic_job_id, JobContext& context) {
    if (_job_mutex == nullptr || xSemaphoreTake(_job_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    const auto it = _job_cache.find(asic_job_id);
    const bool found = it != _job_cache.end();
    if (found) {
        context = it->second;
    }
    xSemaphoreGive(_job_mutex);
    return found;
}

void AsicMiningService::_clear_job_cache() {
    if (_job_mutex != nullptr && xSemaphoreTake(_job_mutex, portMAX_DELAY) == pdTRUE) {
        _job_cache.clear();
        xSemaphoreGive(_job_mutex);
    }
    _submitted_nonces.clear();
    _dedup_job_key = "";
}

void AsicMiningService::_record_nonce(double diff) {
    const uint32_t now_ms = millis();
    _hashrate_samples.push_back({now_ms, diff});
    _hashrate_s3m += diff;
    _hashrate_s30m += diff;
    _hashrate_s60m += diff;

    while (_off_3m < _hashrate_samples.size() && (now_ms - _hashrate_samples[_off_3m].first) > kHashrateWindow3mMs) {
        _hashrate_s3m -= _hashrate_samples[_off_3m].second;
        ++_off_3m;
    }
    while (_off_30m < _hashrate_samples.size() && (now_ms - _hashrate_samples[_off_30m].first) > kHashrateWindow30mMs) {
        _hashrate_s30m -= _hashrate_samples[_off_30m].second;
        ++_off_30m;
    }
    while (!_hashrate_samples.empty() && (now_ms - _hashrate_samples.front().first) > kHashrateWindow60mMs) {
        _hashrate_s60m -= _hashrate_samples.front().second;
        _hashrate_samples.pop_front();
        if (_off_30m > 0) {
            --_off_30m;
        }
        if (_off_3m > 0) {
            --_off_3m;
        }
    }
    if (_off_3m >= _hashrate_samples.size()) {
        _hashrate_s3m = 0.0;
    }
    if (_off_30m >= _hashrate_samples.size()) {
        _hashrate_s30m = 0.0;
    }
    if (_hashrate_samples.empty()) {
        _hashrate_s60m = 0.0;
    }

    if (_runtime != nullptr) {
        ++_runtime->mining.asic_nonce_counter;
        _runtime->mining.last_asic_nonce_ms = now_ms;
    }
}

void AsicMiningService::_publish_diff(double last, double pool, double network) {
    if (_runtime == nullptr) {
        return;
    }
    if (last > 0.0) {
        _runtime->mining.diff_last = last;
        if (last > _runtime->mining.diff_best_session) {
            _runtime->mining.diff_best_session = last;
        }
    }
    _runtime->mining.diff_pool = pool;
    _runtime->mining.diff_network = network;
    if (_events != nullptr) {
        _events->set(system::Event::TelemetryUpdated);
    }
}

void AsicMiningService::_log_share_table(double last, double pool, double network, uint8_t asic_id) {
    static uint32_t last_header_ms = 0;
    const uint32_t now_ms = millis();
    if (last_header_ms == 0 || now_ms - last_header_ms >= kSummaryLogIntervalMs) {
        LOG_L(" ============%s=========== ", app::kFirmwareVersion);
        LOG_I("| ASIC | Last | Pool | Network |");
        LOG_I("|------|------|------|---------|");
        last_header_ms = now_ms;
    }

    const uint8_t expected = _board != nullptr ? _board->mining_profile().asic_count : 0;
    LOG_I("| %u/%u  |%-6s|%-6s|%-7s|",
          static_cast<unsigned>(asic_id + 1),
          static_cast<unsigned>(expected),
          formatNumber(static_cast<float>(last), 4).c_str(),
          formatNumber(static_cast<float>(pool), 4).c_str(),
          formatNumber(static_cast<float>(network), 7).c_str());
}

uint8_t AsicMiningService::_job_id_step() const {
    if (_board == nullptr) {
        return 8;
    }
    switch (_board->mining_profile().asic_family) {
        case bsp::AsicFamily::BM1366:
            return 8;
        case bsp::AsicFamily::BM1370:
        case bsp::AsicFamily::BM1373:
            return 24;
        case bsp::AsicFamily::Unknown:
        default:
            return 8;
    }
}

double AsicMiningService::_network_diff(const String& nbits) const {
    if (nbits.length() < 8) {
        return 0.0;
    }
    static constexpr uint8_t kTargetBufferSize = 64;
    uint8_t netdiff_array[kTargetBufferSize / 2] = {};
    char str[kTargetBufferSize + 1] = {};
    memset(str, '0', kTargetBufferSize);
    const int k = static_cast<int>(strtol(nbits.substring(0, 2).c_str(), nullptr, 16)) - 3;
    const int index = 58 - 2 * k;
    if (index < 0 || index >= kTargetBufferSize) {
        return 0.0;
    }
    memcpy(str + index, nbits.substring(2).c_str(), nbits.length() - 2);
    str[kTargetBufferSize] = '\0';
    str_to_byte_array(str, kTargetBufferSize / 2, netdiff_array);
    reverse_bytes(netdiff_array, kTargetBufferSize / 2);
    return le_hash_to_diff(netdiff_array);
}

double AsicMiningService::_share_diff(
    uint32_t version,
    uint8_t* prev_block_hash,
    uint8_t* merkle_root,
    uint32_t ntime,
    uint32_t nbits,
    uint32_t nonce) const {
    uint8_t header[80] = {};
    uint8_t hash[32] = {};
    uint8_t prev_block_hash_t[32] = {};
    uint8_t merkle_root_t[32] = {};
    memcpy(prev_block_hash_t, prev_block_hash, 32);
    memcpy(merkle_root_t, merkle_root, 32);
    reverse_words(prev_block_hash_t, 32);
    reverse_words(merkle_root_t, 32);
    memcpy(header, &version, 4);
    memcpy(header + 4, prev_block_hash_t, 32);
    memcpy(header + 36, merkle_root_t, 32);
    memcpy(header + 68, &ntime, 4);
    memcpy(header + 72, &nbits, 4);
    memcpy(header + 76, &nonce, 4);
    csha256d(header, sizeof(header), hash);
    return le_hash_to_diff(hash);
}

state::StratumTelemetry AsicMiningService::_stratum_snapshot() const {
    state::RuntimeState temp;
    if (_stratum != nullptr) {
        _stratum->poll(temp);
    }
    return temp.stratum;
}

}  // namespace nm::services
