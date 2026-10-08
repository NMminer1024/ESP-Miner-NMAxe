#include "mdns_discovery.h"
#include "utils/logger/logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

namespace {

constexpr uint16_t MDNS_PORT      = 5353;
constexpr uint8_t  MDNS_GROUP[4] = { 224, 0, 0, 251 };

// ── DNS wire-format helpers (RFC 1035 / RFC 6762) ──────────────────────────

// Append a DNS name (dot-separated) to the packet buffer.
// Returns bytes written, or -1 on buffer overflow.
int dns_put_name(uint8_t* buf, size_t cap, size_t off, const char* name) {
    size_t p = off;
    while (*name) {
        // label length byte
        const char* dot = strchr(name, '.');
        size_t len = dot ? (size_t)(dot - name) : strlen(name);
        if (len == 0 || len > 63) return -1;
        if (p + 1 + len >= cap) return -1;
        buf[p++] = (uint8_t)len;
        memcpy(buf + p, name, len);
        p += len;
        if (!dot) break;
        name = dot + 1;
    }
    if (p + 1 >= cap) return -1;
    buf[p++] = 0; // root label
    return (int)(p - off);
}

// Read a (possibly compressed) DNS name starting at buf+off.
// Returns the name in out (NUL-terminated, max out_cap), and the number of
// bytes consumed in the uncompressed stream (0 for pointer targets).
// Returns -1 on malformed input / loop.
int dns_get_name(const uint8_t* buf, size_t len, size_t off, char* out, size_t out_cap) {
    size_t p = off;
    size_t copied = 0;
    int hops = 0;
    bool jumped = false;
    size_t stream_end = off; // end of the name in the original stream

    while (true) {
        if (p >= len) return -1;
        uint8_t first = buf[p];
        if ((first & 0xC0) == 0xC0) {
            // compression pointer
            if (p + 1 >= len) return -1;
            uint16_t target = ((first & 0x3F) << 8) | buf[p + 1];
            if (target >= len) return -1;
            if (!jumped) stream_end = p + 2;
            p = target;
            jumped = true;
            if (++hops > 16) return -1; // pointer loop guard
            continue;
        }
        if (first == 0) {
            if (!jumped) stream_end = p + 1;
            break;
        }
        uint8_t lbl_len = first;
        if (p + 1 + lbl_len >= len) return -1;
        if (out) {
            if (copied + lbl_len + 1 >= out_cap) return -1;
            if (copied > 0) out[copied++] = '.';
            memcpy(out + copied, buf + p + 1, lbl_len);
            copied += lbl_len;
        }
        p += 1 + lbl_len;
    }
    if (out) out[copied] = '\0';
    return (int)(stream_end - off);
}

} // namespace

