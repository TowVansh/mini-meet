/* mm-server: Mini-Meet signalling server.
 *
 *   mm-server [port]          (default 9000)
 *
 * One thread, one select() loop over the listening socket and every client
 * socket. The server groups clients into rooms of at most 4 and relays
 * candidate messages between members of the same room. It never sees media:
 * audio and video go peer-to-peer over UDP.
 */
#include "../common/net.h"
#include "../common/proto.h"
#include "../common/util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#define MAX_CLIENTS   (FD_SETSIZE - 1)
#define IDLE_TIMEOUT  20000   /* ms without any line (clients PING every 5 s) */

typedef struct {
    sock_t       sock;
    char         id[MM_ID_LEN + 1];
    char         name[MM_NAME_MAX + 1];
    char         room[MM_NAME_MAX + 1];   /* empty = not joined */
    endpoint_t   addr;
    uint64_t     last_seen;
    mm_linebuf_t lb;
} client_t;

static client_t clients[MAX_CLIENTS];
static int      nclients;

static void send_line(client_t *c, const char *fmt, ...) {
    char line[MM_LINE_MAX];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof line - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof line - 2) n = (int)sizeof line - 2;
    line[n++] = '\n';
    send(c->sock, line, n, 0);
}

static void send_error(client_t *c, const char *code, const char *text) {
    send_line(c, "ERROR %s %s", code, text);
}

static int room_count(const char *room) {
    int i, n = 0;
    for (i = 0; i < nclients; i++)
        if (strcmp(clients[i].room, room) == 0) n++;
    return n;
}

static client_t *find_in_room(const char *room, const char *id) {
    int i;
    for (i = 0; i < nclients; i++)
        if (strcmp(clients[i].room, room) == 0 && strcmp(clients[i].id, id) == 0) return &clients[i];
    return NULL;
}

static void leave_room(client_t *c) {
    int i;
    if (!c->room[0]) return;
    log_msg("room", "leave room=%s id=%s name=%s", c->room, c->id, c->name);
    for (i = 0; i < nclients; i++) {
        client_t *o = &clients[i];
        if (o != c && strcmp(o->room, c->room) == 0) send_line(o, "PEER_LEFT %s", c->id);
    }
    c->room[0] = '\0';
}

static void handle_join(client_t *c, mm_msg_t *m) {
    char line[MM_LINE_MAX];
    size_t len;
    int i;
    if (m->argc != 3) return send_error(c, "BAD_MESSAGE", "usage: JOIN <room> <name>");
    if (c->room[0]) return send_error(c, "ALREADY_JOINED", "already in a room");
    if (!mm_valid_name(m->argv[1]) || !mm_valid_name(m->argv[2]))
        return send_error(c, "BAD_NAME", "room and name must be 1-32 chars of A-Z a-z 0-9 _ -");
    if (room_count(m->argv[1]) >= MM_MAX_ROOM) return send_error(c, "ROOM_FULL", "room already has 4 participants");

    snprintf(c->name, sizeof c->name, "%s", m->argv[2]);
    snprintf(c->room, sizeof c->room, "%s", m->argv[1]);

    /* JOINED lists everyone already in the room; they get PEER_JOINED. */
    len = (size_t)snprintf(line, sizeof line, "JOINED %s %s", c->id, c->room);
    for (i = 0; i < nclients; i++) {
        client_t *o = &clients[i];
        if (o == c || strcmp(o->room, c->room) != 0) continue;
        len += (size_t)snprintf(line + len, sizeof line - len, " %s:%s", o->id, o->name);
        send_line(o, "PEER_JOINED %s %s", c->id, c->name);
    }
    send_line(c, "%s", line);
    {
        char a[32];
        log_msg("room", "join room=%s id=%s name=%s from=%s (%d/%d)", c->room, c->id, c->name,
                ep_str(&c->addr, a, sizeof a), room_count(c->room), MM_MAX_ROOM);
    }
}

static void handle_cand(client_t *c, mm_msg_t *m) {
    client_t *to;
    endpoint_t ep;
    if (m->argc != 5) return send_error(c, "BAD_MESSAGE", "usage: CAND <to> <type> <ip> <port>");
    if (!c->room[0]) return send_error(c, "NOT_JOINED", "join a room first");
    if (!mm_valid_cand_type(m->argv[2]) || ep_parse(m->argv[3], m->argv[4], &ep) != 0)
        return send_error(c, "BAD_MESSAGE", "bad candidate");
    to = find_in_room(c->room, m->argv[1]);
    if (!to) return send_error(c, "UNKNOWN_PEER", "target peer is not in your room");
    /* Relay with the sender's id in place of the target's. */
    send_line(to, "CAND %s %s %s %s", c->id, m->argv[2], m->argv[3], m->argv[4]);
}

