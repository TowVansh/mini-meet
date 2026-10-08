/* mm-stun: minimal STUN server (RFC 5389 Binding only).
 *
 *   mm-stun [port]            (default 3478)
 *
 * Answers every Binding request with the source address it saw, which is the
 * client's public (NAT-mapped) address when the request crossed a NAT. Clients
 * can use it instead of Google's server: mm --stun <host>:3478
 */
#include "../common/net.h"
#include "../common/util.h"
#include "stun.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    uint16_t port = argc > 1 ? (uint16_t)atoi(argv[1]) : 3478;
    sock_t s;
    if (net_init() != 0) return 1;
    s = udp_bind(port);
    if (s == SOCK_INVALID) {
        log_msg("stun", "cannot bind UDP %u", port);
        return 1;
    }
    sock_set_nonblock(s, 0);
    log_msg("stun", "STUN server on UDP %u", port);
    for (;;) {
        uint8_t in[1500], out[128];
        endpoint_t from;
        stun_msg_t m;
        char a[32];
        int n = udp_recv(s, &from, in, sizeof in);
        if (n <= 0) continue;
        if (stun_parse(in, (size_t)n, &m) != 0 || m.type != STUN_BINDING_REQUEST) continue;
        udp_send(s, &from, out, stun_build_success(out, sizeof out, m.tid, &from));
        log_msg("stun", "binding from %s", ep_str(&from, a, sizeof a));
    }
}
