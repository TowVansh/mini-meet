#include "rtp.h"
#include "../common/util.h"

#include <string.h>
#include <math.h>

size_t rtp_write_header(uint8_t *out, const rtp_hdr_t *h) {
    out[0] = 0x80;                                      /* V=2, P=0, X=0, CC=0 */
    out[1] = (uint8_t)((h->marker ? 0x80 : 0) | (h->pt & 0x7F));
    put_u16(out + 2, h->seq);
    put_u32(out + 4, h->ts);
    put_u32(out + 8, h->ssrc);
    return RTP_HDR_LEN;
}

int rtp_parse_header(const uint8_t *buf, size_t len, rtp_hdr_t *h) {
    size_t off = RTP_HDR_LEN;
    if (len < RTP_HDR_LEN || (buf[0] >> 6) != 2) return -1;
    h->marker = (buf[1] & 0x80) != 0;
    h->pt = buf[1] & 0x7F;
    h->seq = get_u16(buf + 2);
    h->ts = get_u32(buf + 4);
    h->ssrc = get_u32(buf + 8);
    off += (size_t)(buf[0] & 0x0F) * 4;                 /* skip CSRCs */
    if (buf[0] & 0x10) {                                /* skip header extension */
        if (len < off + 4) return -1;
        off += 4 + (size_t)get_u16(buf + off + 2) * 4;
    }
    if (off > len) return -1;
    if (buf[0] & 0x20) {                                /* padding */
        uint8_t pad = buf[len - 1];
        if (pad > len - off) return -1;
    }
    return (int)off;
}

int rtp_is_rtcp(const uint8_t *buf, size_t len) {
    return len >= 8 && (buf[0] >> 6) == 2 && buf[1] >= 200 && buf[1] <= 206;
}

uint64_t ntp_from_us(uint64_t us) {
    uint64_t sec = us / 1000000ull, rem = us % 1000000ull;
    return (sec << 32) | ((rem << 32) / 1000000ull);
}

static void write_block(uint8_t *p, const rtcp_block_t *b) {
    uint32_t lost = (uint32_t)b->cum_lost & 0x00FFFFFFu;
    put_u32(p, b->ssrc);
    put_u32(p + 4, ((uint32_t)b->fraction_lost << 24) | lost);
    put_u32(p + 8, b->ext_high_seq);
    put_u32(p + 12, b->jitter);
    put_u32(p + 16, b->lsr);
    put_u32(p + 20, b->dlsr);
}

size_t rtcp_write_sr(uint8_t *out, uint32_t ssrc, uint64_t ntp, uint32_t rtp_ts, uint32_t packets,
                     uint32_t octets, const rtcp_block_t *blocks, int nblocks) {
    size_t len = 28 + 24 * (size_t)nblocks;
    int i;
    out[0] = (uint8_t)(0x80 | (nblocks & 0x1F));
    out[1] = RTCP_SR;
    put_u16(out + 2, (uint16_t)(len / 4 - 1));          /* length in 32-bit words minus one */
    put_u32(out + 4, ssrc);
    put_u32(out + 8, (uint32_t)(ntp >> 32));
    put_u32(out + 12, (uint32_t)ntp);
    put_u32(out + 16, rtp_ts);
    put_u32(out + 20, packets);
    put_u32(out + 24, octets);
    for (i = 0; i < nblocks; i++) write_block(out + 28 + 24 * i, &blocks[i]);
    return len;
}

size_t rtcp_write_nack(uint8_t *out, uint32_t ssrc, uint32_t media_ssrc, const uint16_t *seqs, int n) {
    size_t off = 12;
    int i = 0, nfci = 0;
    out[0] = 0x80 | 1;                                  /* FMT 1 = Generic NACK */
    out[1] = RTCP_RTPFB;
    put_u32(out + 4, ssrc);
    put_u32(out + 8, media_ssrc);
    while (i < n && nfci < RTCP_MAX_NACK) {
        uint16_t pid = seqs[i], blp = 0;
        int j = i + 1;
        /* Fold following losses within pid+1..pid+16 into the bitmask. */
        while (j < n) {
            uint16_t d = (uint16_t)(seqs[j] - pid);
            if (d < 1 || d > 16) break;
            blp |= (uint16_t)(1u << (d - 1));
            j++;
        }
        put_u16(out + off, pid);
        put_u16(out + off + 2, blp);
        off += 4;
        nfci++;
        i = j;
    }
    put_u16(out + 2, (uint16_t)(off / 4 - 1));
    return off;
}

size_t rtcp_write_pli(uint8_t *out, uint32_t ssrc, uint32_t media_ssrc) {
    out[0] = 0x80 | 1;                                  /* FMT 1 = PLI */
    out[1] = RTCP_PSFB;
    put_u16(out + 2, 2);
    put_u32(out + 4, ssrc);
    put_u32(out + 8, media_ssrc);
    return 12;
}

static void read_blocks(const uint8_t *p, int count, size_t avail, rtcp_info_t *info) {
    int i;
    for (i = 0; i < count && (size_t)(i + 1) * 24 <= avail && info->nblocks < RTCP_MAX_BLOCKS; i++) {
        const uint8_t *q = p + 24 * i;
        rtcp_block_t *b = &info->blocks[info->nblocks++];
        uint32_t w = get_u32(q + 4);
        b->ssrc = get_u32(q);
        b->fraction_lost = (uint8_t)(w >> 24);
        b->cum_lost = (int32_t)(w << 8) >> 8;           /* sign-extend 24 bits */
        b->ext_high_seq = get_u32(q + 8);
        b->jitter = get_u32(q + 12);
        b->lsr = get_u32(q + 16);
        b->dlsr = get_u32(q + 20);
    }
}

