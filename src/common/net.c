#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int net_init(void) {
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0 ? 0 : -1;
#else
    return 0;
#endif
}

void net_cleanup(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

void sock_close(sock_t s) {
    if (s == SOCK_INVALID) return;
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

int sock_set_nonblock(sock_t s, int on) {
#ifdef _WIN32
    u_long mode = on ? 1 : 0;
    return ioctlsocket(s, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int flags = fcntl(s, F_GETFL, 0);
    if (flags < 0) return -1;
    flags = on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return fcntl(s, F_SETFL, flags);
#endif
}

int sock_last_error(void) {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

int sock_would_block(void) {
#ifdef _WIN32
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAECONNRESET; /* ICMP port unreachable surfaces as CONNRESET on UDP */
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNREFUSED;
#endif
}

void ep_from_sockaddr(endpoint_t *ep, const struct sockaddr_in *sa) {
    ep->ip = sa->sin_addr.s_addr;
    ep->port = ntohs(sa->sin_port);
}

void ep_to_sockaddr(const endpoint_t *ep, struct sockaddr_in *sa) {
    memset(sa, 0, sizeof *sa);
    sa->sin_family = AF_INET;
    sa->sin_addr.s_addr = ep->ip;
    sa->sin_port = htons(ep->port);
}

const char *ep_str(const endpoint_t *ep, char *buf, size_t cap) {
    struct in_addr a;
    char ip[INET_ADDRSTRLEN];
    a.s_addr = ep->ip;
    inet_ntop(AF_INET, &a, ip, sizeof ip);
    snprintf(buf, cap, "%s:%u", ip, (unsigned)ep->port);
    return buf;
}

int ep_parse(const char *ip, const char *port, endpoint_t *out) {
    struct in_addr a;
    char *end;
    long p = strtol(port, &end, 10);
    if (*end || p <= 0 || p > 65535) return -1;
    if (inet_pton(AF_INET, ip, &a) != 1) return -1;
    out->ip = a.s_addr;
    out->port = (uint16_t)p;
    return 0;
}

int ep_equal(const endpoint_t *a, const endpoint_t *b) {
    return a->ip == b->ip && a->port == b->port;
}

int net_resolve(const char *host, uint16_t port, endpoint_t *out) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return -1;
    out->ip = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
    out->port = port;
    freeaddrinfo(res);
    return 0;
}

int net_local_ip(uint32_t *ip_out) {
    /* "Connecting" a UDP socket sends nothing; it only makes the OS pick the
     * outgoing interface, whose address getsockname() then reports. */
    struct sockaddr_in sa, me;
    socklen_t len = sizeof me;
    sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == SOCK_INVALID) return -1;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &sa.sin_addr);
    if (connect(s, (struct sockaddr *)&sa, sizeof sa) != 0 ||
        getsockname(s, (struct sockaddr *)&me, &len) != 0) {
        sock_close(s);
        return -1;
    }
    *ip_out = me.sin_addr.s_addr;
    sock_close(s);
    return 0;
}

sock_t tcp_listen(uint16_t port) {
    struct sockaddr_in sa;
    int yes = 1;
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == SOCK_INVALID) return SOCK_INVALID;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(port);
    if (bind(s, (struct sockaddr *)&sa, sizeof sa) != 0 || listen(s, 16) != 0) {
        sock_close(s);
        return SOCK_INVALID;
    }
    return s;
}

sock_t tcp_connect(const char *host, uint16_t port) {
    endpoint_t ep;
    struct sockaddr_in sa;
    int yes = 1;
    sock_t s;
    if (net_resolve(host, port, &ep) != 0) return SOCK_INVALID;
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == SOCK_INVALID) return SOCK_INVALID;
    ep_to_sockaddr(&ep, &sa);
    if (connect(s, (struct sockaddr *)&sa, sizeof sa) != 0) {
        sock_close(s);
        return SOCK_INVALID;
    }
    /* Signalling lines are tiny; send them immediately instead of waiting for Nagle. */
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&yes, sizeof yes);
    return s;
}

sock_t udp_bind(uint16_t port) {
    struct sockaddr_in sa;
    int buf = 1 << 20;
    sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == SOCK_INVALID) return SOCK_INVALID;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(port);
    if (bind(s, (struct sockaddr *)&sa, sizeof sa) != 0) {
        sock_close(s);
        return SOCK_INVALID;
    }
    /* Bigger kernel buffers so a burst of video packets is not dropped locally. */
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char *)&buf, sizeof buf);
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char *)&buf, sizeof buf);
#ifdef _WIN32
    {
        /* Stop Windows reporting ICMP "port unreachable" as a recv error on UDP. */
        BOOL off = FALSE;
        DWORD ret = 0;
        WSAIoctl(s, _WSAIOW(IOC_VENDOR, 12) /* SIO_UDP_CONNRESET */, &off, sizeof off, NULL, 0, &ret, NULL, NULL);
    }
#endif
    sock_set_nonblock(s, 1);
    return s;
}

int udp_send(sock_t s, const endpoint_t *to, const void *buf, size_t len) {
    struct sockaddr_in sa;
    ep_to_sockaddr(to, &sa);
    return (int)sendto(s, (const char *)buf, (int)len, 0, (struct sockaddr *)&sa, sizeof sa);
}

int udp_recv(sock_t s, endpoint_t *from, void *buf, size_t cap) {
    struct sockaddr_in sa;
    socklen_t len = sizeof sa;
    int n = (int)recvfrom(s, (char *)buf, (int)cap, 0, (struct sockaddr *)&sa, &len);
    if (n >= 0 && from) ep_from_sockaddr(from, &sa);
    return n;
}
