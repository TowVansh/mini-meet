/* NAT traversal, a simplified ICE (RFC 8445):
 *  1. gather   host candidate (our LAN address) and server-reflexive candidate
 *              (our public address as seen by a STUN server)
 *  2. exchange candidates through the signalling server (CAND lines)
 *  3. check    both peers send STUN Binding requests to every candidate of the
 *              other at the same time. The outgoing packets open a mapping in
 *              our own NAT, so the other side's packets can come back in:
 *              "UDP hole punching"
 *  4. select   the first candidate that answers becomes the media address
 * Checks use USERNAME "<their-id>:<my-id>" so packets map to the right peer. */
#include "client.h"
#include "../common/util.h"
#include "../stun/stun.h"

#include <string.h>

#define CHECK_INTERVAL_MS 100
#define ICE_TIMEOUT_MS    15000
#define KEEPALIVE_MS      2500

int ice_gather(void) {
    uint32_t ip;
    char a[32];
    g.nlcands = 0;
    if (net_local_ip(&ip) == 0) {
        struct sockaddr_in sa;
        socklen_t len = sizeof sa;
        getsockname(g.udp, (struct sockaddr *)&sa, &len);
        g.lcands[0].addr.ip = ip;
        g.lcands[0].addr.port = ntohs(sa.sin_port);
        strcpy(g.lcands[0].type, "host");
        g.nlcands = 1;
        log_msg("ice", "host candidate %s", ep_str(&g.lcands[0].addr, a, sizeof a));
    }
    if (g.use_stun) {
        endpoint_t stun;
        int attempt;
        if (net_resolve(g.stun_host, (uint16_t)g.stun_port, &stun) != 0) {
            log_msg("ice", "cannot resolve STUN server %s", g.stun_host);
            return g.nlcands;
        }
        /* UDP may drop the request; retry a few times (RFC 5389 retransmission). */
        for (attempt = 0; attempt < 5; attempt++) {
            uint8_t req[64], buf[PKT_MAX], tid[12];
            uint64_t deadline = now_ms() + 300u * (attempt + 1);
            stun_new_tid(tid);
            udp_send(g.udp, &stun, req, stun_build_request(req, sizeof req, tid, NULL));
            while (now_ms() < deadline) {
                endpoint_t from;
                stun_msg_t m;
                int n = udp_recv(g.udp, &from, buf, sizeof buf);
                if (n <= 0) {
                    sleep_ms(5);
                    continue;
                }
                if (stun_parse(buf, (size_t)n, &m) == 0 && m.type == STUN_BINDING_SUCCESS &&
                    memcmp(m.tid, tid, 12) == 0 && m.has_mapped) {
                    if (g.nlcands && ep_equal(&m.mapped, &g.lcands[0].addr)) {
                        log_msg("ice", "STUN says %s: no NAT in front of us", ep_str(&m.mapped, a, sizeof a));
                        return g.nlcands;
                    }
                    g.lcands[g.nlcands].addr = m.mapped;
                    strcpy(g.lcands[g.nlcands].type, "srflx");
                    g.nlcands++;
                    log_msg("ice", "srflx candidate %s (via STUN %s)", ep_str(&m.mapped, a, sizeof a), g.stun_host);
                    return g.nlcands;
                }
            }
        }
        log_msg("ice", "no STUN reply from %s: UDP blocked? continuing with host candidate only", g.stun_host);
    }
    return g.nlcands;
}

void ice_send_candidates(peer_t *p) {
    int i;
    for (i = 0; i < g.nlcands; i++) {
        struct in_addr ia;
        char ip[INET_ADDRSTRLEN];
        ia.s_addr = g.lcands[i].addr.ip;
        inet_ntop(AF_INET, &ia, ip, sizeof ip);
        sig_send("CAND %s %s %s %u", p->id, g.lcands[i].type, ip, (unsigned)g.lcands[i].addr.port);
    }
}

void ice_add_remote(peer_t *p, const char *type, const endpoint_t *ep) {
    int i;
    char a[32];
    for (i = 0; i < p->ncands; i++)
        if (ep_equal(&p->cands[i].addr, ep)) return;
    if (p->ncands >= MAX_CANDS) return;
    p->cands[p->ncands].addr = *ep;
    snprintf(p->cands[p->ncands].type, sizeof p->cands[0].type, "%s", type);
    stun_new_tid(p->cands[p->ncands].tid);
    p->ncands++;
    if (p->ice_state == ICE_NEW) {
        p->ice_state = ICE_CHECKING;
        p->ice_start_ms = now_ms();
    }
    log_msg("ice", "%s: remote %s candidate %s", p->name, type, ep_str(ep, a, sizeof a));
}

