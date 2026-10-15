#include "crypto.h"
#include "../common/util.h"

#include <string.h>
#include <stdio.h>
#include <sodium.h>

static uint8_t my_pk[crypto_kx_PUBLICKEYBYTES], my_sk[crypto_kx_SECRETKEYBYTES];

int mmc_init(void) {
    if (sodium_init() < 0) return -1;
    crypto_kx_keypair(my_pk, my_sk);
    return 0;
}

void mmc_public_hex(char out[MMC_KEY_HEX + 1]) {
    sodium_bin2hex(out, MMC_KEY_HEX + 1, my_pk, sizeof my_pk);
}

int mmc_valid_hex(const char *hex) {
    size_t i;
    if (strlen(hex) != MMC_KEY_HEX) return 0;
    for (i = 0; i < MMC_KEY_HEX; i++) {
        char c = hex[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return 0;
    }
    return 1;
}

int mmc_derive(mm_crypto_t *c, const char *self_id, const char *peer_id, const char *peer_pub_hex) {
    uint8_t peer_pk[crypto_kx_PUBLICKEYBYTES], h[8];
    crypto_generichash_state st;
    int as_client = strcmp(self_id, peer_id) < 0;
    if (!mmc_valid_hex(peer_pub_hex) ||
        sodium_hex2bin(peer_pk, sizeof peer_pk, peer_pub_hex, MMC_KEY_HEX, NULL, NULL, NULL) != 0)
        return -1;
    /* crypto_kx gives (rx, tx); the "client" side's tx equals the "server" side's rx. */
    if (as_client ? crypto_kx_client_session_keys(c->rx, c->tx, my_pk, my_sk, peer_pk)
                  : crypto_kx_server_session_keys(c->rx, c->tx, my_pk, my_sk, peer_pk))
        return -1;
    /* Safety code: hash of both public keys in a fixed order, so both sides show the same code. */
    crypto_generichash_init(&st, NULL, 0, sizeof h);
    crypto_generichash_update(&st, as_client ? my_pk : peer_pk, sizeof my_pk);
    crypto_generichash_update(&st, as_client ? peer_pk : my_pk, sizeof my_pk);
    crypto_generichash_final(&st, h, sizeof h);
    snprintf(c->safety, sizeof c->safety, "%02X%02X-%02X%02X", h[0], h[1], h[2], h[3]);
    c->rtcp_index = 0;
    c->ready = 1;
    return 0;
}

static void rtp_nonce(const uint8_t *pkt, uint8_t n[12]) {
    memcpy(n, pkt + 8, 4);          /* SSRC */
    memcpy(n + 4, pkt + 2, 2);      /* sequence number */
    n[6] = pkt[1] & 0x7F;           /* payload type: a retransmission (PT 97) gets its own nonce */
    memcpy(n + 7, pkt + 4, 4);      /* timestamp */
    n[11] = 0;
}

size_t mmc_seal_rtp(const mm_crypto_t *c, uint8_t *pkt, size_t len, size_t cap) {
    uint8_t n[12];
    unsigned long long clen;
    if (len < 12 || len + MMC_TAG_LEN > cap) return 0;
    rtp_nonce(pkt, n);
    crypto_aead_chacha20poly1305_ietf_encrypt(pkt + 12, &clen, pkt + 12, len - 12, pkt, 12, NULL, n, c->tx);
    return 12 + (size_t)clen;
}

size_t mmc_open_rtp(const mm_crypto_t *c, uint8_t *pkt, size_t len) {
    uint8_t n[12];
    unsigned long long mlen;
    if (len < 12 + MMC_TAG_LEN) return 0;
    rtp_nonce(pkt, n);
    if (crypto_aead_chacha20poly1305_ietf_decrypt(pkt + 12, &mlen, NULL, pkt + 12, len - 12, pkt, 12, n, c->rx) != 0)
        return 0;
    return 12 + (size_t)mlen;
}

size_t mmc_seal_rtcp(mm_crypto_t *c, uint8_t *pkt, size_t len, size_t cap) {
    uint8_t n[12] = {0};
    unsigned long long clen;
    uint32_t idx = ++c->rtcp_index;
    if (len < 8 || len + MMC_TAG_LEN + 4 > cap) return 0;
    put_u32(n, idx);
    n[11] = 1;
    crypto_aead_chacha20poly1305_ietf_encrypt(pkt + 8, &clen, pkt + 8, len - 8, pkt, 8, NULL, n, c->tx);
    put_u32(pkt + 8 + clen, idx);
    return 8 + (size_t)clen + 4;
}

size_t mmc_open_rtcp(const mm_crypto_t *c, uint8_t *pkt, size_t len) {
    uint8_t n[12] = {0};
    unsigned long long mlen;
    if (len < 8 + MMC_TAG_LEN + 4) return 0;
    memcpy(n, pkt + len - 4, 4);
    n[11] = 1;
    if (crypto_aead_chacha20poly1305_ietf_decrypt(pkt + 8, &mlen, NULL, pkt + 8, len - 8 - 4, pkt, 8, n, c->rx) != 0)
        return 0;
    return 8 + (size_t)mlen;
}
