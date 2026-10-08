/* Portable socket layer: hides the differences between Winsock (Windows)
 * and BSD sockets (Linux) so the rest of the code uses one API. */
#ifndef MM_NET_H
#define MM_NET_H

#ifdef _WIN32
#  ifndef FD_SETSIZE
#    define FD_SETSIZE 256          /* Winsock default is only 64 */
#  endif
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0600     /* Vista+: inet_pton / inet_ntop */
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
typedef SOCKET sock_t;
#  define SOCK_INVALID INVALID_SOCKET
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <sys/select.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
typedef int sock_t;
#  define SOCK_INVALID (-1)
#endif

#include <stdint.h>
#include <stddef.h>

/* IPv4 endpoint in host-friendly form. */
typedef struct {
    uint32_t ip;    /* network byte order */
    uint16_t port;  /* host byte order */
} endpoint_t;

int  net_init(void);
void net_cleanup(void);
void sock_close(sock_t s);
int  sock_set_nonblock(sock_t s, int on);
int  sock_would_block(void);       /* last error was EWOULDBLOCK/EAGAIN */
int  sock_last_error(void);

/* TCP helpers */
sock_t tcp_listen(uint16_t port);
sock_t tcp_connect(const char *host, uint16_t port);

/* UDP helpers */
sock_t udp_bind(uint16_t port);    /* port 0 = any */
int    udp_send(sock_t s, const endpoint_t *to, const void *buf, size_t len);
int    udp_recv(sock_t s, endpoint_t *from, void *buf, size_t cap);

/* Resolve "host" (name or dotted quad) to an IPv4 address. 0 on success. */
int  net_resolve(const char *host, uint16_t port, endpoint_t *out);
/* Primary local IPv4 address (the interface used for the default route). */
int  net_local_ip(uint32_t *ip_out);

void ep_from_sockaddr(endpoint_t *ep, const struct sockaddr_in *sa);
void ep_to_sockaddr(const endpoint_t *ep, struct sockaddr_in *sa);
const char *ep_str(const endpoint_t *ep, char *buf, size_t cap); /* "1.2.3.4:5678" */
int  ep_parse(const char *ip, const char *port, endpoint_t *out);
int  ep_equal(const endpoint_t *a, const endpoint_t *b);

#endif
