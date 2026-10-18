#include "thread_entry.h"
#include "../app/application.h"
#include "../app/system_events.h"
#include "../utils/logger/logger.h"
#include "../utils/reboot_log/reboot_log.h"
#include "../board/board.h"
#include "../mining/mining_types.h"
#include "../drivers/power/power_hal.h"
#include "../drivers/power/power_ctx.h"
#include "../drivers/fan/fan.h"
#include "../drivers/fan/fan_ctx.h"
#include "../drivers/temp/temp_hal.h"
#include "../drivers/temp/temp_ctx.h"
#include "../drivers/temp/tmp102.h"
#include "../net/wifi_ctx.h"
#include "../market/market_ctx.h"
#include "../net/swarm_ctx.h"
#include "../net/mdns_discovery.h"
#include "../app/daemon_ctx.h"
#include "../app/monitor_ctx.h"
#include "../app/button_ctx.h"
#include "../app/led_ctx.h"
#include "../app/benchmark_ctx.h"
#include "../app/aphorism_ctx.h"
#include "../web/web_ctx.h"
#include "../web/http_server.h"
#include "../ui/ui_manager.h"
#include "../web/recovery_page.h"
#include "../nvs/nvs_config.h"
#include <nvs.h>
#include <OneButton.h>
#include <Adafruit_NeoPixel.h>
#include "../utils/helper.h"
#include "../utils/sha/csha256.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ESPmDNS.h>
#include <set>
#include <sys/time.h>
#include "lwip/sockets.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip4.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include <limits>
#include <unordered_set>
#include <vector>

#include "thread_shared.h"
using namespace thread_shared;


// ── WiFi: STA connect with AP config-portal fallback ────────────────────────
//    Mirrors legacy wifi_connect_thread_entry; board_sal_t* -> WifiCtx*.
void wifi_connect_thread_entry(void* args) {
    WifiCtx* ctx = static_cast<WifiCtx*>(args);
    WifiState&      st  = *ctx->state;
    WifiConnConfig& cfg = *ctx->cfg;

    auto wifiEvent = [ctx](WiFiEvent_t event, WiFiEventInfo_t info) {
        WifiState& st = *ctx->state;
        const char* reason = NULL;
        static uint8_t retry_cnt = 0, max_retries = 120;
        wifi_event_sta_disconnected_t disconnected;
        switch (event) {
            case SYSTEM_EVENT_STA_CONNECTED:
                LOG_I("WiFi connected to [%s], waiting for IP...", WiFi.SSID().c_str());
                break;
            case SYSTEM_EVENT_STA_GOT_IP:
                st.ip      = WiFi.localIP();
                st.gateway = WiFi.gatewayIP();
                st.subnet  = WiFi.subnetMask();
                st.dns     = WiFi.dnsIP();
                st.status  = WL_CONNECTED;
                retry_cnt  = 0;
                LOG_I("Got IP : %s", WiFi.localIP().toString().c_str());
                // Publish mDNS so the miner is reachable as http://<mdns_name>.local
                // (sanitized: user values may contain chars invalid for DNS labels).
                {
                    String mdns = mdns_name_sanitize(ctx->cfg->mdns_name);
                    if (MDNS.begin(mdns.c_str())) {
                        MDNS.addService("http", "tcp", 80);
                        MDNS.addServiceTxt("http", "tcp", "model", ctx->cfg->board_name.c_str());
                        // Dedicated service type for swarm discovery: peers query
                        // _nmaxe._tcp.local and only NMAxe devices answer.
                        MDNS.addService("nmaxe", "tcp", 80);
                        MDNS.addServiceTxt("nmaxe", "tcp", "model", ctx->cfg->board_name.c_str());
                        LOG_I("mDNS started: http://%s.local", mdns.c_str());
                    } else {
                        LOG_W("mDNS start FAILED (name conflict?), falling back to IP access");
                    }
                }
                break;
            case SYSTEM_EVENT_STA_DISCONNECTED:
                st.ip = st.gateway = st.subnet = st.dns = IPAddress(0, 0, 0, 0);
                st.status = WL_DISCONNECTED;
                disconnected = info.wifi_sta_disconnected;
                reason = WiFi.disconnectReasonName((wifi_err_reason_t)disconnected.reason);
                retry_cnt++;
                LOG_W("WiFi disconnected, reason: %s", reason);
                break;
            default:
                break;
        }
        if (retry_cnt >= max_retries) {
            LOG_W("WiFi connection retry limit reached, rebooting...");
            reboot_intent_set(REBOOT_INTENT_WIFI_RECONNECT_FAIL, "reached %d/%d retries", retry_cnt, max_retries);
            delay(1000);
            ESP.restart();
        }
    };

    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_15dBm);
    WiFi.onEvent(wifiEvent);
    WiFi.setHostname(cfg.hostname.c_str());

    // ── force config mode ──
    if (st.force_config_required) {
        nvs_config_set_u8(NVS_CONFIG_FORCE_CONFIG, false);
        LOG_W("Set softAP [%s]...", cfg.ap_ssid.c_str());
        WiFi.mode(WIFI_AP);
        WiFi.softAP(cfg.ap_ssid);
        WiFi.softAPConfig(cfg.ap_ip, cfg.ap_ip, IPAddress(255, 255, 255, 0));
        delay(500);
        xEventGroupSetBits(ctx->init_evt, INIT_EVENT_WIFI_AP_READY);
        xTaskCreatePinnedToCore(config_monitor_thread_entry, "(config_monitor)", 1024 * 4, ctx, TASK_PRIORITY_CONFIG, NULL, 1);
        while (true) {
            st.client_connected = (WiFi.softAPgetStationNum() > 0);
            if (WiFi.softAPgetStationNum() == 0)
                LOG_W("Force configuration, ssid[%s], timeout: %ds...", cfg.ap_ssid.c_str(), st.config_timeout);
            delay(1000);
        }
    }

    // ── normal wifi connection ──
    uint16_t random_delay = random(0, 1000 * 8);
    LOG_I("Initializing WiFi, delay: %dms...", random_delay);
    delay(random_delay);
    LOG_I("Try to connect [%s]...", cfg.sta_ssid.c_str());
    WiFi.begin(cfg.sta_ssid.c_str(), cfg.sta_pwd.c_str());
    int maxRetries = 0;
    while (WiFi.status() != WL_CONNECTED && maxRetries < 60 * 5) {
        maxRetries++;
        LOG_I("Try to connect [%s] %ds...", cfg.sta_ssid.c_str(), maxRetries);
        if (maxRetries >= 15) {
            LOG_I("Set softAP [%s]...", cfg.hostname.c_str());
            WiFi.mode(WIFI_AP);
            WiFi.softAP(cfg.ap_ssid);
            WiFi.softAPConfig(cfg.ap_ip, cfg.ap_ip, IPAddress(255, 255, 255, 0));
            delay(500);
            xEventGroupSetBits(ctx->init_evt, INIT_EVENT_WIFI_AP_READY);
            xTaskCreatePinnedToCore(config_monitor_thread_entry, "(config_monitor)", 1024 * 4, ctx, TASK_PRIORITY_CONFIG, NULL, 1);
            while (true) {
                st.client_connected = (WiFi.softAPgetStationNum() > 0);
                if (WiFi.softAPgetStationNum() == 0)
                    LOG_W("Force configuration, ssid[%s], timeout: %ds...", cfg.ap_ssid.c_str(), st.config_timeout);
                delay(1000);
            }
        }
        delay(1000);
    }

    LOG_I("------------------------------------");
    LOG_I("SSID     : %s ", WiFi.SSID().c_str());
    LOG_I("IP       : %s ", WiFi.localIP().toString().c_str());
    LOG_I("RSSI     : %d dBm", WiFi.RSSI());
    LOG_I("Channel  : %d", WiFi.channel());
    LOG_I("Gateway  : %s", WiFi.gatewayIP().toString().c_str());
    LOG_I("Subnet   : %s", WiFi.subnetMask().toString().c_str());
    LOG_I("MAC      : %s", WiFi.macAddress().c_str());
    LOG_I("Hostname : %s", WiFi.getHostname());
    LOG_I("------------------------------------");

    xEventGroupSetBits(ctx->init_evt, INIT_EVENT_WIFI_STA_CONNECTED);
    vTaskDelete(NULL);
}


