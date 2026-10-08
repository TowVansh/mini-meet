/* STUN (RFC 5389) message encoding and decoding: just the parts needed for
 * NAT discovery (Binding request/response with XOR-MAPPED-ADDRESS) and for
 * ICE-style connectivity checks between peers (USERNAME attribute). */
#ifndef MM_STUN_H
#define MM_STUN_H

#include <stdint.h>
#include <stddef.h>
#include "../common/net.h"

#define STUN_HEADER_LEN       20
#define STUN_MAGIC_COOKIE     0x2112A442u
#define STUN_BINDING_REQUEST  0x0001
#define STUN_BINDING_SUCCESS  0x0101
#define STUN_ATTR_MAPPED_ADDRESS     0x0001
#define STUN_ATTR_USERNAME           0x0006
#define STUN_ATTR_XOR_MAPPED_ADDRESS 0x0020
#define STUN_ATTR_SOFTWARE           0x8022

typedef struct {
    uint16_t   type;
    uint8_t    tid[12];          /* transaction ID */
    int        has_mapped;
    endpoint_t mapped;           /* from XOR-MAPPED-ADDRESS (or MAPPED-ADDRESS) */
    char       username[64];     /* "" if absent */
} stun_msg_t;

/* First byte 0..3 and the magic cookie at bytes 4..7 identify STUN. RTP/RTCP
 * start with 0x80..0xBF, so one UDP port can carry both (RFC 7983 demux). */
int    stun_is_stun(const uint8_t *buf, size_t len);

size_t stun_build_request(uint8_t *out, size_t cap, const uint8_t tid[12], const char *username);
size_t stun_build_success(uint8_t *out, size_t cap, const uint8_t tid[12], const endpoint_t *mapped);
int    stun_parse(const uint8_t *buf, size_t len, stun_msg_t *msg);
void   stun_new_tid(uint8_t tid[12]);

#endif
