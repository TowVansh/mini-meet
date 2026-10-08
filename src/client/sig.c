/* Signalling client: TCP connection to mm-server (protocol in proto.h). */
#include "client.h"
#include "../common/util.h"

#include <stdarg.h>
#include <string.h>

static mm_linebuf_t lb;
static uint64_t last_ping;

int sig_connect(void) {
    g.tcp = tcp_connect(g.server_host, (uint16_t)g.server_port);
    if (g.tcp == SOCK_INVALID) {
        log_msg("sig", "cannot connect to signalling server %s:%d", g.server_host, g.server_port);
        return -1;
    }
    log_msg("sig", "connected to %s:%d", g.server_host, g.server_port);
    sig_send("JOIN %s %s", g.room, g.name);
    last_ping = now_ms();
    return 0;
}

void sig_send(const char *fmt, ...) {
    char line[MM_LINE_MAX];
    va_list ap;
    int n;
    if (g.tcp == SOCK_INVALID) return;
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof line - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof line - 2) n = (int)sizeof line - 2;
    line[n++] = '\n';
    send(g.tcp, line, n, 0);
}

static void on_line(char *line) {
    mm_msg_t m;
    peer_t *p;
    int i;
    if (mm_tokenize(line, &m) == 0) return;

    if (strcmp(m.argv[0], "JOINED") == 0 && m.argc >= 3) {
        snprintf(g.self_id, sizeof g.self_id, "%s", m.argv[1]);
        g.joined = 1;
        log_msg("sig", "joined room %s as %s (id %s), %d already here", m.argv[2], g.name, g.self_id, m.argc - 3);
        for (i = 3; i < m.argc; i++) {
            char *colon = strchr(m.argv[i], ':');
            if (!colon) continue;
            *colon = '\0';
            p = peer_add(m.argv[i], colon + 1);
            if (p) ice_send_candidates(p);
        }
    } else if (strcmp(m.argv[0], "PEER_JOINED") == 0 && m.argc == 3) {
        log_msg("sig", "%s joined", m.argv[2]);
        p = peer_add(m.argv[1], m.argv[2]);
        if (p) ice_send_candidates(p);
    } else if (strcmp(m.argv[0], "PEER_LEFT") == 0 && m.argc == 2) {
        p = peer_find(m.argv[1]);
        if (p) {
            log_msg("sig", "%s left", p->name);
            peer_remove(p);
        }
    } else if (strcmp(m.argv[0], "CAND") == 0 && m.argc == 5) {
        endpoint_t ep;
        p = peer_find(m.argv[1]);
        if (!p) p = peer_add(m.argv[1], m.argv[1]);
        if (p && ep_parse(m.argv[3], m.argv[4], &ep) == 0) ice_add_remote(p, m.argv[2], &ep);
    } else if (strcmp(m.argv[0], "ERROR") == 0) {
        char msg[MM_LINE_MAX] = "";
        for (i = 2; i < m.argc; i++) {
            strncat(msg, m.argv[i], sizeof msg - strlen(msg) - 2);
            strcat(msg, " ");
        }
        log_msg("sig", "server error %s: %s", m.argc > 1 ? m.argv[1] : "?", msg);
        if (m.argc > 1 && (strcmp(m.argv[1], "ROOM_FULL") == 0 || strcmp(m.argv[1], "BAD_NAME") == 0)) g.running = 0;
    }
}

void sig_on_readable(void) {
    char buf[2048], line[MM_LINE_MAX];
    int n = (int)recv(g.tcp, buf, sizeof buf, 0);
    if (n <= 0) {
        log_msg("sig", "signalling connection closed (calls in progress keep running peer-to-peer)");
        sock_close(g.tcp);
        g.tcp = SOCK_INVALID;
        return;
    }
    if (mm_linebuf_push(&lb, buf, (size_t)n) != 0) {
        lb.len = 0;
        return;
    }
    while (mm_linebuf_pop(&lb, line, sizeof line)) on_line(line);
}

void sig_tick(uint64_t now) {
    if (now - last_ping >= 5000) {
        last_ping = now;
        sig_send("PING");
    }
}
