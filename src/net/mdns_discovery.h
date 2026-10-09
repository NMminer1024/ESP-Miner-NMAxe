#pragma once
#include <Arduino.h>
#include <stdint.h>
#include "swarm_ctx.h"   // neighbor_ip_set_t / neighbor_ip_t

// ============================================================================
//  mDNS (DNS-SD) discovery client for swarm peer detection.
//
//  Queries the LAN multicast group (224.0.0.251:5353) for a set of known
//  DNS-SD service types (see kMdnsServiceNames in mdns_discovery.cpp) and
//  collects the A-record IPs of all responding miners. Currently this covers
//  "_nmaxe._tcp.local" (NMAxe family) and "_nmminer._tcp.local" (NMMiner).
//  To support a new miner family, append its service type to that array —
//  the response parser already gathers every A record regardless of which
//  service it belongs to, so no other change is required.
//
//  Fallback semantics: if the query fails (socket error) or no responses
//  arrive (AP client isolation, multicast blocked, ...), the caller keeps
//  the legacy ICMP-scan-based discovery path. This function never blocks
//  longer than timeout_ms + a small margin.
// ============================================================================

// Query the LAN for _nmaxe._tcp services.
//  out_ips   : receives discovered peer IPs (self is excluded)
//  self_ip   : own IPv4 (host byte order, as neighbor_ip_t) to filter out
//              our own responder's echo of the query
//  timeout_ms: response collection window (default 3000 ms)
// Returns: number of peers found (>= 0), or -1 on socket error.
int mdns_discovery_query(neighbor_ip_set_t* out_ips, uint32_t self_ip,
                         uint32_t timeout_ms = 3000);