static void send_check(peer_t *p, cand_t *c) {
    uint8_t buf[128];
    char user[2 * MM_ID_LEN + 2];
    snprintf(user, sizeof user, "%s:%s", p->id, g.self_id);
    udp_send(g.udp, &c->addr, buf, stun_build_request(buf, sizeof buf, c->tid, user));
}

void ice_tick(peer_t *p, uint64_t now) {
    int i;
    if (p->ice_state == ICE_CHECKING) {
        if (now - p->last_check_ms >= CHECK_INTERVAL_MS) {
            p->last_check_ms = now;
            for (i = 0; i < p->ncands; i++) send_check(p, &p->cands[i]);
        }
        if (now - p->ice_start_ms > ICE_TIMEOUT_MS) {
            p->ice_state = ICE_FAILED;
            log_msg("ice", "%s: no candidate pair worked after %d s. Both sides behind symmetric NAT? "
                    "That case needs a TURN relay.", p->name, ICE_TIMEOUT_MS / 1000);
        }
    } else if (p->ice_state == ICE_CONNECTED) {
        /* Keepalive: refresh NAT mappings (they expire after ~30 s of silence). */
        if (now - p->last_keepalive_ms >= KEEPALIVE_MS) {
            cand_t c;
            p->last_keepalive_ms = now;
            c.addr = p->sel;
            stun_new_tid(c.tid);
            send_check(p, &c);
        }
    }
}

static const char *local_type_for(const endpoint_t *mapped) {
    int i;
    for (i = 0; i < g.nlcands; i++)
        if (ep_equal(&g.lcands[i].addr, mapped)) return g.lcands[i].type;
    return "prflx";   /* a mapping we did not learn from STUN (another NAT path) */
}

void ice_on_stun(const uint8_t *buf, size_t len, const endpoint_t *from) {
    stun_msg_t m;
    char a[32];
    int i, j;
    if (stun_parse(buf, len, &m) != 0) return;

    if (m.type == STUN_BINDING_REQUEST) {
        /* USERNAME is "<my-id>:<their-id>" from the sender's point of view. */
        char *colon = strchr(m.username, ':');
        peer_t *p;
        uint8_t out[128];
        if (!colon || (size_t)(colon - m.username) != strlen(g.self_id) ||
            strncmp(m.username, g.self_id, strlen(g.self_id)) != 0) return;
        p = peer_find(colon + 1);
        if (!p) return;
        udp_send(g.udp, from, out, stun_build_success(out, sizeof out, m.tid, from));
        p->last_rx_ms = now_ms();
        /* A request from an address we did not know about is a peer-reflexive candidate. */
        if (p->ice_state != ICE_CONNECTED) {
            int known = 0;
            for (i = 0; i < p->ncands; i++)
                if (ep_equal(&p->cands[i].addr, from)) known = 1;
            if (!known) ice_add_remote(p, "prflx", from);
        }
        return;
    }

    if (m.type == STUN_BINDING_SUCCESS) {
        for (j = 0; j < MAX_PEERS; j++) {
            peer_t *p = &g.peers[j];
            if (!p->used) continue;
            for (i = 0; i < p->ncands; i++) {
                if (memcmp(p->cands[i].tid, m.tid, 12) != 0) continue;
                p->last_rx_ms = now_ms();
                if (p->ice_state != ICE_CONNECTED) {
                    p->ice_state = ICE_CONNECTED;
                    p->sel = *from;
                    memcpy(p->rtype, p->cands[i].type, sizeof p->rtype);
                    strncpy(p->ltype, m.has_mapped ? local_type_for(&m.mapped) : "host", sizeof p->ltype - 1);
                    p->ltype[sizeof p->ltype - 1] = '\0';
                    p->last_keepalive_ms = now_ms();
                    g.force_key = 1;   /* new receiver needs a keyframe to start decoding */
                    log_msg("ice", "%s: CONNECTED via %s->%s, media to %s (%.0f ms)", p->name, p->ltype, p->rtype,
                            ep_str(from, a, sizeof a), (double)(now_ms() - p->ice_start_ms));
                }
                return;
            }
            /* keepalive responses: any success from the selected address */
            if (p->ice_state == ICE_CONNECTED && ep_equal(&p->sel, from)) {
                p->last_rx_ms = now_ms();
                return;
            }
        }
    }
}