// ── WiFi config monitor: reboots if no client connects within the window ────
//    Mirrors legacy config_monitor_thread_entry; board_sal_t* -> WifiCtx*.
void config_monitor_thread_entry(void* args) {
    WifiCtx* ctx = static_cast<WifiCtx*>(args);
    WifiState& st = *ctx->state;
    LOG_I("(config_monitor) thread started on core %d...", xPortGetCoreID());

    st.config_timeout = MINER_WIFI_CONFIG_TIMEOUT;
    while (true) {
        if (WL_CONNECTED == st.status) break;

        if (st.client_connected == false) {
            // For NMQAxe++: the UI thread owns the decrement (lv_indev touch detect);
            // this thread only fires the reboot when timeout reaches 0.
#if defined(BOARD_NMQAXE_PP) || defined(BOARD_NMQAXE_PP_REV61) || defined(BOARD_NMQAXE_PP_REV81) || defined(BOARD_NMQAXE_PP_NEXUS)
            if (st.config_timeout == 0) {
                LOG_W("WiFi configuration timeout, rebooting...");
                reboot_intent_set(REBOOT_INTENT_WIFI_CONFIG_TIMEOUT,
                                  "no client connected during AP setup window");
                delay(1000);
                ESP.restart();
            }
            // Do NOT decrement here — UI thread handles it
#elif defined(BOARD_NMAXE) || defined(BOARD_NMAXE_GAMMA)
            if (st.config_timeout == 0) {
                LOG_W("WiFi configuration timeout, rebooting...");
                reboot_intent_set(REBOOT_INTENT_WIFI_CONFIG_TIMEOUT,
                                  "no client connected during AP setup window");
                delay(1000);
                ESP.restart();
            }
            st.config_timeout--;
#else
            #error "No board model defined. Add -D BOARD_<model> in platformio.ini"
#endif
        }
        delay(1000);
    }
    LOG_I("WiFi configuration monitor exit...");
    vTaskDelete(NULL);
}


