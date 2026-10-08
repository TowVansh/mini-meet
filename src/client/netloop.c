/* Network thread: one select() loop over the TCP signalling socket and the
 * UDP media socket, plus timers for ICE checks, jitter-buffer playout,
 * RTCP reports, NACKs and the per-second statistics. */
#include "client.h"
#include "../common/util.h"
#include "../stun/stun.h"

#include <string.h>
#include <stdlib.h>

peer_t *peer_find(const char *id) {
    int i;
    for (i = 0; i < MAX_PEERS; i++)
        if (g.peers[i].used && strcmp(g.peers[i].id, id) == 0) return &g.peers[i];
    return NULL;
}

peer_t *peer_add(const char *id, const char *name) {
    peer_t *p = peer_find(id);
    int i;
    if (p) {
        snprintf(p->name, sizeof p->name, "%s", name);
        return p;
    }
    for (i = 0; i < MAX_PEERS; i++) {
        if (g.peers[i].used) continue;
        SDL_LockMutex(g.lock);
        p = &g.peers[i];
        memset(p, 0, sizeof *p);
        p->used = 1;
        snprintf(p->id, sizeof p->id, "%s", id);
        snprintf(p->name, sizeof p->name, "%s", name);
        p->vjb = calloc(VJB_SLOTS, sizeof(vslot_t));
        p->rtt_ms = -1;
        cc_init(&p->cc);
        SDL_UnlockMutex(g.lock);
        return p;
    }
    log_msg("peer", "peer table full, ignoring %s", name);
    return NULL;
}

void peer_remove(peer_t *p) {
    SDL_LockMutex(g.lock);
    peer_media_free(p);
    free(p->vjb);
    free(p->disp);
    memset(p, 0, sizeof *p);
    SDL_UnlockMutex(g.lock);
}

peer_t *peer_by_addr(const endpoint_t *ep) {
    int i;
    for (i = 0; i < MAX_PEERS; i++)
        if (g.peers[i].used && g.peers[i].ice_state == ICE_CONNECTED && ep_equal(&g.peers[i].sel, ep))
            return &g.peers[i];
    return NULL;
}

/* ---- RTCP ---- */

static void send_reports(peer_t *p, uint64_t now_us) {
    rtcp_block_t blocks[2];
    int nb = 0;
    uint8_t buf[256];
    size_t n;
    if (p->rxv.init) rx_stats_block(&p->rxv, now_us, &blocks[nb++]);
    if (p->rxa.init) rx_stats_block(&p->rxa, now_us, &blocks[nb++]);
    SDL_LockMutex(g.lock);
    n = rtcp_write_sr(buf, g.vssrc, ntp_from_us(now_us), g.last_vts, g.vpackets, g.voctets, blocks, nb);
    SDL_UnlockMutex(g.lock);
    udp_send(g.udp, &p->sel, buf, n);
}

static void on_rtcp(peer_t *p, const uint8_t *buf, size_t len, uint64_t now_us) {
    rtcp_info_t info;
    int i;
    if (rtcp_parse(buf, len, &info) != 0) return;
    if (info.has_sr) {
        /* Remember when this SR arrived so our next report can carry LSR/DLSR. */
        rx_stats_t *s = info.sender_ssrc == p->remote_vssrc ? &p->rxv : NULL;
        if (s && s->init) {
            s->last_sr = ntp_middle32(info.sr_ntp);
            s->last_sr_us = now_us;
        }
        if (p->rxa.init) {
            p->rxa.last_sr = ntp_middle32(info.sr_ntp);
            p->rxa.last_sr_us = now_us;
        }
    }
    for (i = 0; i < info.nblocks; i++) {
        rtcp_block_t *b = &info.blocks[i];
        if (b->ssrc == g.vssrc) {
            /* RTT = now - LSR - DLSR, all in 1/65536 s (RFC 3550 6.4.1). */
            if (b->lsr) {
                uint32_t now32 = ntp_middle32(ntp_from_us(now_us));
                uint32_t rtt16 = now32 - b->lsr - b->dlsr;
                if (rtt16 < 65536u * 30u) p->rtt_ms = rtt16 * 1000.0 / 65536.0;
            }
            p->rloss_v = b->fraction_lost / 256.0;
            p->rjitter_v_ms = b->jitter / 90.0;
            if (g.adapt && cc_on_report(&p->cc, p->rloss_v, p->rtt_ms)) {
                int j, worst = 0;
                for (j = 0; j < MAX_PEERS; j++)
                    if (g.peers[j].used && g.peers[j].ice_state == ICE_CONNECTED && g.peers[j].cc.level > worst)
                        worst = g.peers[j].cc.level;
                if (worst != g.enc_level) {
                    log_msg("cc", "%s reported loss %.1f%% rtt %.0f ms -> encoder level %d (%d kbps %dx%d)", p->name,
                            p->rloss_v * 100, p->rtt_ms, worst, CC_LADDER[worst].kbps, CC_LADDER[worst].width,
                            CC_LADDER[worst].height);
                    g.enc_level = worst;
                }
            }
        } else if (b->ssrc == g.assrc) {
            p->rloss_a = b->fraction_lost / 256.0;
            p->rjitter_a_ms = b->jitter / 48.0;
        }
    }
    for (i = 0; i < info.nnack; i++) tx_resend(p, info.nack[i]);
    if (info.pli) g.force_key = 1;
}

