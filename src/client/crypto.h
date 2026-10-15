/* End-to-end media encryption (an SRTP-style design built on libsodium).
 *
 * Key agreement: each client makes a fresh X25519 key pair at start-up and
 * sends the public key to every peer through signalling (KEY line). Both
 * sides derive the same pair of 256-bit session keys with crypto_kx
 * (X25519 + BLAKE2b): one key for each direction.
 * The server only relays public keys, so it can never decrypt the media.
 *
 * Media: ChaCha20-Poly1305 (IETF AEAD) per packet.
 *   RTP : 12-byte header stays clear (needed for demux / stats) but is
 *         authenticated as associated data; payload encrypted; 16-byte tag
 *         appended. Nonce = SSRC | seq | PT | timestamp | 0x00 (unique per packet).
 *   RTCP: first 8 bytes clear + authenticated, rest encrypted, then tag and a
 *         4-byte packet index. Nonce = index | zeros | 0x01.
 * Any modified or forged packet fails the tag check and is dropped.
 * STUN checks stay in the clear, as in ICE.
 *
 * Authenticity: a malicious signalling server could swap keys (man in the
 * middle). Both users can compare the short "safety code" shown on screen
 * (hash of both public keys), the same idea as Signal's safety numbers. */
#ifndef MM_CRYPTO_H
#define MM_CRYPTO_H

#include <stdint.h>
#include <stddef.h>

#define MMC_TAG_LEN   16
#define MMC_KEY_HEX   64

typedef struct {
    int      ready;
    uint8_t  rx[32], tx[32];
    char     safety[16];         /* e.g. "3F9A-C41D" */
    uint32_t rtcp_index;
} mm_crypto_t;

int    mmc_init(void);                         /* sodium_init + our key pair */
void   mmc_public_hex(char out[MMC_KEY_HEX + 1]);
int    mmc_valid_hex(const char *hex);
/* Derive session keys with a peer. Ids decide which side is "client". */
int    mmc_derive(mm_crypto_t *c, const char *self_id, const char *peer_id, const char *peer_pub_hex);

/* In-place. Return new length, or 0 if it does not fit / fails. */
size_t mmc_seal_rtp(const mm_crypto_t *c, uint8_t *pkt, size_t len, size_t cap);
size_t mmc_open_rtp(const mm_crypto_t *c, uint8_t *pkt, size_t len);
size_t mmc_seal_rtcp(mm_crypto_t *c, uint8_t *pkt, size_t len, size_t cap);
size_t mmc_open_rtcp(const mm_crypto_t *c, uint8_t *pkt, size_t len);

#endif
