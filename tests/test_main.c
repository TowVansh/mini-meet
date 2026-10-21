/* Unit tests for the protocol code (no network, no media). Run: make test */
#include "../src/common/net.h"
#include "../src/common/proto.h"
#include "../src/common/util.h"
#include "../src/stun/stun.h"
#include "../src/client/rtp.h"
#include "../src/client/cc.h"
#include "../src/client/crypto.h"

#include <sodium.h>
#include <stdio.h>
#include <string.h>

static int failures, checks;
#define CHECK(cond)                                                              \
    do {                                                                         \
        checks++;                                                                \
        if (!(cond)) {                                                           \
            failures++;                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                        \
    } while (0)

static void test_tokenize(void) {
    char line[] = "CAND 1a2b3c4d srflx 1.2.3.4 5000\r";
    mm_msg_t m;
    CHECK(mm_tokenize(line, &m) == 5);
    CHECK(strcmp(m.argv[0], "CAND") == 0);
    CHECK(strcmp(m.argv[4], "5000") == 0);
    CHECK(mm_valid_name("room_1-A"));
    CHECK(!mm_valid_name("bad room"));
    CHECK(!mm_valid_name(""));
    CHECK(!mm_valid_name("0123456789012345678901234567890123"));
    CHECK(mm_valid_cand_type("host") && mm_valid_cand_type("srflx") && !mm_valid_cand_type("relay"));
}

static void test_linebuf(void) {
    mm_linebuf_t lb = {{0}, 0};
    char out[MM_LINE_MAX], big[MM_LINE_MAX + 10];
    CHECK(mm_linebuf_push(&lb, "JOIN ro", 7) == 0);
    CHECK(mm_linebuf_pop(&lb, out, sizeof out) == 0);          /* incomplete: TCP split the line */
    CHECK(mm_linebuf_push(&lb, "om alice\nPING\n", 14) == 0);
    CHECK(mm_linebuf_pop(&lb, out, sizeof out) == 1 && strcmp(out, "JOIN room alice") == 0);
    CHECK(mm_linebuf_pop(&lb, out, sizeof out) == 1 && strcmp(out, "PING") == 0);
    CHECK(mm_linebuf_pop(&lb, out, sizeof out) == 0);
    memset(big, 'x', sizeof big);
    CHECK(mm_linebuf_push(&lb, big, sizeof big) == -1);        /* over-long line rejected */
}

static void test_stun(void) {
    uint8_t tid[12], buf[256];
    stun_msg_t m;
    endpoint_t ep;
    size_t n;
    stun_new_tid(tid);
    n = stun_build_request(buf, sizeof buf, tid, "abcd1234:ffff0000");
    CHECK(n == 20 + 4 + 20);                                    /* 17-byte username padded to 20 */
    CHECK(stun_is_stun(buf, n));
    CHECK(stun_parse(buf, n, &m) == 0);
    CHECK(m.type == STUN_BINDING_REQUEST && memcmp(m.tid, tid, 12) == 0);
    CHECK(strcmp(m.username, "abcd1234:ffff0000") == 0);

    ep_parse("203.0.113.7", "54321", &ep);
    n = stun_build_success(buf, sizeof buf, tid, &ep);
    CHECK(stun_parse(buf, n, &m) == 0);
    CHECK(m.type == STUN_BINDING_SUCCESS && m.has_mapped && ep_equal(&m.mapped, &ep));
    /* The address on the wire is XORed, so the plain bytes must not appear. */
    CHECK(get_u16(buf + 20 + 4 + 2) != 54321);

    buf[0] = 0x80;                                              /* RTP-looking first byte */
    CHECK(!stun_is_stun(buf, n));
}

static void test_rtp(void) {
    uint8_t buf[64];
    rtp_hdr_t h = {1, RTP_PT_VP8, 65535, 123456, 0xdeadbeef}, o;
    CHECK(rtp_write_header(buf, &h) == RTP_HDR_LEN);
    CHECK(rtp_parse_header(buf, RTP_HDR_LEN, &o) == RTP_HDR_LEN);
    CHECK(o.marker == 1 && o.pt == RTP_PT_VP8 && o.seq == 65535 && o.ts == 123456 && o.ssrc == 0xdeadbeef);
    CHECK(!rtp_is_rtcp(buf, RTP_HDR_LEN));                      /* marker+PT 96 = 224, outside 200..206 */
    CHECK(seq_newer(2, 65534));                                 /* wrap-around */
    CHECK(!seq_newer(65534, 2));
}

static void test_rtcp(void) {
    uint8_t buf[512];
    rtcp_block_t b = {0x11111111, 64, 1234, 70000, 90, 0xAABBCCDD, 6553}, bs[1];
    rtcp_info_t info;
    uint16_t lost[] = {100, 101, 105, 117, 200};
    size_t n;
    bs[0] = b;
    n = rtcp_write_sr(buf, 0x22222222, 0x0102030405060708ull, 9000, 50, 60000, bs, 1);
    n += rtcp_write_nack(buf + n, 0x22222222, 0x33333333, lost, 5);
    n += rtcp_write_pli(buf + n, 0x22222222, 0x33333333);
    CHECK(rtp_is_rtcp(buf, n));
    CHECK(rtcp_parse(buf, n, &info) == 0);
    CHECK(info.has_sr && info.sender_ssrc == 0x22222222 && info.sr_ntp == 0x0102030405060708ull);
    CHECK(info.nblocks == 1 && info.blocks[0].fraction_lost == 64 && info.blocks[0].cum_lost == 1234);
    CHECK(info.blocks[0].lsr == 0xAABBCCDD && info.blocks[0].dlsr == 6553);
    /* 100 101 105 117 fit in one FCI (pid 100 + mask); 200 needs a second one. */
    CHECK(info.nnack == 5);
    CHECK(info.nack[0] == 100 && info.nack[1] == 101 && info.nack[2] == 105 && info.nack[3] == 117 && info.nack[4] == 200);
    CHECK(info.pli == 1);

    b.cum_lost = -5;                                            /* 24-bit sign extension */
    bs[0] = b;
    n = rtcp_write_sr(buf, 1, 0, 0, 0, 0, bs, 1);
    rtcp_parse(buf, n, &info);
    CHECK(info.blocks[0].cum_lost == -5);
}

static void test_rx_stats(void) {
    rx_stats_t s;
    rtcp_block_t b;
    uint16_t seq;
    uint32_t ts = 0;
    uint64_t t = 1000000;
    rx_stats_init(&s, 7, 90000, 65530);
    /* 20 packets across the 16-bit wrap, every 4th one lost, 50 ms apart on both clocks */
    for (seq = 65530; seq != 14; seq++) {
        if (seq % 4 != 0) rx_stats_update(&s, seq, ts, t);
        t += 50000;
        ts += 4500;
    }
    rx_stats_block(&s, t, &b);
    CHECK(b.ext_high_seq == 65536 + 13);
    CHECK(b.cum_lost == 5);                                     /* 65532, 0, 4, 8, 12 */
    CHECK(b.fraction_lost == (5 * 256) / 20);
    /* Constant spacing on both clocks: jitter should stay near zero. */
    CHECK(b.jitter < 50);
}

static void test_playout(void) {
    playout_t po;
    const uint64_t t0 = 1000000000ull;
    const uint32_t ts0 = 4294967000u;                           /* the 32-bit timestamp wraps during the test */
    int64_t d;
    int i;
    playout_reset(&po);
    CHECK(playout_due_us(&po, ts0, 90000) == 0);                /* no data yet: play at once */
    /* 64 frames sent 50 ms apart, 100 ms network delay plus 0..63 ms of jitter */
    for (i = 0; i < 64; i++) {
        uint32_t ts = ts0 + (uint32_t)i * 4500u;
        uint64_t arrival = t0 + (uint64_t)i * 50000 + 100000 + (uint64_t)((i * 37) % 64) * 1000;
        playout_add(&po, ts, 90000, arrival);
    }
    /* the buffer holds back to the 95th percentile: ~60 ms over the fastest frame */
    CHECK(playout_buffer_ms(&po) > 55 && playout_buffer_ms(&po) < 64);
    /* playout keeps the sender's 50 ms spacing, however unevenly frames arrived */
    d = (int64_t)playout_due_us(&po, ts0 + 10u * 4500u, 90000) - (int64_t)playout_due_us(&po, ts0 + 9u * 4500u, 90000);
    CHECK(d >= 49999 && d <= 50001);
    /* and a frame is due ~160 ms after it was sent (100 ms delay + 60 ms buffer) */
    d = (int64_t)playout_due_us(&po, ts0 + 9u * 4500u, 90000) - (int64_t)(t0 + 9u * 50000u);
    CHECK(d > 155000 && d < 165000);
    /* buffer off (--no-jb): paced by the fastest frame, no jitter allowance */
    po.min_only = 1;
    d = (int64_t)playout_due_us(&po, ts0 + 9u * 4500u, 90000) - (int64_t)(t0 + 9u * 50000u);
    CHECK(d > 99000 && d < 101000 && playout_buffer_ms(&po) == 0);
}

static void test_cc(void) {
    cc_state_t cc;
    int i;
    cc_init(&cc);
    CHECK(cc.level == CC_START_LEVEL);                          /* starts low (slow start) */
    for (i = 0; i < CC_START_LEVEL; i++) cc_on_report(&cc, 0.0, 20);
    CHECK(cc.level == 0);                                       /* startup: up one level per clean report */
    CHECK(cc_on_report(&cc, 0.15, 20) == 1 && cc.level == 2);   /* loss: drop two levels */
    for (i = 0; i < 3 && cc.level == 2; i++) cc_on_report(&cc, 0.0, 300);
    CHECK(cc.level == 4);                                       /* queue building (RTT 20 -> 300): drop again */
    for (i = 0; i < 40; i++) cc_on_report(&cc, 0.0, 25);
    CHECK(cc.level == 0);                                       /* clean reports: climb back one level at a time */

    /* A path that simply gets longer (fixed +200 ms) must not stay "congested":
     * once the 10 s minimum window has moved past the old samples, it recovers. */
    cc_init(&cc);
    for (i = 0; i < 10; i++) cc_on_report(&cc, 0.0, 1);
    for (i = 0; i < 30; i++) cc_on_report(&cc, 0.0, 200);
    CHECK(cc.level == 0);

    /* ...but a standing multi-second queue must never become the new "normal". */
    cc_init(&cc);
    for (i = 0; i < 10; i++) cc_on_report(&cc, 0.0, 20);
    for (i = 0; i < 40; i++) cc_on_report(&cc, 0.0, 3000);
    CHECK(cc.level == CC_LEVELS - 1);
}

static void test_crypto(void) {
    mm_crypto_t a, b;
    uint8_t pkt[256], copy[256];
    char pub[MMC_KEY_HEX + 1];
    rtp_hdr_t h = {1, RTP_PT_VP8, 1000, 90000, 0x1234};
    size_t n, sealed;
    rtcp_info_t info;

    /* The module holds one key pair per process, so derive against our own
     * public key and make "a" the receiver of b's sending direction.
     * (Two-party agreement itself is checked in test_key_agreement.) */
    CHECK(mmc_init() == 0);
    mmc_public_hex(pub);
    CHECK(mmc_derive(&b, "bbbbbbbb", "aaaaaaaa", pub) == 0);
    CHECK(mmc_derive(&b, "bbbbbbbb", "aaaaaaaa", "zz") != 0);  /* malformed key rejected */
    a = b;
    memcpy(a.rx, b.tx, 32);

    n = rtp_write_header(pkt, &h);
    memcpy(pkt + n, "hello video", 11);
    n += 11;
    memcpy(copy, pkt, n);
    sealed = mmc_seal_rtp(&b, pkt, n, sizeof pkt);
    CHECK(sealed == n + MMC_TAG_LEN);
    CHECK(memcmp(pkt, copy, 12) == 0);                          /* header stays readable */
    CHECK(memcmp(pkt + 12, copy + 12, 11) != 0);                /* payload is not */
    CHECK(mmc_open_rtp(&a, pkt, sealed) == n && memcmp(pkt, copy, n) == 0);

    sealed = mmc_seal_rtp(&b, pkt, n, sizeof pkt);
    pkt[3] ^= 1;                                                /* tamper with the (clear) sequence number */
    CHECK(mmc_open_rtp(&a, pkt, sealed) == 0);                  /* authentication catches it */

    n = rtcp_write_pli(pkt, 1, 2);
    sealed = mmc_seal_rtcp(&b, pkt, n, sizeof pkt);
    CHECK(rtp_is_rtcp(pkt, sealed));                            /* still demultiplexes as RTCP */
    CHECK(mmc_open_rtcp(&a, pkt, sealed) == n);
    CHECK(rtcp_parse(pkt, n, &info) == 0 && info.pli == 1);

    CHECK(b.safety[4] == '-' && strlen(b.safety) == 9);
    CHECK(!mmc_valid_hex("xyz") && mmc_valid_hex(pub));
    CHECK(mm_valid_key_hex(pub) && !mm_valid_key_hex("00"));
}

/* Full key agreement between two independent key pairs (libsodium directly). */
static void test_key_agreement(void) {
    uint8_t apk[32], ask[32], bpk[32], bsk[32], arx[32], atx[32], brx[32], btx[32];
    crypto_kx_keypair(apk, ask);
    crypto_kx_keypair(bpk, bsk);
    CHECK(crypto_kx_client_session_keys(arx, atx, apk, ask, bpk) == 0);
    CHECK(crypto_kx_server_session_keys(brx, btx, bpk, bsk, apk) == 0);
    CHECK(memcmp(atx, brx, 32) == 0 && memcmp(btx, arx, 32) == 0);   /* both sides agree, per direction */
}

int main(void) {
    net_init();
    test_tokenize();
    test_linebuf();
    test_stun();
    test_rtp();
    test_rtcp();
    test_rx_stats();
    test_playout();
    test_cc();
    test_crypto();
    test_key_agreement();
    printf("%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
