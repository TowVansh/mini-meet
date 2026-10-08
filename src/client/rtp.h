/* RTP (RFC 3550) and the RTCP messages Mini-Meet uses:
 *   SR  (200) sender report      RR (201) receiver report
 *   RTPFB/NACK (205, FMT 1)      PSFB/PLI (206, FMT 1)
 * All functions are pure (no I/O) so they can be unit-tested. */
#ifndef MM_RTP_H
#define MM_RTP_H

#include <stdint.h>
#include <stddef.h>

#define RTP_HDR_LEN     12
#define RTP_PT_VP8      96
#define RTP_PT_VP8_RTX  97      /* NACK retransmissions, excluded from loss stats */
#define RTP_PT_OPUS     111
#define RTP_CLOCK_VIDEO 90000
#define RTP_CLOCK_AUDIO 48000
#define RTP_MAX_PAYLOAD 1160    /* keeps IP packets under ~1200 bytes, safely below a 1500 MTU */

/* 1-byte video payload descriptor that follows the RTP header. */
#define VP_START  0x80          /* first packet of a frame */
#define VP_KEY    0x40          /* frame is a keyframe */

typedef struct {
    int      marker;            /* video: last packet of a frame */
    uint8_t  pt;
    uint16_t seq;
    uint32_t ts;
    uint32_t ssrc;
} rtp_hdr_t;

size_t rtp_write_header(uint8_t *out, const rtp_hdr_t *h);
/* Returns payload offset, or -1 if not a valid RTP packet. */
int    rtp_parse_header(const uint8_t *buf, size_t len, rtp_hdr_t *h);
/* RTCP packet types 200..206 sit where an RTP header has marker+PT (RFC 5761). */
int    rtp_is_rtcp(const uint8_t *buf, size_t len);

#define RTCP_SR    200
#define RTCP_RR    201
#define RTCP_RTPFB 205
#define RTCP_PSFB  206
#define RTCP_MAX_BLOCKS 4
#define RTCP_MAX_NACK   32

typedef struct {
    uint32_t ssrc;              /* source this block reports on */
    uint8_t  fraction_lost;     /* lost/expected since last report, x256 */
    int32_t  cum_lost;          /* 24-bit signed */
    uint32_t ext_high_seq;
    uint32_t jitter;            /* RTP timestamp units */
    uint32_t lsr;               /* middle 32 bits of last SR NTP time */
    uint32_t dlsr;              /* delay since that SR, 1/65536 s */
} rtcp_block_t;

/* One parsed compound RTCP datagram. */
typedef struct {
    int          has_sr;
    uint32_t     sender_ssrc;
    uint64_t     sr_ntp;
    uint32_t     sr_rtp_ts, sr_packets, sr_octets;
    int          nblocks;
    rtcp_block_t blocks[RTCP_MAX_BLOCKS];
    int          nnack;
    uint16_t     nack[RTCP_MAX_NACK * 17];  /* expanded lost sequence numbers */
    int          pli;
} rtcp_info_t;

size_t rtcp_write_sr(uint8_t *out, uint32_t ssrc, uint64_t ntp, uint32_t rtp_ts, uint32_t packets,
                     uint32_t octets, const rtcp_block_t *blocks, int nblocks);
/* Generic NACK: each FCI holds a packet ID plus a 16-bit mask of the following lost packets. */
size_t rtcp_write_nack(uint8_t *out, uint32_t ssrc, uint32_t media_ssrc, const uint16_t *seqs, int n);
size_t rtcp_write_pli(uint8_t *out, uint32_t ssrc, uint32_t media_ssrc);
int    rtcp_parse(const uint8_t *buf, size_t len, rtcp_info_t *info);

/* NTP-format time from a monotonic microsecond clock (only differences matter). */
uint64_t ntp_from_us(uint64_t us);
static inline uint32_t ntp_middle32(uint64_t ntp) { return (uint32_t)(ntp >> 16); }

/* Receiver-side statistics for one incoming stream (RFC 3550 appendix A). */
typedef struct {
    int      init;
    uint32_t ssrc;
    uint32_t clock_rate;
    uint16_t max_seq;
    uint32_t cycles;
    uint32_t base_seq;
    uint32_t received;
    uint32_t expected_prior, received_prior;
    double   jitter;            /* RTP ts units */
    int64_t  last_transit;
    int      have_transit;
    uint32_t last_sr;           /* middle 32 of peer's last SR */
    uint64_t last_sr_us;        /* when we received it */
} rx_stats_t;

void rx_stats_init(rx_stats_t *s, uint32_t ssrc, uint32_t clock_rate, uint16_t first_seq);
void rx_stats_update(rx_stats_t *s, uint16_t seq, uint32_t rtp_ts, uint64_t arrival_us);
/* Fills a report block and advances the "prior" counters (one call per report). */
void rx_stats_block(rx_stats_t *s, uint64_t now_us, rtcp_block_t *b);

#endif