int rtcp_parse(const uint8_t *buf, size_t len, rtcp_info_t *info) {
    size_t off = 0;
    memset(info, 0, sizeof *info);
    while (off + 4 <= len) {
        const uint8_t *p = buf + off;
        int count = p[0] & 0x1F;
        size_t plen = ((size_t)get_u16(p + 2) + 1) * 4;
        if ((p[0] >> 6) != 2 || off + plen > len) return -1;
        switch (p[1]) {
        case RTCP_SR:
            if (plen < 28) return -1;
            info->has_sr = 1;
            info->sender_ssrc = get_u32(p + 4);
            info->sr_ntp = ((uint64_t)get_u32(p + 8) << 32) | get_u32(p + 12);
            info->sr_rtp_ts = get_u32(p + 16);
            info->sr_packets = get_u32(p + 20);
            info->sr_octets = get_u32(p + 24);
            read_blocks(p + 28, count, plen - 28, info);
            break;
        case RTCP_RR:
            if (plen < 8) return -1;
            info->sender_ssrc = get_u32(p + 4);
            read_blocks(p + 8, count, plen - 8, info);
            break;
        case RTCP_RTPFB:
            if (count == 1) {
                size_t f;
                for (f = 12; f + 4 <= plen; f += 4) {
                    uint16_t pid = get_u16(p + f), blp = get_u16(p + f + 2);
                    int b;
                    if (info->nnack < (int)(sizeof info->nack / sizeof info->nack[0])) info->nack[info->nnack++] = pid;
                    for (b = 0; b < 16; b++)
                        if ((blp >> b) & 1 && info->nnack < (int)(sizeof info->nack / sizeof info->nack[0]))
                            info->nack[info->nnack++] = (uint16_t)(pid + b + 1);
                }
            }
            break;
        case RTCP_PSFB:
            if (count == 1) info->pli = 1;
            break;
        default:
            break;
        }
        off += plen;
    }
    return 0;
}

void rx_stats_init(rx_stats_t *s, uint32_t ssrc, uint32_t clock_rate, uint16_t first_seq) {
    memset(s, 0, sizeof *s);
    s->init = 1;
    s->ssrc = ssrc;
    s->clock_rate = clock_rate;
    s->base_seq = first_seq;
    s->max_seq = (uint16_t)(first_seq - 1);
}

void rx_stats_update(rx_stats_t *s, uint16_t seq, uint32_t rtp_ts, uint64_t arrival_us) {
    uint16_t delta = (uint16_t)(seq - s->max_seq);
    int64_t arrival, transit, d;

    /* Sequence tracking: a forward jump extends max_seq (counting wraps). */
    if (delta > 0 && delta < 3000) {
        if (seq < s->max_seq) s->cycles += 65536;
        s->max_seq = seq;
    } else if (delta > 65536 - 100 || delta == 0) {
        /* duplicate or reordered packet: counted as received below */
    } else {
        /* huge jump: the sender restarted; resync */
        s->base_seq = seq;
        s->max_seq = seq;
        s->cycles = 0;
        s->received = 0;
        s->expected_prior = s->received_prior = 0;
    }
    s->received++;

    /* Interarrival jitter, RFC 3550 6.4.1: J += (|D| - J) / 16, in RTP units. */
    arrival = (int64_t)(arrival_us * s->clock_rate / 1000000ull);
    transit = arrival - (int64_t)rtp_ts;
    if (s->have_transit) {
        d = transit - s->last_transit;
        if (d < 0) d = -d;
        s->jitter += ((double)d - s->jitter) / 16.0;
    }
    s->last_transit = transit;
    s->have_transit = 1;
}

void rx_stats_block(rx_stats_t *s, uint64_t now_us, rtcp_block_t *b) {
    uint32_t ext_max = s->cycles + s->max_seq;
    uint32_t expected = ext_max - s->base_seq + 1;
    uint32_t exp_int = expected - s->expected_prior;
    uint32_t rec_int = s->received - s->received_prior;
    int32_t lost_int = (int32_t)(exp_int - rec_int);
    int64_t cum = (int64_t)expected - (int64_t)s->received;

    s->expected_prior = expected;
    s->received_prior = s->received;

    memset(b, 0, sizeof *b);
    b->ssrc = s->ssrc;
    b->fraction_lost = (exp_int == 0 || lost_int <= 0) ? 0 : (uint8_t)(((uint32_t)lost_int << 8) / exp_int);
    if (cum > 0x7FFFFF) cum = 0x7FFFFF;
    if (cum < -0x800000) cum = -0x800000;
    b->cum_lost = (int32_t)cum;
    b->ext_high_seq = ext_max;
    b->jitter = (uint32_t)s->jitter;
    if (s->last_sr_us) {
        b->lsr = s->last_sr;
        b->dlsr = (uint32_t)(((now_us - s->last_sr_us) << 16) / 1000000ull);
    }
}
