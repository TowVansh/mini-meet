#include "stun.h"
#include "../common/util.h"

#include <string.h>

/* Header layout (20 bytes):
 *   0: type (16)   2: length of attributes (16)   4: magic cookie (32)   8: transaction ID (96)
 * Each attribute: type (16), length (16), value padded to a 4-byte boundary. */

int stun_is_stun(const uint8_t *buf, size_t len) {
    return len >= STUN_HEADER_LEN && (buf[0] & 0xC0) == 0 && get_u32(buf + 4) == STUN_MAGIC_COOKIE;
}

void stun_new_tid(uint8_t tid[12]) {
    int i;
    for (i = 0; i < 12; i += 4) put_u32(tid + i, rand_u32());
}

static size_t put_header(uint8_t *out, uint16_t type, const uint8_t tid[12]) {
    put_u16(out, type);
    put_u16(out + 2, 0);
    put_u32(out + 4, STUN_MAGIC_COOKIE);
    memcpy(out + 8, tid, 12);
    return STUN_HEADER_LEN;
}

static size_t put_attr(uint8_t *out, size_t off, size_t cap, uint16_t type, const void *val, uint16_t len) {
    size_t padded = (len + 3u) & ~3u;
    if (off + 4 + padded > cap) return off;
    put_u16(out + off, type);
    put_u16(out + off + 2, len);
    memcpy(out + off + 4, val, len);
    memset(out + off + 4 + len, 0, padded - len);
    return off + 4 + padded;
}

static void finish(uint8_t *out, size_t total) {
    put_u16(out + 2, (uint16_t)(total - STUN_HEADER_LEN));
}

size_t stun_build_request(uint8_t *out, size_t cap, const uint8_t tid[12], const char *username) {
    size_t n;
    if (cap < STUN_HEADER_LEN) return 0;
    n = put_header(out, STUN_BINDING_REQUEST, tid);
    if (username && *username) n = put_attr(out, n, cap, STUN_ATTR_USERNAME, username, (uint16_t)strlen(username));
    finish(out, n);
    return n;
}

size_t stun_build_success(uint8_t *out, size_t cap, const uint8_t tid[12], const endpoint_t *mapped) {
    uint8_t v[8];
    uint32_t ip_host = ntohl(mapped->ip);
    size_t n;
    if (cap < STUN_HEADER_LEN) return 0;
    n = put_header(out, STUN_BINDING_SUCCESS, tid);
    /* XOR-MAPPED-ADDRESS: port XOR top 16 bits of cookie, address XOR cookie.
     * Obfuscating it stops NAT "helpers" that rewrite IPs inside payloads. */
    v[0] = 0;
    v[1] = 0x01; /* IPv4 */
    put_u16(v + 2, (uint16_t)(mapped->port ^ (STUN_MAGIC_COOKIE >> 16)));
    put_u32(v + 4, ip_host ^ STUN_MAGIC_COOKIE);
    n = put_attr(out, n, cap, STUN_ATTR_XOR_MAPPED_ADDRESS, v, 8);
    n = put_attr(out, n, cap, STUN_ATTR_SOFTWARE, "mini-meet", 9);
    finish(out, n);
    return n;
}

int stun_parse(const uint8_t *buf, size_t len, stun_msg_t *msg) {
    size_t off = STUN_HEADER_LEN, end;
    memset(msg, 0, sizeof *msg);
    if (!stun_is_stun(buf, len)) return -1;
    msg->type = get_u16(buf);
    memcpy(msg->tid, buf + 8, 12);
    end = STUN_HEADER_LEN + get_u16(buf + 2);
    if (end > len) return -1;
    while (off + 4 <= end) {
        uint16_t at = get_u16(buf + off), al = get_u16(buf + off + 2);
        const uint8_t *v = buf + off + 4;
        if (off + 4 + al > end) return -1;
        if ((at == STUN_ATTR_XOR_MAPPED_ADDRESS || at == STUN_ATTR_MAPPED_ADDRESS) && al >= 8 && v[1] == 0x01) {
            uint16_t port = get_u16(v + 2);
            uint32_t ip = get_u32(v + 4);
            if (at == STUN_ATTR_XOR_MAPPED_ADDRESS) {
                port ^= (uint16_t)(STUN_MAGIC_COOKIE >> 16);
                ip ^= STUN_MAGIC_COOKIE;
            }
            if (!msg->has_mapped || at == STUN_ATTR_XOR_MAPPED_ADDRESS) {
                msg->mapped.ip = htonl(ip);
                msg->mapped.port = port;
                msg->has_mapped = 1;
            }
        } else if (at == STUN_ATTR_USERNAME && al < sizeof msg->username) {
            memcpy(msg->username, v, al);
            msg->username[al] = '\0';
        }
        off += 4 + ((al + 3u) & ~3u);
    }
    return 0;
}