/* ---- statistics ---- */

static void csv_row(peer_t *p, const char *kind, const char *dir, double kbps, double loss, double jitter,
                    int fps, int w, int h, double conceal, int freezes) {
    char ct[24];
    if (!g.csv) return;
    snprintf(ct, sizeof ct, "%s->%s", p->ltype, p->rtype);
    fprintf(g.csv, "%llu,%s,%s,%s,%s,%s,%.1f,,%.2f,%.1f,%.1f,", (unsigned long long)wall_ms(), g.run, g.name,
            p->name, kind, dir, kbps, loss, jitter, p->rtt_ms < 0 ? 0 : p->rtt_ms);
    if (fps >= 0) fprintf(g.csv, "%d,%d,%d,", fps, w, h);
    else fputs(",,,", g.csv);
    if (strcmp(dir, "out") == 0) fprintf(g.csv, "%d,%s,", CC_LADDER[g.enc_level].kbps, g.enc_level ? "bandwidth" : "none");
    else fputs(",,", g.csv);
    if (conceal >= 0) fprintf(g.csv, "%.2f,", conceal);
    else fputc(',', g.csv);
    if (freezes >= 0) fprintf(g.csv, "%d,", freezes);
    else fputc(',', g.csv);
    fprintf(g.csv, "%d,%d,%s\n", g.adapt, g.enc_level, ct);
}

static double interval_loss(rx_stats_t *s, uint32_t *exp_prior, uint32_t *rec_prior) {
    uint32_t expected, e, r;
    double loss;
    if (!s->init) return 0;
    expected = s->cycles + s->max_seq - s->base_seq + 1;
    e = expected - *exp_prior;
    r = s->received - *rec_prior;
    *exp_prior = expected;
    *rec_prior = s->received;
    loss = e ? 100.0 * ((double)e - (double)r) / e : 0;
    return loss < 0 ? 0 : loss;
}

static void stats_tick(double secs) {
    int i;
    for (i = 0; i < MAX_PEERS; i++) {
        peer_t *p = &g.peers[i];
        double vin, ain, vout, aout, lv, la, conc;
        if (!p->used || p->ice_state != ICE_CONNECTED) continue;
        vin = p->rx_bytes_v * 8 / 1000.0 / secs;
        ain = p->rx_bytes_a * 8 / 1000.0 / secs;
        vout = p->tx_bytes_v * 8 / 1000.0 / secs;
        aout = p->tx_bytes_a * 8 / 1000.0 / secs;
        lv = interval_loss(&p->rxv, &p->exp_prior_v, &p->rec_prior_v);
        la = interval_loss(&p->rxa, &p->exp_prior_a, &p->rec_prior_a);
        conc = p->aframes ? 100.0 * p->aconcealed / p->aframes : 0;
        p->lost_pct_v = lv;
        p->lost_pct_a = la;

        csv_row(p, "video", "in", vin, lv, p->rxv.jitter / 90.0, (int)(p->frames_decoded / secs + 0.5), p->disp_w,
                p->disp_h, -1, p->freezes);
        csv_row(p, "audio", "in", ain, la, p->rxa.jitter / 48.0, -1, 0, 0, conc, -1);
        csv_row(p, "video", "out", vout, p->rloss_v * 100, p->rjitter_v_ms, -1, 0, 0, -1, -1);
        csv_row(p, "audio", "out", aout, p->rloss_a * 100, p->rjitter_a_ms, -1, 0, 0, -1, -1);

        if (g.headless || (now_ms() / 1000) % 5 == 0)
            log_msg("stats", "%-8s %s->%s rtt %4.0fms | in v %4.0fkbps %2.0ffps %dx%d loss %4.1f%% jit %3.0fms frz %d nack %d rtx %d | a conceal %4.1f%% fec %d | out %4.0fkbps lvl %d",
                    p->name, p->ltype, p->rtype, p->rtt_ms, vin, p->frames_decoded / secs, p->disp_w, p->disp_h, lv,
                    p->rxv.jitter / 90.0, p->freezes, p->nack_sent, p->rtx_recv, conc, p->afec, vout + aout, g.enc_level);

        p->rx_bytes_v = p->rx_bytes_a = p->tx_bytes_v = p->tx_bytes_a = 0;
        p->frames_decoded = p->nack_sent = p->rtx_recv = p->plis_sent = 0;
        p->aframes = p->aconcealed = p->afec = 0;
    }
    if (g.csv) fflush(g.csv);
}

