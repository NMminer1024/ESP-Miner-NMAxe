#pragma once
#include <Arduino.h>
#include <vector>
#include <set>
#include <map>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>
#include "utils/helper.h"            // PsramAllocator
#include "mining/mining_types.h"     // MinerStatus

// ── Neighbor IP container aliases (PSRAM-backed) ────────────────────────────
using neighbor_ip_t        = uint32_t;
using neighbor_ip_vector_t = std::vector<neighbor_ip_t, PsramAllocator<neighbor_ip_t>>;
using neighbor_ip_set_t    = std::set<neighbor_ip_t, std::less<neighbor_ip_t>,
                                      PsramAllocator<neighbor_ip_t>>;
using neighbor_ip_fail_map_t = std::map<neighbor_ip_t, uint8_t, std::less<neighbor_ip_t>,
                                        PsramAllocator<std::pair<const neighbor_ip_t, uint8_t>>>;
using neighbor_ip_seen_map_t = std::map<neighbor_ip_t, uint32_t, std::less<neighbor_ip_t>,
                                        PsramAllocator<std::pair<const neighbor_ip_t, uint32_t>>>;

// ============================================================================
//  SwarmState — aggregated neighbor-miner statistics (replaces board.status.swarm)
// ============================================================================
struct SwarmState {
    SemaphoreHandle_t      mutex = nullptr;        // guards aggregates + blacklist
    uint32_t               total_workers = 1;      // include self
    float                  total_hr = 0.0f;
    float                  best_session_bd = 0.0f;
    float                  best_ever_bd = 0.0f;
    neighbor_ip_set_t      confirmed_ips;          // confirmed NM peers (kept across gens)
    neighbor_ip_set_t      axe_family_ips;         // subset of confirmed_ips whose /probe model is NMAxe*/NMQAxe*; sole writer: swarm thread
    neighbor_ip_seen_map_t probe_blacklist;        // IP -> expiry millis; non-NM (30 min) or probe failure (5 min)
    neighbor_ip_set_t      gossip_union;           // supplemental IPs from peers' /alive
    neighbor_ip_fail_map_t probe_fail_cnt;         // consecutive probe failures per IP
    uint32_t               last_scan_gen = 0;      // last processed scan generation
};

// ============================================================================
//  NeighborState — local /24 ICMP scan results (replaces board.status.neighbor)
// ============================================================================
struct NeighborState {
    neighbor_ip_vector_t alive_ips;                // ICMP-alive IP list
    neighbor_ip_seen_map_t mdns_peers;             // mDNS peer -> last-seen millis; sole writer: mdns task (guarded by mutex)
    SemaphoreHandle_t    mutex = nullptr;
    SemaphoreHandle_t    scan_required = nullptr;  // released to trigger a re-scan
    SemaphoreHandle_t    mdns_kick = nullptr;      // released to run an mDNS query immediately
    uint32_t             last_scan_ms = 0;
    uint32_t             next_scan_due_ms = 0;     // millis() deadline of the next ICMP scan (0 = unknown)
    uint32_t             scan_generation = 0;      // +1 per completed full scan
    bool                 is_scanning = false;
    uint16_t             scan_progress = 0;        // 0..254
    // Last successful mDNS discovery (millis). Written by the swarm thread,
    // read by the scan thread to decide the ICMP full-scan cadence
    // (mDNS fresh → 30 min safety-net, stale → legacy 5 min).
    // Aligned uint32_t: atomic read/write on Xtensa, no lock needed.
    volatile uint32_t    mdns_last_ok_ms = 0;
    // True when every confirmed Axe-family peer was seen via mDNS recently, i.e. ICMP
    // is only a safety net. Written by the swarm thread, read by the scan thread.
    volatile bool        mdns_covered = false;
};

// ============================================================================
//  SwarmCtx — launch context shared by swarm_thread_entry / scan_thread_entry
// ============================================================================
struct SwarmCtx {
    SwarmState*        swarm    = nullptr;
    NeighborState*     neighbor = nullptr;
    MinerStatus*       status   = nullptr;   // self hashrate / best diffs
    volatile bool*     ota_running = nullptr;
    EventGroupHandle_t sys_evt  = nullptr;   // SYS_EVENT_SCREEN_SAVER_TRIGGERED
    EventGroupHandle_t init_evt = nullptr;   // INIT_EVENT_MINER_READY (scan gate)
};