static void handle_line(client_t *c, char *line) {
    mm_msg_t m;
    if (mm_tokenize(line, &m) == 0) return;
    if (strcmp(m.argv[0], "JOIN") == 0) handle_join(c, &m);
    else if (strcmp(m.argv[0], "CAND") == 0) handle_cand(c, &m);
    else if (strcmp(m.argv[0], "LEAVE") == 0) leave_room(c);
    else if (strcmp(m.argv[0], "PING") == 0) send_line(c, "PONG");
    else send_error(c, "BAD_MESSAGE", "unknown command");
}

static void drop_client(int idx) {
    client_t *c = &clients[idx];
    leave_room(c);
    sock_close(c->sock);
    clients[idx] = clients[--nclients];   /* keep the array dense */
}

static void accept_client(sock_t ls) {
    struct sockaddr_in sa;
    socklen_t len = sizeof sa;
    int yes = 1;
    client_t *c;
    sock_t s = accept(ls, (struct sockaddr *)&sa, &len);
    if (s == SOCK_INVALID) return;
    if (nclients >= MAX_CLIENTS) {
        sock_close(s);
        return;
    }
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&yes, sizeof yes);
    c = &clients[nclients++];
    memset(c, 0, sizeof *c);
    c->sock = s;
    ep_from_sockaddr(&c->addr, &sa);
    snprintf(c->id, sizeof c->id, "%08x", rand_u32());
    c->last_seen = now_ms();
}

static volatile int running = 1;
static void on_signal(int sig) { (void)sig; running = 0; }

int main(int argc, char **argv) {
    uint16_t port = argc > 1 ? (uint16_t)atoi(argv[1]) : MM_DEFAULT_PORT;
    sock_t ls;
    uint64_t last_sweep = now_ms();

    setvbuf(stdout, NULL, _IONBF, 0);
    if (net_init() != 0) return 1;
    signal(SIGINT, on_signal);
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);   /* writing to a closed socket must not kill the server */
#endif
    ls = tcp_listen(port);
    if (ls == SOCK_INVALID) {
        log_msg("server", "cannot listen on port %u (error %d)", port, sock_last_error());
        return 1;
    }
    log_msg("server", "Mini-Meet signalling server listening on TCP %u (protocol v%d)", port, MM_PROTO_VERSION);

    while (running) {
        fd_set rd;
        struct timeval tv = {1, 0};
        sock_t maxfd = ls;
        int i;

        FD_ZERO(&rd);
        FD_SET(ls, &rd);
        for (i = 0; i < nclients; i++) {
            FD_SET(clients[i].sock, &rd);
            if (clients[i].sock > maxfd) maxfd = clients[i].sock;
        }
        if (select((int)maxfd + 1, &rd, NULL, NULL, &tv) < 0) continue;

        if (FD_ISSET(ls, &rd)) accept_client(ls);

        for (i = nclients - 1; i >= 0; i--) {
            client_t *c = &clients[i];
            char buf[2048], line[MM_LINE_MAX];
            int n;
            if (!FD_ISSET(c->sock, &rd)) continue;
            n = (int)recv(c->sock, buf, sizeof buf, 0);
            if (n <= 0) {            /* orderly close or error */
                drop_client(i);
                continue;
            }
            c->last_seen = now_ms();
            if (mm_linebuf_push(&c->lb, buf, (size_t)n) != 0) {
                send_error(c, "TOO_LARGE", "line exceeds 512 bytes");
                drop_client(i);
                continue;
            }
            while (mm_linebuf_pop(&c->lb, line, sizeof line)) handle_line(c, line);
        }

        /* Heartbeat: drop clients that went silent (lost network, closed lid). */
        if (now_ms() - last_sweep > 1000) {
            uint64_t t = now_ms();
            last_sweep = t;
            for (i = nclients - 1; i >= 0; i--)
                if (t - clients[i].last_seen > IDLE_TIMEOUT) {
                    log_msg("server", "timeout id=%s", clients[i].id);
                    drop_client(i);
                }
        }
    }

    log_msg("server", "shutting down");
    while (nclients) drop_client(nclients - 1);
    sock_close(ls);
    net_cleanup();
    return 0;
}