int mdns_discovery_query(neighbor_ip_set_t* out_ips, uint32_t self_ip, uint32_t timeout_ms) {
    if (!out_ips) return -1;
    out_ips->clear();

    // ── Build the query packet: Q(1) _nmaxe._tcp.local PTR IN ──────────────
    uint8_t pkt[128];
    memset(pkt, 0, sizeof(pkt));
    uint16_t txid = (uint16_t)(millis() & 0xFFFF);
    pkt[0] = (txid >> 8) & 0xFF;
    pkt[1] = txid & 0xFF;
    pkt[2] = 0; pkt[3] = 0;          // flags: standard query
    pkt[4] = 0; pkt[5] = 1;          // QDCOUNT = 1
    pkt[6] = 0; pkt[7] = 0;          // ANCOUNT
    pkt[8] = 0; pkt[9] = 0;          // NSCOUNT
    pkt[10] = 0; pkt[11] = 0;        // ARCOUNT

    int nlen = dns_put_name(pkt, sizeof(pkt), 12, "_nmaxe._tcp.local");
    if (nlen < 0) { LOG_E("(mdns) query name too long"); return -1; }
    size_t q = 12 + (size_t)nlen;
    pkt[q]     = 0; pkt[q + 1] = 12; // QTYPE = PTR (12)
    pkt[q + 2] = 0; pkt[q + 3] = 1;  // QCLASS = IN (1)
    size_t pkt_len = q + 4;

    // ── Socket: bind to an EPHEMERAL port (port 0 → OS picks). We must NOT
    //    bind 5353 here — the lwIP mDNS responder started by MDNS.begin()
    //    already owns UDP 5353, so binding it again fails with EADDRINUSE.
    //    Per RFC 6762 §6.7, responders reply to the query's source IP:port,
    //    so replies land on our ephemeral port regardless. ──────────────────
    int sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        LOG_W("(mdns) socket failed: %d", errno);
        return -1;
    }
    int one = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = 0; // ephemeral; responders reply to this source port
    if (::bind(sock, (struct sockaddr*)&local, sizeof(local)) < 0) {
        LOG_W("(mdns) bind ephemeral failed: %d", errno);
        ::close(sock);
        return -1;
    }
    struct timeval tv = { 0, 100 * 1000L }; // 100ms recv granularity
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dest = {};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(MDNS_PORT);
    dest.sin_addr.s_addr = htonl(((uint32_t)MDNS_GROUP[0] << 24) | ((uint32_t)MDNS_GROUP[1] << 16) |
                                 ((uint32_t)MDNS_GROUP[2] << 8)  |  (uint32_t)MDNS_GROUP[3]);

    uint32_t t0 = millis();
    bool sent = false;
    uint8_t rbuf[512];

    while (millis() - t0 < timeout_ms) {
        if (!sent) {
            if (::sendto(sock, pkt, pkt_len, 0, (struct sockaddr*)&dest, sizeof(dest)) < 0) {
                LOG_W("(mdns) sendto failed: %d", errno);
                ::close(sock);
                return -1;
            }
            sent = true;
        }
        int n = ::recvfrom(sock, rbuf, sizeof(rbuf), 0, nullptr, nullptr);
        if (n < 12) continue; // too short / timeout (EAGAIN)

        // Validate: same txid, response flag (bit 15 of byte 2)
        if ((rbuf[0] != pkt[0]) || (rbuf[1] != pkt[1])) continue;
        if (!(rbuf[2] & 0x80)) continue;

        uint16_t qcount = (rbuf[4] << 8) | rbuf[5];
        uint16_t ancount = (rbuf[6] << 8) | rbuf[7];
        uint16_t nscount = (rbuf[8] << 8) | rbuf[9];
        uint16_t arcount = (rbuf[10] << 8) | rbuf[11];

        size_t p = 12;
        // Skip question section
        for (uint16_t i = 0; i < qcount && p < (size_t)n; i++) {
            int nl = dns_get_name(rbuf, (size_t)n, p, nullptr, 1);
            if (nl < 0) break;
            p += (size_t)nl + 4;
        }
        // Walk answer + authority + additional: collect A records
        uint16_t total_rr = ancount + nscount + arcount;
        for (uint16_t i = 0; i < total_rr && p < (size_t)n; i++) {
            char name[128];
            int nl = dns_get_name(rbuf, (size_t)n, p, name, sizeof(name));
            if (nl < 0) break;
            p += (size_t)nl;
            if (p + 10 > (size_t)n) break;
            uint16_t rtype  = (rbuf[p] << 8) | rbuf[p + 1];
            uint16_t rdlen  = (rbuf[p + 8] << 8) | rbuf[p + 9];
            p += 10;
            if (p + rdlen > (size_t)n) break;
            if (rtype == 1 && rdlen == 4) { // A record
                uint32_t ip = ((uint32_t)rbuf[p] << 24) | ((uint32_t)rbuf[p + 1] << 16) |
                              ((uint32_t)rbuf[p + 2] << 8) | (uint32_t)rbuf[p + 3];
                if (ip != 0 && ip != self_ip) out_ips->insert(ip);
            }
            p += rdlen;
        }
    }
    ::close(sock);

    LOG_D("(mdns) query done: %u peers in %lums", (unsigned)out_ips->size(),
          (unsigned long)(millis() - t0));
    return (int)out_ips->size();
}