// ── Scan: LAN /24 ICMP alive-IP discovery ───────────────────────────────────
//    Mirrors legacy alive_ip_scan_thread_entry; board_sal_t* -> SwarmCtx* (DI).
void scan_thread_entry(void* args) {
    SwarmCtx* ctx = static_cast<SwarmCtx*>(args);
    NeighborState& nbr = *ctx->neighbor;

    constexpr uint32_t PING_TIMEOUT_MS = 500; // wait 500ms per IP
    // Tunables (NVS, defaults == historical hardcoded values).
    const uint32_t yield_batch = nvs_config_get_u32(NVS_CONFIG_SCAN_YIELD_BATCH, 10);
    const uint32_t yield_ms    = nvs_config_get_u32(NVS_CONFIG_SCAN_YIELD_MS, 1100);

    struct PingPkt {
        struct icmp_echo_hdr hdr;
        uint8_t              data[32];
    };

    auto neighbor_ip_from_octets = [](uint8_t o0, uint8_t o1, uint8_t o2, uint8_t o3) -> neighbor_ip_t {
        return ((neighbor_ip_t)o0 << 24) | ((neighbor_ip_t)o1 << 16) | ((neighbor_ip_t)o2 << 8) | (neighbor_ip_t)o3;
    };

    wait_for_wifi_sta_connected(ctx->init_evt, "(neighbor)");

    // Avoid lwip resource contention during startup; wait until miner is ready.
    xEventGroupWaitBits(ctx->init_evt, INIT_EVENT_MINER_READY, pdFALSE, pdTRUE, portMAX_DELAY);
    delay(2000);

    // ── ARP pre-resolve to avoid lwIP assert(q==NULL) on full ARP table ──
    auto arp_resolve = [](ip4_addr_t* ipaddr, uint32_t timeout_ms) -> bool {
        struct netif* nif = netif_default;
        if (!nif) return false;
        LOCK_TCPIP_CORE();
        etharp_request(nif, ipaddr);
        UNLOCK_TCPIP_CORE();
        uint32_t t0 = millis();
        while ((millis() - t0) < timeout_ms) {
            struct eth_addr* eth_ret = nullptr;
            const ip4_addr_t* ip_ret = nullptr;
            LOCK_TCPIP_CORE();
            int8_t idx = etharp_find_addr(nif, ipaddr, &eth_ret, &ip_ret);
            UNLOCK_TCPIP_CORE();
            if (idx >= 0) return true;
            delay(20);
        }
        return false;
    };

    auto ping_one = [&](int sock, uint8_t o0, uint8_t o1, uint8_t o2, int last, uint16_t seq) -> bool {
        ip4_addr_t target_ip;
        IP4_ADDR(&target_ip, o0, o1, o2, last);
        if (!arp_resolve(&target_ip, PING_TIMEOUT_MS)) {
            LOG_D("(scan) %u.%u.%u.%u ARP timeout, skipping", o0, o1, o2, last);
            return false;
        }

        PingPkt pkt = {};
        ICMPH_TYPE_SET(&pkt.hdr, ICMP_ECHO);
        ICMPH_CODE_SET(&pkt.hdr, 0);
        pkt.hdr.id     = htons(0xBEEF);
        pkt.hdr.seqno  = htons(seq);
        pkt.hdr.chksum = inet_chksum(&pkt, sizeof(pkt));

        struct sockaddr_in dest = {};
        dest.sin_family      = AF_INET;
        dest.sin_addr.s_addr = htonl(
            ((uint32_t)o0 << 24) | ((uint32_t)o1 << 16) |
            ((uint32_t)o2 <<  8) |  (uint32_t)last);

        uint32_t t0 = millis();
        if (::sendto(sock, &pkt, sizeof(pkt), 0, (struct sockaddr*)&dest, sizeof(dest)) < 0) {
            LOG_D("(scan) %u.%u.%u.%u sendto failed: errno=%d (%s)", o0, o1, o2, last, errno, strerror(errno));
            return false;
        }

        uint8_t rbuf[64];
        struct sockaddr_in from = {};
        socklen_t fromlen = sizeof(from);
        int n = ::recvfrom(sock, rbuf, sizeof(rbuf), 0, (struct sockaddr*)&from, &fromlen);
        if (n > 0) {
            struct icmp_echo_hdr* reply = (struct icmp_echo_hdr*)(rbuf + 20);
            if (ICMPH_TYPE(reply) == ICMP_ER) {
                LOG_D("(scan) %u.%u.%u.%u alive, ping %lums", o0, o1, o2, last, (unsigned long)(millis() - t0));
                return true;
            }
            LOG_D("(scan) %u.%u.%u.%u unexpected ICMP type=%u code=%u", o0, o1, o2, last, ICMPH_TYPE(reply), ICMPH_CODE(reply));
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOG_D("(scan) %u.%u.%u.%u no reply (timeout %ums)", o0, o1, o2, last, PING_TIMEOUT_MS);
            } else {
                LOG_D("(scan) %u.%u.%u.%u recvfrom error: errno=%d (%s)", o0, o1, o2, last, errno, strerror(errno));
            }
        }
        return false;
    };

    while (true) {
        delay(1000);
        if (WiFi.status() != WL_CONNECTED) continue;
        if (*ctx->ota_running)             continue;
        if (xEventGroupGetBits(ctx->sys_evt) & SYS_EVENT_SCREEN_SAVER_TRIGGERED) {
            LOG_D("(scan) screensaver active, skipping alive IP scan to yield resources");
            continue;
        }

        int sock = ::socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        if (sock < 0) {
            LOG_E("(scan) raw socket create failed: %d", errno);
            delay(1000 * 10);
            continue;
        }
        struct timeval tv = { 0, (long)(PING_TIMEOUT_MS * 1000L) };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        uint32_t scan_start = millis();

        IPAddress local = WiFi.localIP();
        uint8_t o0 = local[0], o1 = local[1], o2 = local[2], self_last = local[3];
        LOG_D("(scan) start ping %u.%u.%u.0/24, generation=%u", o0, o1, o2, nbr.scan_generation);

        neighbor_ip_vector_t found;
        found.reserve(16);
        // Previous round's result stays visible until this round overwrites it.
        neighbor_ip_vector_t prev_alive;
        if (xSemaphoreTake(nbr.mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            prev_alive = nbr.alive_ips;
            xSemaphoreGive(nbr.mutex);
        }
        auto merged_with_prev = [&](const neighbor_ip_vector_t& cur) {
            neighbor_ip_set_t s(prev_alive.begin(), prev_alive.end());
            s.insert(cur.begin(), cur.end());
            return neighbor_ip_vector_t(s.begin(), s.end());
        };

        nbr.is_scanning   = true;
        nbr.scan_progress = 0;

        uint16_t seq = 0, MAX_SCAN = 254;
        for (int last = 1; last <= MAX_SCAN; last++) {
            if (last == self_last) continue;

            if (*ctx->ota_running) {
                LOG_D("(scan) OTA started mid-scan, aborting at %u.%u.%u.%u", o0, o1, o2, last);
                break;
            }
            if (xEventGroupGetBits(ctx->sys_evt) & SYS_EVENT_SCREEN_SAVER_TRIGGERED) {
                LOG_D("(scan) screensaver activated mid-scan, aborting at %u.%u.%u.%u", o0, o1, o2, last);
                break;
            }

            nbr.scan_progress = (uint16_t)last;

            if (xSemaphoreTake(nbr.scan_required, 0) == pdTRUE) {
                // Restart this round from .1; keep the visible list untouched.
                last = 1;
                seq  = 0;
                found.clear();
                nbr.scan_progress = 0;
            }

            if (ping_one(sock, o0, o1, o2, last, ++seq)) {
                found.push_back(neighbor_ip_from_octets(o0, o1, o2, (uint8_t)last));
            }

            if ((seq % yield_batch == 0) || (last == MAX_SCAN)) {
                bool is_last = (last == MAX_SCAN);
                if (xSemaphoreTake(nbr.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    // Last flush of a round is authoritative; mid-round flushes also keep prev devices.
                    if (is_last) nbr.alive_ips = std::move(found);
                    else         nbr.alive_ips = merged_with_prev(found);
                    nbr.last_scan_ms = millis();
                    xSemaphoreGive(nbr.mutex);
                }
                LOG_D("(scan) progress to %u.%u.%u.%u, found %u alive hosts", o0, o1, o2, last, (unsigned)nbr.alive_ips.size());
                delay(yield_ms);
            }
            delay(5);
        }
        ::close(sock);

        uint32_t elapsed = (millis() - scan_start) / 1000;
        LOG_D("(scan) done in %lus, %u alive on %u.%u.%u.0/24", (uint32_t)elapsed, (uint16_t)nbr.alive_ips.size(), o0, o1, o2);
        nbr.is_scanning   = false;
        nbr.scan_progress = 254;
        if (xSemaphoreTake(nbr.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            nbr.scan_generation++;
            nbr.last_scan_ms = millis();
            xSemaphoreGive(nbr.mutex);
        }
        // Cadence: when mDNS covers every confirmed Axe peer, ICMP is only a
        // safety net — run it rarely (60 min; the Swarm page can force one via
        // /api/swarm/scan?force=1). Mixed-firmware LANs or blocked multicast
        // keep the legacy 5 min, because devices mDNS cannot see are found by
        // ICMP only.
        uint32_t wait_ms  = nbr.mdns_covered ? 60 * 60 * 1000 : 5 * 60 * 1000;
        if (xSemaphoreTake(nbr.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            nbr.next_scan_due_ms = millis() + wait_ms;
            xSemaphoreGive(nbr.mutex);
        }
        xSemaphoreTake(nbr.scan_required, wait_ms);
    }
}


// ── mDNS discovery: dedicated lightweight task ──────────────────────────────
//    Queries the LAN every 10 s for _nmaxe._tcp peers. Runs in its own task so
//    the heavier swarm HTTP probe/gossip (which blocks 60+ s) cannot stall the
//    discovery cadence. Every mDNS response is already an NMAxe device, so
//    discovered IPs go straight into the confirmed set — the swarm thread still
//    probes them for hashrate / best-difficulty detail.
void mdns_thread_entry(void* args) {
    SwarmCtx* sctx = static_cast<SwarmCtx*>(args);
    NeighborState& nbr = *sctx->neighbor;

    wait_for_wifi_sta_connected(sctx->init_evt, "(mdns)");

    // Run the first query immediately after WiFi is up instead of waiting out
    // the first 10 s cycle, so peers appear on the Swarm page right away.
    if (nbr.mdns_kick) xSemaphoreGive(nbr.mdns_kick);

    auto self_ip = []() -> neighbor_ip_t {
        IPAddress a = WiFi.localIP();
        return ((neighbor_ip_t)a[0] << 24) | ((neighbor_ip_t)a[1] << 16) |
               ((neighbor_ip_t)a[2] << 8)  |  (neighbor_ip_t)a[3];
    };

    while (true) {
        // Wakes early when the Swarm page asks for an immediate query.
        xSemaphoreTake(nbr.mdns_kick, pdMS_TO_TICKS(10 * 1000));

        if (WiFi.status() != WL_CONNECTED) continue;
        if (*sctx->ota_running)            continue;

        neighbor_ip_set_t peers;
        int found = mdns_discovery_query(&peers, self_ip(), 3000);
        if (found < 0) {
            LOG_W("(mdns) query failed, relying on ICMP scan fallback");
            continue;
        }

        nbr.mdns_last_ok_ms = millis();
        if (xSemaphoreTake(nbr.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            const uint32_t now_ms = millis();
            for (const auto& ip : peers) nbr.mdns_peers[ip] = now_ms;
            // Single multicast queries lose replies; keep peers for a while before dropping.
            for (auto it = nbr.mdns_peers.begin(); it != nbr.mdns_peers.end();) {
                if ((uint32_t)(now_ms - it->second) >= MDNS_PEER_TTL_MS) it = nbr.mdns_peers.erase(it);
                else ++it;
            }
            xSemaphoreGive(nbr.mutex);
        }

        // Per-query overview (INFO): always visible at default log level.
        LOG_I("(mdns) query: %d peer(s)", found);
    }
}


// ── Swarm: probe / aggregate neighbor miner stats via /probe + /alive gossip ─
//    Mirrors legacy swarm_thread_entry; board_sal_t* -> SwarmCtx* (DI).
void swarm_thread_entry(void* args) {
    SwarmCtx* sctx = static_cast<SwarmCtx*>(args);
    SwarmState&    ctx = *sctx->swarm;
    NeighborState& nbr = *sctx->neighbor;
    MinerStatus&   st  = *sctx->status;

    wait_for_wifi_sta_connected(sctx->init_evt, "(swarm)");

    auto neighbor_ip_from_octets = [](uint8_t o0, uint8_t o1, uint8_t o2, uint8_t o3) -> neighbor_ip_t {
        return ((neighbor_ip_t)o0 << 24) | ((neighbor_ip_t)o1 << 16) | ((neighbor_ip_t)o2 << 8) | (neighbor_ip_t)o3;
    };
    auto neighbor_ip_from_ipaddress = [&neighbor_ip_from_octets](const IPAddress& address) -> neighbor_ip_t {
        return neighbor_ip_from_octets(address[0], address[1], address[2], address[3]);
    };
    auto neighbor_ip_to_cstr = [](neighbor_ip_t address, char* buffer, size_t buffer_size) {
        snprintf(buffer, buffer_size, "%u.%u.%u.%u",
                 (unsigned)((address >> 24) & 0xFF), (unsigned)((address >> 16) & 0xFF),
                 (unsigned)((address >> 8) & 0xFF), (unsigned)(address & 0xFF));
    };
    auto neighbor_ip_from_string = [&neighbor_ip_from_ipaddress](const char* text, neighbor_ip_t* address) -> bool {
        if (!text || !address) return false;
        IPAddress parsed;
        if (!parsed.fromString(text)) return false;
        *address = neighbor_ip_from_ipaddress(parsed);
        return true;
    };

    // Blacklist with expiry: non-NM hosts rest 30 min, transient probe failures only 5 min
    // (a miner mid-reboot must come back quickly). Caller must hold no lock.
    constexpr uint32_t BLACKLIST_NON_NM_MS = 30UL * 60 * 1000;
    constexpr uint32_t BLACKLIST_FAIL_MS   = 5UL * 60 * 1000;
    auto blacklist_add = [&ctx](neighbor_ip_t ip, uint32_t ttl_ms) {
        if (xSemaphoreTake(ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            ctx.probe_blacklist[ip] = millis() + ttl_ms;
            xSemaphoreGive(ctx.mutex);
        }
    };
    auto blacklist_has = [&ctx](neighbor_ip_t ip) -> bool {
        bool hit = false;
        if (xSemaphoreTake(ctx.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            auto it = ctx.probe_blacklist.find(ip);
            if (it != ctx.probe_blacklist.end()) {
                if ((int32_t)(it->second - millis()) > 0) hit = true;
                else ctx.probe_blacklist.erase(it);
            }
            xSemaphoreGive(ctx.mutex);
        }
        return hit;
    };

    while (true) {
        delay(30 * 1000);

        if (WiFi.status() != WL_CONNECTED) continue;
        if (*sctx->ota_running)            continue;
        if (xEventGroupGetBits(sctx->sys_evt) & SYS_EVENT_SCREEN_SAVER_TRIGGERED) {
            LOG_D("(swarm) screensaver active, skipping swarm probe to yield resources");
            continue;
        }

        // ── Snapshot current scan generation and alive list ──
        neighbor_ip_vector_t alive;
        neighbor_ip_seen_map_t mdns_snapshot;
        uint32_t cur_gen = 0;
        if (xSemaphoreTake(nbr.mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            alive         = nbr.alive_ips;
            mdns_snapshot = nbr.mdns_peers;
            cur_gen       = nbr.scan_generation;
            xSemaphoreGive(nbr.mutex);
        } else {
            LOG_W("(swarm) WARNING: failed to acquire nbr.mutex in 200ms");
        }
        // confirmed_ips has a single writer (this thread): fold in mDNS peers here.
        for (const auto& kv : mdns_snapshot) ctx.confirmed_ips.insert(kv.first);

        const uint8_t MAX_PROBE_FAIL = 3;
        if (cur_gen != ctx.last_scan_gen) {
            ctx.last_scan_gen = cur_gen;
            LOG_D("(swarm) new scan gen=%u; confirmed=%u gossip=%u",
                  cur_gen, (uint32_t)ctx.confirmed_ips.size(), (uint32_t)ctx.gossip_union.size());
        }

        // ── Build probe targets: confirmed ∪ local_alive ∪ gossip ──
        const neighbor_ip_t selfIP = neighbor_ip_from_ipaddress(WiFi.localIP());
        neighbor_ip_vector_t targets;
        neighbor_ip_set_t    seen;
        auto add_target = [&](neighbor_ip_t ip) {
            if (ip == selfIP) return;
            if (seen.count(ip)) return;
            if (blacklist_has(ip)) return;
            seen.insert(ip);
            targets.push_back(ip);
        };
        for (const auto& ip : ctx.confirmed_ips) add_target(ip);
        for (const auto& ip : alive)             add_target(ip);
        for (const auto& ip : ctx.gossip_union)  add_target(ip);

        uint32_t workers         = 0;
        double   total_hr        = 0.0;
        double   best_session_bd = 0.0;
        double   best_ever_bd    = 0.0;

        LOG_D("(swarm) targets(%u): confirmed=%u alive=%u gossip=%u",
              (uint32_t)targets.size(), (uint32_t)ctx.confirmed_ips.size(),
              (uint32_t)alive.size(), (uint32_t)ctx.gossip_union.size());

        for (const auto& ip : targets) {
            uint32_t t0 = millis();
            char ip_buf[16];
            neighbor_ip_to_cstr(ip, ip_buf, sizeof(ip_buf));
            LOG_D("(swarm) >>> probing %s", ip_buf);

            WiFiClient wclient;
            int code = -1;
            String body;
            if (wclient.connect(ip_buf, 80, 1000)) {
                wclient.print("GET /probe HTTP/1.0\r\nHost: ");
                wclient.print(ip_buf);
                wclient.print("\r\nConnection: close\r\n\r\n");
                String resp;
                resp.reserve(512);
                uint32_t read_dl = millis() + 1500;
                while ((wclient.connected() || wclient.available()) && millis() < read_dl) {
                    while (wclient.available()) {
                        resp += (char)wclient.read();
                        if (resp.length() > 1024) goto probe_read_done;
                    }
                    delay(1);
                }
                probe_read_done:
                wclient.stop();
                int sp = resp.indexOf(' ');
                if (sp >= 0 && resp.startsWith("HTTP/") && (int)resp.length() > sp + 3) {
                    code = resp.substring(sp + 1, sp + 4).toInt();
                }
                int bi = resp.indexOf("\r\n\r\n");
                if (bi >= 0) body = resp.substring(bi + 4);
            }

            LOG_D("(swarm) <<< probe %s => code=%d (%lums)", ip_buf, code, (unsigned long)(millis() - t0));

            if (code == 200) {
                StaticJsonDocument<256> doc;
                LOG_D("(swarm) %s: %s", ip_buf, body.c_str());
                if (!deserializeJson(doc, body)) {
                    bool isNM = doc.containsKey("hr") && doc.containsKey("ver");
                    if (isNM) {
                        workers++;
                        double hr  = doc["hr"].as<double>();
                        double sbd = doc["sbd"].as<double>();
                        double ebd = doc["ebd"].as<double>();
                        if (!isfinite(hr) || hr < 0.0 || hr > 1e14) {
                            LOG_W("(swarm) %s INVALID hr=%.0f, skipped (torn read?)", ip_buf, hr);
                        } else {
                            total_hr += hr;
                        }
                        if (!isfinite(sbd) || sbd < 0.0) sbd = 0.0;
                        if (!isfinite(ebd) || ebd < 0.0) ebd = 0.0;
                        if (sbd > best_session_bd) best_session_bd = sbd;
                        if (ebd > best_ever_bd)    best_ever_bd    = ebd;
                        ctx.confirmed_ips.insert(ip);
                        ctx.probe_fail_cnt[ip] = 0;
                        ctx.gossip_union.erase(ip);  // graduated from candidate to confirmed
                        {
                            const char* mdl = doc["model"] | "";
                            if (strncmp(mdl, "NMAxe", 5) == 0 || strncmp(mdl, "NMQAxe", 6) == 0) ctx.axe_family_ips.insert(ip);
                            else                                                                   ctx.axe_family_ips.erase(ip);
                        }
                        LOG_D("(swarm) %s NM device hr=%.0f sbd=%.0f ebd=%.0f", ip_buf, hr, sbd, ebd);
                    } else {
                        blacklist_add(ip, BLACKLIST_NON_NM_MS);
                        ctx.confirmed_ips.erase(ip);
                        ctx.axe_family_ips.erase(ip);
                        ctx.probe_fail_cnt.erase(ip);
                        ctx.gossip_union.erase(ip);  // blacklisted; gossip may re-add after expiry
                        LOG_D("(swarm) %s not NM device, blacklisted", ip_buf);
                    }
                }
            } else if (code == 503) {
                // Peer is up but busy with OTA: not a probe failure — keep it
                // confirmed and off the blacklist so it does not flap in/out.
                LOG_D("(swarm) probe %s => 503 (OTA in progress), kept", ip_buf);
            } else {
                bool is_confirmed = ctx.confirmed_ips.count(ip) > 0;
                if (is_confirmed) {
                    uint8_t& fc = ctx.probe_fail_cnt[ip];
                    if (fc < 0xFF) fc++;
                    if (fc >= MAX_PROBE_FAIL) {
                        ctx.confirmed_ips.erase(ip);
                        ctx.axe_family_ips.erase(ip);
                        ctx.probe_fail_cnt.erase(ip);
                        LOG_W("(swarm) %s removed from confirmed after %u failures", ip_buf, MAX_PROBE_FAIL);
                    } else {
                        LOG_D("(swarm) probe %s => %d, fail_cnt=%u (kept)", ip_buf, code, fc);
                    }
                } else {
                    blacklist_add(ip, BLACKLIST_FAIL_MS);
                    ctx.gossip_union.erase(ip);  // blacklisted; gossip may re-add after expiry
                    LOG_D("(swarm) probe %s => %d, blacklisted", ip_buf, code);
                }
            }
            delay(50);
        }

        // ── mDNS coverage: every confirmed Axe-family peer was heard via mDNS within the TTL ──
        // Non-Axe NM devices (e.g. NMMiner) do not speak mDNS and are ignored here.
        {
            neighbor_ip_seen_map_t mdns_now;
            if (xSemaphoreTake(nbr.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                mdns_now = nbr.mdns_peers;
                xSemaphoreGive(nbr.mutex);
            }
            const uint32_t now_ms = millis();
            uint32_t missing = 0;
            for (const auto& ip : ctx.axe_family_ips) {
                auto it = mdns_now.find(ip);
                if (it == mdns_now.end() || (uint32_t)(now_ms - it->second) >= MDNS_PEER_TTL_MS) missing++;
            }
            const bool covered = (nbr.mdns_last_ok_ms != 0) && (missing == 0);
            if (covered != nbr.mdns_covered) {
                LOG_I("(swarm) mDNS coverage %s: axe peers=%u, not seen via mDNS=%u",
                      covered ? "FULL (ICMP relaxed)" : "PARTIAL (ICMP kept at 5 min)",
                      (uint32_t)ctx.axe_family_ips.size(), missing);
            }
            nbr.mdns_covered = covered;
        }

        // ── Gossip: ask each confirmed NM peer for its /alive list ──
        // Only useful for finding peers mDNS cannot see, so skip it when mDNS covers everything.
        size_t gossip_added = 0;
        BasicJsonDocument<PsramJsonAllocator> gdoc(2048);
        for (const auto& ip : ctx.confirmed_ips) {
            if (nbr.mdns_covered) break;
            if (*sctx->ota_running) break;
            if (xEventGroupGetBits(sctx->sys_evt) & SYS_EVENT_SCREEN_SAVER_TRIGGERED) break;

            WiFiClient wclient;
            char ip_buf[16];
            neighbor_ip_to_cstr(ip, ip_buf, sizeof(ip_buf));
            if (!wclient.connect(ip_buf, 80, 1000)) continue;
            wclient.print("GET /alive?src=swarm HTTP/1.0\r\nHost: ");
            wclient.print(ip_buf);
            wclient.print("\r\nConnection: close\r\n\r\n");
            String resp;
            resp.reserve(1024);
            uint32_t read_dl = millis() + 1500;
            while ((wclient.connected() || wclient.available()) && millis() < read_dl) {
                while (wclient.available()) {
                    resp += (char)wclient.read();
                    if (resp.length() > 4096) goto gossip_read_done;
                }
                delay(1);
            }
            gossip_read_done:
            wclient.stop();
            int bi = resp.indexOf("\r\n\r\n");
            if (bi < 0) continue;
            String gbody = resp.substring(bi + 4);
            gdoc.clear();
            if (deserializeJson(gdoc, gbody)) continue;
            JsonArray arr = gdoc["ips"].as<JsonArray>();
            for (JsonVariant v : arr) {
                neighbor_ip_t candidate;
                if (!neighbor_ip_from_string(v.as<const char*>(), &candidate)) continue;
                if (candidate == selfIP) continue;
                if (ctx.confirmed_ips.count(candidate)) continue;
                if (blacklist_has(candidate)) continue;
                if (ctx.gossip_union.insert(candidate).second) gossip_added++;
            }
            delay(50);
        }
        if (gossip_added) {
            LOG_W("(swarm) gossip added %u new IPs from %u peers, union=%u",
                  (uint32_t)gossip_added, (uint32_t)ctx.confirmed_ips.size(), (uint32_t)ctx.gossip_union.size());
        }

        // ── Write aggregated results ──
        double self_hr = st.hashrate._3m;
        if (!isfinite(self_hr) || self_hr < 0.0 || self_hr > 1e14) {
            LOG_W("(swarm) INVALID self hr=%.0f, using 0 (torn read?)", self_hr);
            self_hr = 0.0;
        }
        uint32_t agg_workers = 0;
        float    agg_hr = 0.0f, agg_sbd = 0.0f, agg_ebd = 0.0f;
        if (xSemaphoreTake(ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            ctx.total_workers   = workers + 1;  // only peers that answered /probe, plus self
            ctx.total_hr        = total_hr + self_hr;
            ctx.best_session_bd = (best_session_bd > st.diff.best_session) ? best_session_bd : st.diff.best_session;
            ctx.best_ever_bd    = (best_ever_bd > st.diff.best_ever) ? best_ever_bd : st.diff.best_ever;
            agg_workers = ctx.total_workers;
            agg_hr      = ctx.total_hr;
            agg_sbd     = ctx.best_session_bd;
            agg_ebd     = ctx.best_ever_bd;
            xSemaphoreGive(ctx.mutex);
        }
        LOG_D("(swarm) gen=%u probed=%u(+self) workers=%u hr=%sH/s sbd=%s ebd=%s",
              ctx.last_scan_gen, (uint32_t)targets.size(), agg_workers,
              formatNumber(agg_hr, 3).c_str(),
              formatNumber(agg_sbd, 3).c_str(),
              formatNumber(agg_ebd, 3).c_str());
    }
}


void webserver_thread_entry(void* args) {
    WebCtx* ctx = static_cast<WebCtx*>(args);
    g_web = ctx;   // publish context for the free-function HTTP handlers

    bool spiffs_ok = file_system_init();
    if (!spiffs_ok) {
        LOG_W("SPIFFS mount failed — entering recovery mode (firmware-embedded recovery page)");
    }

    // wait for sta or ap ready
    xEventGroupWaitBits(ctx->init_evt, INIT_EVENT_WIFI_STA_CONNECTED | INIT_EVENT_WIFI_AP_READY, pdFALSE, pdFALSE, portMAX_DELAY);
    delay(100);

    if (!spiffs_ok) {
        webServer.on("/api/system/info",  HTTP_GET, get_system_info);
        // ── Recovery mode: wakeup endpoint so the swarm batch-upgrade flow (which calls
        // /api/wakeup before streaming the binary) does not fail immediately on devices
        // in recovery mode. Without this the wakeup GET falls through to the catch-all,
        // returns HTML, Angular httpClient fails to parse JSON → OTA never starts.
        webServer.on("/api/wakeup", HTTP_GET, [](AsyncWebServerRequest *request){
            AsyncWebServerResponse *r = request->beginResponse(200, "application/json", "{\"ok\":true}");
            r->addHeader("Access-Control-Allow-Origin", "*");
            request->send(r);
        });
        // ── Find me: also available in recovery mode so the swarm panel can locate
        // a device that is stuck in recovery (SPIFFS failed) by blinking its screen.
        webServer.on("/api/swarm/find", HTTP_POST, [ctx](AsyncWebServerRequest* request) {
            xEventGroupSetBits(ctx->sys_evt, SYS_EVENT_FIND_NEIGHBOR_TRIGGERED);
            AsyncWebServerResponse *r = request->beginResponse(200, "application/json", "{\"status\":\"ok\"}");
            r->addHeader("Access-Control-Allow-Origin", "*");
            request->send(r);
        });
        // ── Recovery mode: SPIFFS is unavailable ─────────────────────────────
        // Serve the firmware-embedded recovery page for every GET request so
        // the user can re-flash SPIFFS via a browser, then restart the device.
        webServer.on("/api/system/restart", HTTP_POST, post_restart);
        // OTA upload endpoints (both canonical and legacy aliases) so the
        // recovery page upload button works regardless of browser-cached URL.
        webServer.on("/api/update/firmware", HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler);
        webServer.on("/api/update/spiffs",   HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler);
        webServer.on("/api/system/OTA",      HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler);
        webServer.on("/api/system/OTAWWW",   HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler);
        // API probe endpoint: checkNewApiSupport() on other devices calls this to
        // decide which OTA URL to use. Returning a valid timeZone JSON lets the
        // swarm panel correctly select /api/update/firmware & /api/update/spiffs
        // instead of the legacy /api/system/OTA & /api/system/OTAWWW aliases.
        webServer.on("/api/setting/time", HTTP_GET, [](AsyncWebServerRequest *request){
            AsyncWebServerResponse *r = request->beginResponse(200, "application/json",
                "{\"timeZone\":\"UTC\",\"ntpServer\":\"pool.ntp.org\"}");
            r->addHeader("Access-Control-Allow-Origin", "*");
            request->send(r);
        });
        // for miner probe endpoint, swarm panel calls this to get hashrate and difficulty for swarm mining display , Keep this endpoint lightweight and fast.
        webServer.on("/probe", HTTP_GET, [ctx](AsyncWebServerRequest* request) {
            // Snapshot double fields once: double is non-atomic on 32-bit ESP32,
            // reading directly in printf could yield a torn value.
            double hr  = ctx->status->hashrate._3m;
            double sbd = ctx->status->diff.best_session;
            double ebd = ctx->status->diff.best_ever;
            if (!isfinite(hr)  || hr  < 0.0) hr  = 0.0;
            if (!isfinite(sbd) || sbd < 0.0) sbd = 0.0;
            if (!isfinite(ebd) || ebd < 0.0) ebd = 0.0;
            AsyncResponseStream* resp = request->beginResponseStream("application/json");
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->print("{");
            resp->printf("\"model\":\"%s\",",    ctx->spec->name.c_str());  // board model name
            resp->printf("\"hostname\":\"%s\",",    ctx->wifi_cfg->hostname.c_str()); // hostname
            resp->printf("\"ver\":\"%s\",", ctx->fw_version.c_str()); // firmware version
            resp->printf("\"sw\":%d,",           ctx->spec->tft.width);   // TFT width from board spec
            resp->printf("\"sh\":%d,",           ctx->spec->tft.height);  // TFT height from board spec
            resp->printf("\"hr\":%.0f,",       hr);  // hashrate in H/s, 3m average
            resp->printf("\"sbd\":%.0f,",      sbd); // best session difficulty
            resp->printf("\"ebd\":%.0f,",      ebd); // best ever difficulty
            resp->printf("\"ut\":%d",      ctx->status->uptime_session);   // uptime in seconds for current session
            resp->print("}");
            request->send(resp);
        });
        // Catch-all GET: serve recovery page from flash (no SPIFFS needed).
        webServer.on("/*", HTTP_GET, [](AsyncWebServerRequest *request){
            AsyncWebServerResponse *r = request->beginResponse(200, "text/html", recovery_page);
            r->addHeader("Access-Control-Allow-Origin", "*");
            request->send(r);
        });
        // CORS preflight handler — CRITICAL for swarm cross-origin upgrades.
        webServer.on("/*", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
            AsyncWebServerResponse *r = request->beginResponse(200);
            r->addHeader("Access-Control-Allow-Origin",  "*");
            r->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE");
            r->addHeader("Access-Control-Allow-Headers", "Accept,Content-Type");
            request->send(r);
        });
        webServer.begin();
        while (true) { delay(250); }
    }

    // Register AsyncWebSocket handler on webServer (same port 80, path /ws)
    webSocket.onEvent(webSocketEvent);
    webServer.addHandler(&webSocket);
    webServer.on("/api/system/info", HTTP_GET, get_system_info);
    webServer.on("/api/system/restart",HTTP_POST, post_restart);
    webServer.on("/api/system/clearhits", HTTP_POST, post_reset_block_hits);
    webServer.on("/api/mining/state", HTTP_PATCH, [](AsyncWebServerRequest *request){}, NULL, patch_mining_state);
    // ── Reboot history (planned vs crash, persisted across boots) ───────────
    webServer.on("/api/reboot/last", HTTP_GET,    get_reboot_last);
    webServer.on("/api/reboot/list", HTTP_GET,    get_reboot_list);
    webServer.on("/api/reboot/list", HTTP_DELETE, delete_reboot_list);
    // ── Coredump (post-mortem summary in flash, cleared on demand) ─────────
    webServer.on("/api/coredump/info", HTTP_GET,    get_coredump_info);
    webServer.on("/api/coredump",      HTTP_DELETE, delete_coredump);
    // ── Wakeup: any caller (local or cross-origin swarm panel) can wake this device's screensaver.
    webServer.on("/api/wakeup", HTTP_GET, [ctx](AsyncWebServerRequest *request){
        UIManager::instance().wake_activity();
        xEventGroupClearBits(ctx->sys_evt, SYS_EVENT_SCREEN_SAVER_TRIGGERED);
        AsyncWebServerResponse *r = request->beginResponse(200, "application/json", "{\"ok\":true}");
        r->addHeader("Access-Control-Allow-Origin", "*");
        request->send(r);
    });
    // ── Dashboard data endpoints ──────────────────────────────────────────────
    webServer.on("/api/dashboard/hr/dist",        HTTP_GET, get_hr_distribution);
    webServer.on("/api/dashboard/gauge/limits",   HTTP_GET, get_gauge_limits);
    webServer.on("/api/dashboard/chart/history",  HTTP_GET, get_status_history);
    webServer.on("/api/dashboard/chart/realtime", HTTP_GET, get_status_realtime);
    webServer.on("/api/dashboard/luck/history",   HTTP_GET, get_lucky_history);
    // ── Swarm endpoints ───────────────────────────────────────────────────────
    webServer.on("/api/swarm/scan", HTTP_POST, [ctx](AsyncWebServerRequest* request) {
        // Opening the Swarm page: query mDNS right away (cheap, ~3 s) and only
        // trigger a background ICMP rescan when the last one is stale.
        // ?force=1 (the page's deep-scan button) always triggers a rescan.
        if (ctx->neighbor->mdns_kick) xSemaphoreGive(ctx->neighbor->mdns_kick);
        const bool force = request->hasParam("force");
        uint32_t lms = 0;
        if (xSemaphoreTake(ctx->neighbor->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            lms = ctx->neighbor->last_scan_ms;
            xSemaphoreGive(ctx->neighbor->mutex);
        }
        // Match the scan thread's cadence (60 min safety net when mDNS covers
        // every confirmed Axe peer, otherwise the legacy 5 min) so opening the
        // Swarm page does not defeat the relaxed ICMP schedule.
        const uint32_t stale_ms = ctx->neighbor->mdns_covered ? 60UL * 60 * 1000 : 5UL * 60 * 1000;
        bool stale = force || (lms == 0) || (millis() - lms > stale_ms);
        if (stale && ctx->neighbor->scan_required) {
            xSemaphoreGive(ctx->neighbor->scan_required);
            LOG_I("Triggered alive IP scan by swarm request (%s)", force ? "forced" : "last scan stale");
        }
        AsyncWebServerResponse *r = request->beginResponse(200, "application/json", "{\"status\":\"ok\"}");
        r->addHeader("Access-Control-Allow-Origin", "*");
        request->send(r);
    }); // kick mDNS now; ICMP rescan when stale, or always when forced
    // ── Swarm summary: the firmware-aggregated totals (SwarmState) — the same
    // numbers the LCD shows, so AxeOS aggregate cards can match the screens. ──
    webServer.on("/api/swarm/summary", HTTP_GET, [ctx](AsyncWebServerRequest* request) {
        if (ctx->ota->running) {
            request->send(503, "text/plain", "OTA in progress");
            return;
        }
        uint32_t workers = 1;
        float thr = 0.0f, sbd = 0.0f, ebd = 0.0f;
        if (ctx->swarm && xSemaphoreTake(ctx->swarm->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            workers = ctx->swarm->total_workers;
            thr     = ctx->swarm->total_hr;
            sbd     = ctx->swarm->best_session_bd;
            ebd     = ctx->swarm->best_ever_bd;
            xSemaphoreGive(ctx->swarm->mutex);
        }
        AsyncResponseStream* resp = request->beginResponseStream("application/json");
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->printf("{\"workers\":%u,\"hr\":%.0f,\"sbd\":%.0f,\"ebd\":%.0f}",
                     (unsigned)workers, (double)thr, (double)sbd, (double)ebd);
        request->send(resp);
    });
    // ── Find me: blink screen to help user locate a specific device ───────────
    webServer.on("/api/swarm/find", HTTP_POST, [ctx](AsyncWebServerRequest* request) {
        xEventGroupSetBits(ctx->sys_evt, SYS_EVENT_FIND_NEIGHBOR_TRIGGERED);
        AsyncWebServerResponse *r = request->beginResponse(200, "application/json", "{\"status\":\"ok\"}");
        r->addHeader("Access-Control-Allow-Origin", "*");
        request->send(r);
    });
    // ── Logging and echo endpoints ───────────────────────────────────────────────
    webServer.on("/api/log", HTTP_GET, echo_handler);
    // ── OTA update endpoints ──────────────────────────────────────────────────
    webServer.on("/api/update/progress", HTTP_GET,  get_ota_progress);                                          // progress poll
    webServer.on("/api/update/last-result", HTTP_GET, get_ota_last_result);                                     // last OTA result snapshot
    webServer.on("/api/update/firmware", HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler); // canonical
    webServer.on("/api/update/spiffs",   HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler); // canonical
    webServer.on("/api/system/OTA",      HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler); // compat alias
    webServer.on("/api/system/OTAWWW",   HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler); // compat alias
    webServer.on("/api/update/screensaver",HTTP_POST,  [](AsyncWebServerRequest *request){}, file_upload_handler);
    // ── Theme endpoints (OPTIONS covered by wildcard handler below) ───────────
    webServer.on("/api/theme", HTTP_GET,  get_theme_handler);
    webServer.on("/api/theme", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, post_theme_handler);
    // ── Settings per-card endpoints ───────────────────────────────────────────
    webServer.on("/api/setting/network",    HTTP_GET,   get_setting_network);
    webServer.on("/api/setting/network",    HTTP_PATCH, [](AsyncWebServerRequest *request){}, NULL, patch_setting_network);
    webServer.on("/api/setting/time",       HTTP_GET,   get_setting_time);
    webServer.on("/api/setting/time",       HTTP_PATCH, [](AsyncWebServerRequest *request){}, NULL, patch_setting_time);
    webServer.on("/api/setting/mining",     HTTP_GET,   get_setting_mining);
    webServer.on("/api/setting/mining",     HTTP_PATCH, [](AsyncWebServerRequest *request){}, NULL, patch_setting_mining);
    webServer.on("/api/setting/market",     HTTP_GET,   get_setting_market);
    webServer.on("/api/setting/market",     HTTP_PATCH, [](AsyncWebServerRequest *request){}, NULL, patch_setting_market);
    webServer.on("/api/setting/preference", HTTP_GET,   get_setting_preference);
    webServer.on("/api/setting/preference", HTTP_PATCH, [](AsyncWebServerRequest *request){}, NULL, patch_setting_preference);
    // ── Benchmark endpoints ────────────────────────────────────────────────────
    webServer.on("/api/benchmark",         HTTP_GET,    get_benchmark);
    webServer.on("/api/benchmark",         HTTP_PATCH,  [](AsyncWebServerRequest *request){}, NULL, patch_benchmark);
    webServer.on("/api/benchmark/start",   HTTP_POST,   [](AsyncWebServerRequest *request){}, NULL, post_benchmark_start);
    webServer.on("/api/benchmark/stop",    HTTP_POST,   post_benchmark_stop);
    webServer.on("/api/benchmark/results", HTTP_DELETE, delete_benchmark_results);
    webServer.on("/api/benchmark/reset",   HTTP_POST,   post_benchmark_reset);
    webServer.on("/api/benchmark/apply",   HTTP_POST,   [](AsyncWebServerRequest *request){}, NULL, post_benchmark_apply);

    // for miner probe endpoint, swarm panel calls this to get hashrate and difficulty for swarm mining display , Keep this endpoint lightweight and fast.
    webServer.on("/probe", HTTP_GET, [ctx](AsyncWebServerRequest* request) {
        // Return 503 during OTA — avoid competing with the flash write for TCP/lwIP resources.
        if (ctx->ota->running) {
            request->send(503, "text/plain", "OTA in progress");
            return;
        }
        double hr  = ctx->status->hashrate._3m;
        double sbd = ctx->status->diff.best_session;
        double ebd = ctx->status->diff.best_ever;
        if (!isfinite(hr)  || hr  < 0.0) hr  = 0.0;
        if (!isfinite(sbd) || sbd < 0.0) sbd = 0.0;
        if (!isfinite(ebd) || ebd < 0.0) ebd = 0.0;
        AsyncResponseStream* resp = request->beginResponseStream("application/json");
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->print("{");
        resp->printf("\"model\":\"%s\",",    ctx->spec->name.c_str());  // board model name
        resp->printf("\"hostname\":\"%s\",",    ctx->wifi_cfg->hostname.c_str()); // hostname
        resp->printf("\"ver\":\"%s\",", ctx->fw_version.c_str()); // firmware version
        resp->printf("\"sw\":%d,",           ctx->spec->tft.width);   // TFT width from board spec
        resp->printf("\"sh\":%d,",           ctx->spec->tft.height);  // TFT height from board spec
        resp->printf("\"hr\":%.0f,",       hr);  // hashrate in H/s, 3m average
        resp->printf("\"sbd\":%.0f,",      sbd); // best session difficulty
        resp->printf("\"ebd\":%.0f,",      ebd); // best ever difficulty
        resp->printf("\"ut\":%d",      ctx->status->uptime_session);   // uptime in seconds for current session
        resp->print("}");
        request->send(resp);
    });
    // alive ip return from miner, just alive ip, maybe phone, pc, or miner itself, for miner probe endpoint, AxeOS probe one by one .
    webServer.on("/alive", HTTP_GET, [ctx](AsyncWebServerRequest* request) {
        // Return 503 during OTA — avoid competing with the flash write for TCP/lwIP resources.
        if (ctx->ota->running) {
            request->send(503, "text/plain", "OTA in progress");
            return;
        }
        String self_ip = WiFi.localIP().toString();
        AsyncResponseStream* resp = request->beginResponseStream("application/json");
        resp->addHeader("Access-Control-Allow-Origin", "*");
        bool scanning   = ctx->neighbor->is_scanning;
        uint16_t prog   = ctx->neighbor->scan_progress;
        uint32_t lms    = 0;
        uint32_t due_ms = 0;
        resp->printf("{\"self\":\"%s\",\"scanning\":%s,\"progress\":%u,\"total\":254",
            self_ip.c_str(), scanning ? "true" : "false", (unsigned)prog);
        if (xSemaphoreTake(ctx->neighbor->mutex, pdMS_TO_TICKS(300)) == pdTRUE) {
            lms    = ctx->neighbor->last_scan_ms;
            due_ms = ctx->neighbor->next_scan_due_ms;
            xSemaphoreGive(ctx->neighbor->mutex);
        }
        uint32_t next_in = 0;
        if (!scanning && lms > 0 && due_ms != 0) {
            int32_t remain_ms = (int32_t)(due_ms - millis());
            next_in = remain_ms > 0 ? (uint32_t)(remain_ms / 1000) : 0;
        }
        resp->printf(",\"next_scan_in\":%u", (unsigned)next_in);
        // mDNS discovery status: fresh → UI shows the "live auto-discovery"
        // presentation instead of the ICMP scan-progress ring. The query cycle
        // is 10 s + up to 3 s query time (+ scheduling jitter), so a 15 s
        // window could flap the UI between the two presentations; 25 s (≈2
        // worst-case cycles) keeps it stable without hiding real outages.
        const uint32_t MDNS_QUERY_PERIOD_S = 10;
        bool   mdns_active  = false;
        uint32_t mdns_next_in = 0;
        if (ctx->neighbor->mdns_last_ok_ms != 0) {
            uint32_t mdns_age_ms = millis() - ctx->neighbor->mdns_last_ok_ms;
            if (mdns_age_ms < 25 * 1000) {
                mdns_active = true;
                uint32_t mdns_elapsed_s = mdns_age_ms / 1000;
                mdns_next_in = (mdns_elapsed_s < MDNS_QUERY_PERIOD_S)
                               ? (MDNS_QUERY_PERIOD_S - mdns_elapsed_s) : 0;
            }
        }
        resp->printf(",\"mdns_active\":%s,\"mdns_next_in\":%u",
                     mdns_active ? "true" : "false", (unsigned)mdns_next_in);
        resp->printf(",\"ips\":[\"%s\"", self_ip.c_str());
        if (xSemaphoreTake(ctx->neighbor->mutex, pdMS_TO_TICKS(300)) == pdTRUE) {
            auto neighbor_ip_to_cstr = [](neighbor_ip_t address, char *buffer, size_t buffer_size) {
                snprintf(buffer, buffer_size, "%u.%u.%u.%u",
                         (unsigned)((address >> 24) & 0xFF),
                         (unsigned)((address >> 16) & 0xFF),
                         (unsigned)((address >> 8) & 0xFF),
                         (unsigned)(address & 0xFF));
            };
            char ip_buf[16];
            // Union of ICMP-alive and TTL-fresh mDNS peers, de-duplicated (self already emitted).
            neighbor_ip_set_t out_set;
            for (const auto& ip : ctx->neighbor->alive_ips) out_set.insert(ip);
            const uint32_t now_ms = millis();
            for (const auto& kv : ctx->neighbor->mdns_peers) {
                if ((uint32_t)(now_ms - kv.second) < MDNS_PEER_TTL_MS) out_set.insert(kv.first);
            }
            for (const auto& ip : out_set) {
                neighbor_ip_to_cstr(ip, ip_buf, sizeof(ip_buf));
                resp->printf(",\"%s\"", ip_buf);
            }
            xSemaphoreGive(ctx->neighbor->mutex);
        }
        resp->print("]}");
        request->send(resp);
    });
    webServer.on("/api-doc", HTTP_GET, [](AsyncWebServerRequest *request){
        request->redirect("/api-doc.html");
    });
    webServer.on("/*", HTTP_GET, [ctx](AsyncWebServerRequest *request){
        // Wake screensaver when the user loads or refreshes any page on this device.
        UIManager::instance().wake_activity();
        if(xEventGroupGetBits(ctx->sys_evt) & SYS_EVENT_SCREEN_SAVER_TRIGGERED) {
            xEventGroupClearBits(ctx->sys_evt, SYS_EVENT_SCREEN_SAVER_TRIGGERED);
        }
        rest_common_get_handler(request);
    });
    webServer.on("/*", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(200);
        response->addHeader("Access-Control-Allow-Origin", "*");
        response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE");
        response->addHeader("Access-Control-Allow-Headers", "Accept,Content-Type");
        request->send(response);
    });
    webServer.begin();
    // cleanupClients() must be called periodically so AsyncWebSocket can detect
    // dropped connections and fire WS_EVT_DISCONNECT for stale clients.
    while (true) {
        delay(250);
        webSocket.cleanupClients();

        // OTA stall watchdog: runs independently of the upload callback.
        if (ctx->ota->running && ctx->ota->last_progress_ms != 0) {
            if (millis() - ctx->ota->last_progress_ms > 60*1000UL) {
                LOG_E("[OTA watchdog] upload stalled >60s at %d%%, triggering reboot", ctx->ota->progress);
                reboot_intent_set(REBOOT_INTENT_OTA_STALL,
                                  "upload stalled at %d%% for >60s",
                                  ctx->ota->progress);
                xSemaphoreGive(ctx->reboot_xsem);
            }
        }
    }
}
