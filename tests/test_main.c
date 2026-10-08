/* Unit tests for the protocol code (no network, no media). Run: make test */
#include "../src/common/net.h"
#include "../src/common/proto.h"
#include "../src/common/util.h"
#include "../src/stun/stun.h"
#include "../src/client/rtp.h"
#include "../src/client/cc.h"

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

static void test_cc(void) {
    cc_state_t cc;
    int i;
    cc_init(&cc);
    CHECK(cc_on_report(&cc, 0.0, 20) == 0 && cc.level == 0);
    CHECK(cc_on_report(&cc, 0.15, 20) == 1 && cc.level == 2);   /* loss: drop two levels */
    CHECK(cc_on_report(&cc, 0.0, 200) == 1 && cc.level == 4);   /* 180 ms queuing delay: drop again */
    for (i = 0; i < 3; i++) cc_on_report(&cc, 0.0, 25);
    CHECK(cc.level == 3);                                       /* three clean reports: up one */
    for (i = 0; i < 30; i++) cc_on_report(&cc, 0.0, 25);
    CHECK(cc.level == 0);
}

int main(void) {
    net_init();
    test_tokenize();
    test_linebuf();
    test_stun();
    test_rtp();
    test_rtcp();
    test_rx_stats();
    test_cc();
    printf("%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