/* ---- packet dispatch ---- */

static void on_udp(const uint8_t *buf, size_t len, const endpoint_t *from, uint64_t now_us) {
    peer_t *p;
    rtp_hdr_t h;
    int off;
    /* Demultiplex by first byte (RFC 7983): 0-3 STUN, 128-191 RTP/RTCP. */
    if (stun_is_stun(buf, len)) {
        ice_on_stun(buf, len, from);
        return;
    }
    p = peer_by_addr(from);
    if (!p) return;
    p->last_rx_ms = now_us / 1000;
    if (rtp_is_rtcp(buf, len)) {
        on_rtcp(p, buf, len, now_us);
        return;
    }
    off = rtp_parse_header(buf, len, &h);
    if (off < 0) return;
    if (h.pt == RTP_PT_VP8 || h.pt == RTP_PT_VP8_RTX) {
        p->rx_bytes_v += len;
        rx_video(p, &h, buf + off, len - (size_t)off, h.pt == RTP_PT_VP8_RTX, now_us);
    } else if (h.pt == RTP_PT_OPUS) {
        p->rx_bytes_a += len;
        rx_audio(p, &h, buf + off, len - (size_t)off, now_us);
    }
}

static int net_thread(void *arg) {
    uint64_t last_rtcp = now_ms(), last_stats = now_ms();
    (void)arg;
    while (g.running) {
        fd_set rd;
        struct timeval tv = {0, 2000};   /* 2 ms: drives the audio playout clock */
        sock_t maxfd = g.udp;
        uint64_t now;
        int i;

        FD_ZERO(&rd);
        FD_SET(g.udp, &rd);
        if (g.tcp != SOCK_INVALID) {
            FD_SET(g.tcp, &rd);
            if (g.tcp > maxfd) maxfd = g.tcp;
        }
        if (select((int)maxfd + 1, &rd, NULL, NULL, &tv) > 0) {
            if (g.tcp != SOCK_INVALID && FD_ISSET(g.tcp, &rd)) sig_on_readable();
            if (FD_ISSET(g.udp, &rd)) {
                uint8_t buf[PKT_MAX + 64];
                endpoint_t from;
                int n, budget = 256;
                while (budget-- > 0 && (n = udp_recv(g.udp, &from, buf, sizeof buf)) > 0)
                    on_udp(buf, (size_t)n, &from, now_us());
            }
        }

        now = now_ms();
        sig_tick(now);
        for (i = 0; i < MAX_PEERS; i++) {
            peer_t *p = &g.peers[i];
            if (!p->used) continue;
            ice_tick(p, now);
            if (p->ice_state == ICE_CONNECTED) rx_tick(p, now_us());
        }
        if (now - last_rtcp >= 1000) {
            last_rtcp = now;
            for (i = 0; i < MAX_PEERS; i++)
                if (g.peers[i].used && g.peers[i].ice_state == ICE_CONNECTED) send_reports(&g.peers[i], now_us());
        }
        if (now - last_stats >= 1000) {
            double secs = (now - last_stats) / 1000.0;
            last_stats = now;
            stats_tick(secs);
        }
        if (g.duration && now - g.start_ms > (uint64_t)g.duration * 1000) g.running = 0;
    }
    return 0;
}

SDL_Thread *net_thread_start(void) {
    return SDL_CreateThread(net_thread, "net", NULL);
}
