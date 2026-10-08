/* Receive side: jitter buffers, loss repair (NACK, PLI, Opus FEC/PLC),
 * decoding, the SDL window and audio playback. */
#include "client.h"
#include "../common/util.h"

#include <string.h>
#include <stdlib.h>
#include <vpx/vp8dx.h>

#define VIDEO_WAIT_MIN_US  120000   /* how long to wait for a missing packet before skipping */
#define AUDIO_TARGET_FRAMES 3       /* 60 ms playout delay */

/* ================= video ================= */

static void send_nack_pli(peer_t *p, const uint16_t *seqs, int n, int pli) {
    uint8_t buf[256];
    size_t len = 0;
    if (n > 0) {
        len = rtcp_write_nack(buf, g.vssrc, p->remote_vssrc, seqs, n);
        p->nack_sent += n;
    }
    if (pli) {
        len += rtcp_write_pli(buf + len, g.vssrc, p->remote_vssrc);
        p->plis_sent++;
    }
    if (len) udp_send(g.udp, &p->sel, buf, len);
}

static void request_keyframe(peer_t *p, uint64_t now_us) {
    /* Rate-limit PLIs: a keyframe takes about one RTT to arrive, and every
     * extra keyframe is a burst that can make congestion worse (PLI storm). */
    uint64_t gap = p->rtt_ms > 0 ? (uint64_t)(p->rtt_ms * 1500) : 300000;
    if (gap < 300000) gap = 300000;
    if (now_us - p->last_pli_us < gap) return;
    p->last_pli_us = now_us;
    send_nack_pli(p, NULL, 0, 1);
}

static void nack_add(peer_t *p, uint16_t seq, uint64_t now_us) {
    int i;
    for (i = 0; i < p->nnacks; i++)
        if (p->nacks[i].seq == seq) return;
    if (p->nnacks >= NACK_MAX) return;
    p->nacks[p->nnacks].seq = seq;
    p->nacks[p->nnacks].first_us = now_us;
    p->nacks[p->nnacks].last_sent_us = 0;
    p->nacks[p->nnacks].tries = 0;
    p->nnacks++;
}

static void nack_remove(peer_t *p, uint16_t seq) {
    int i;
    for (i = 0; i < p->nnacks; i++)
        if (p->nacks[i].seq == seq) {
            p->nacks[i] = p->nacks[--p->nnacks];
            return;
        }
}

void rx_video(peer_t *p, const rtp_hdr_t *h, const uint8_t *pl, size_t n, int rtx, uint64_t now_us) {
    vslot_t *s;
    if (n < 1) return;
    if (!p->rxv.init || p->rxv.ssrc != h->ssrc) {
        rx_stats_init(&p->rxv, h->ssrc, RTP_CLOCK_VIDEO, h->seq);
        p->remote_vssrc = h->ssrc;
        p->vjb_started = 0;
    }
    if (rtx) {
        p->rtx_recv++;
    } else {
        /* Gap in sequence numbers: ask for the missing packets right away. */
        if (p->vjb_started && seq_newer(h->seq, (uint16_t)(p->high_seq + 1))) {
            uint16_t m;
            int count = 0;
            for (m = (uint16_t)(p->high_seq + 1); m != h->seq && count < 200; m++, count++) nack_add(p, m, now_us);
        }
        rx_stats_update(&p->rxv, h->seq, h->ts, now_us);
    }
    nack_remove(p, h->seq);

    s = &p->vjb[h->seq % VJB_SLOTS];
    s->used = 1;
    s->seq = h->seq;
    s->ts = h->ts;
    s->marker = h->marker;
    s->start = (pl[0] & VP_START) != 0;
    s->key = (pl[0] & VP_KEY) != 0;
    s->len = (uint16_t)(n - 1);
    memcpy(s->data, pl + 1, n - 1);

    if (!p->vjb_started) {
        /* Wait for the first packet of a keyframe before starting. */
        if (s->start && s->key) {
            p->vjb_started = 1;
            p->play_seq = h->seq;
            p->high_seq = h->seq;
            p->need_key = 0;
        } else {
            request_keyframe(p, now_us);
        }
        return;
    }
    if (seq_newer(h->seq, p->high_seq)) p->high_seq = h->seq;
}

static void show_frame(peer_t *p, const vpx_image_t *img, uint64_t now_us) {
    int w = (int)img->d_w, h = (int)img->d_h, y;
    double gap_ms;
    SDL_LockMutex(g.lock);
    if (!p->disp || p->disp_w != w || p->disp_h != h) {
        free(p->disp);
        p->disp = malloc((size_t)w * h * 3 / 2);
        p->disp_w = w;
        p->disp_h = h;
    }
    for (y = 0; y < h; y++) memcpy(p->disp + (size_t)y * w, img->planes[0] + (size_t)y * img->stride[0], (size_t)w);
    for (y = 0; y < h / 2; y++) {
        memcpy(p->disp + (size_t)w * h + (size_t)y * (w / 2), img->planes[1] + (size_t)y * img->stride[1], (size_t)w / 2);
        memcpy(p->disp + (size_t)w * h * 5 / 4 + (size_t)y * (w / 2), img->planes[2] + (size_t)y * img->stride[2], (size_t)w / 2);
    }
    p->disp_new = 1;
    SDL_UnlockMutex(g.lock);

    /* Freeze detection, same rule as WebRTC stats: a frame gap longer than
     * max(3 x average interval, average + 150 ms) counts as one freeze. */
    if (p->last_frame_us) {
        gap_ms = (now_us - p->last_frame_us) / 1000.0;
        if (p->avg_frame_ms > 0 && gap_ms > 3 * p->avg_frame_ms && gap_ms > p->avg_frame_ms + 150) p->freezes++;
        p->avg_frame_ms = p->avg_frame_ms > 0 ? 0.9 * p->avg_frame_ms + 0.1 * (gap_ms < 1000 ? gap_ms : 1000) : gap_ms;
    }
    p->last_frame_us = now_us;
    p->frames_decoded++;
}

static void decode_frame(peer_t *p, const uint8_t *data, size_t len, int key, uint64_t now_us) {
    vpx_codec_iter_t it = NULL;
    vpx_image_t *img;
    if (!p->vdec_ok) {
        if (vpx_codec_dec_init(&p->vdec, vpx_codec_vp8_dx(), NULL, 0) != VPX_CODEC_OK) return;
        p->vdec_ok = 1;
    }
    if (p->need_key && !key) return;            /* references are broken: wait for a keyframe */
    p->need_key = 0;
    if (vpx_codec_decode(&p->vdec, data, (unsigned)len, NULL, 0) != VPX_CODEC_OK) {
        p->need_key = 1;
        request_keyframe(p, now_us);
        return;
    }
    while ((img = vpx_codec_get_frame(&p->vdec, &it)) != NULL) show_frame(p, img, now_us);
}

/* Assemble complete frames from the jitter buffer in sequence order. */
static void video_tick(peer_t *p, uint64_t now_us) {
    static uint8_t frame[512 * 1024];
    int guard = 0;
    uint64_t wait_us = VIDEO_WAIT_MIN_US;
    if (p->rtt_ms > 0) {
        uint64_t w = (uint64_t)(p->rtt_ms * 1500) + 40000;   /* room for one NACK round trip */
        if (w > wait_us) wait_us = w;
        if (wait_us > 600000) wait_us = 600000;
    }

    while (p->vjb_started && guard++ < 64) {
        uint16_t s = p->play_seq;
        size_t flen = 0;
        int complete = 0, key = 0, count = 0;
        vslot_t *first = &p->vjb[s % VJB_SLOTS];

        if (first->used && first->seq == s && first->start) {
            key = first->key;
            for (;; s++) {
                vslot_t *x = &p->vjb[s % VJB_SLOTS];
                if (!x->used || x->seq != s) break;
                if (flen + x->len <= sizeof frame) {
                    memcpy(frame + flen, x->data, x->len);
                    flen += x->len;
                }
                if (++count > VJB_SLOTS / 2) break;
                if (x->marker) {
                    complete = 1;
                    break;
                }
            }
        }

        if (complete) {
            uint16_t q;
            for (q = p->play_seq; q != (uint16_t)(s + 1); q++) p->vjb[q % VJB_SLOTS].used = 0;
            p->play_seq = (uint16_t)(s + 1);
            p->stall_us = 0;
            decode_frame(p, frame, flen, key, now_us);
            continue;
        }

        /* Incomplete: nothing newer has arrived yet, so just wait. */
        if (!seq_newer(p->high_seq, p->play_seq) && !(first->used && first->seq == p->play_seq)) break;
        if (!p->stall_us) p->stall_us = now_us;
        if (now_us - p->stall_us < wait_us) break;

        /* Waited too long for a retransmission: skip to the next frame start.
         * Later frames may reference the lost one, so ask for a keyframe. */
        {
            uint16_t q = (uint16_t)(p->play_seq + 1);
            int found = 0;
            while (!seq_newer(q, p->high_seq)) {
                vslot_t *x = &p->vjb[q % VJB_SLOTS];
                if (x->used && x->seq == q && x->start) {
                    found = 1;
                    break;
                }
                q++;
            }
            {
                uint16_t z;
                for (z = p->play_seq; z != q; z++) p->vjb[z % VJB_SLOTS].used = 0;
            }
            if (!found) {
                p->play_seq = q;   /* q == high_seq + 1 */
                p->stall_us = 0;
                p->need_key = 1;
                request_keyframe(p, now_us);
                break;
            }
            p->play_seq = q;
            p->stall_us = 0;
            if (!p->vjb[q % VJB_SLOTS].key) {
                p->need_key = 1;
                request_keyframe(p, now_us);
            }
        }
    }
    if (p->need_key) request_keyframe(p, now_us);
}

static void nack_tick(peer_t *p, uint64_t now_us) {
    uint16_t list[64];
    int n = 0, i;
    uint64_t resend_us = p->rtt_ms > 0 ? (uint64_t)(p->rtt_ms * 1500) : 50000;
    if (resend_us < 20000) resend_us = 20000;
    for (i = 0; i < p->nnacks; i++) {
        nack_t *k = &p->nacks[i];
        /* Give up on packets we have already skipped past, or after 4 tries. */
        if (seq_newer(p->play_seq, k->seq) || k->tries >= 4 || now_us - k->first_us > 1000000) {
            p->nacks[i--] = p->nacks[--p->nnacks];
            continue;
        }
        if (k->last_sent_us && now_us - k->last_sent_us < resend_us) continue;
        /* Reordering tolerance: a "gap" may just be packets overtaking each
         * other (jitter). Wait about two jitter periods before the first NACK,
         * otherwise reordering triggers needless retransmissions. */
        if (!k->last_sent_us) {
            uint64_t reorder_us = (uint64_t)(p->rxv.jitter / 90.0 * 2000);
            if (reorder_us < 5000) reorder_us = 5000;
            if (reorder_us > 80000) reorder_us = 80000;
            if (now_us - k->first_us < reorder_us) continue;
        }
        if (n < 64) {
            list[n++] = k->seq;
            k->last_sent_us = now_us;
            k->tries++;
        }
    }
    if (n) {
        /* NACK FCIs need ascending order to fold neighbours into the bitmask. */
        int a, b;
        for (a = 1; a < n; a++)
            for (b = a; b > 0 && seq_newer(list[b - 1], list[b]); b--) {
                uint16_t t = list[b];
                list[b] = list[b - 1];
                list[b - 1] = t;
            }
        send_nack_pli(p, list, n, 0);
    }
}

/* ================= audio ================= */

void rx_audio(peer_t *p, const rtp_hdr_t *h, const uint8_t *pl, size_t n, uint64_t now_us) {
    aslot_t *s;
    if (n > sizeof p->ajb[0].data) return;
    if (!p->rxa.init || p->rxa.ssrc != h->ssrc) {
        rx_stats_init(&p->rxa, h->ssrc, RTP_CLOCK_AUDIO, h->seq);
        p->remote_assrc = h->ssrc;
        p->ajb_started = 0;
    }
    rx_stats_update(&p->rxa, h->seq, h->ts, now_us);
    p->alast_rx_us = now_us;
    if (p->ajb_started && !seq_newer(h->seq, (uint16_t)(p->aplay_seq - 1))) {
        /* Arrived after its playout time. If this keeps happening the network
         * delay has grown: restart playout with the delay rebuilt (adaptive playout). */
        if (++p->alate > 5) {
            p->ajb_started = 0;
            p->alate = 0;
        } else {
            return;
        }
    } else {
        p->alate = 0;
    }
    s = &p->ajb[h->seq % AJB_SLOTS];
    s->used = 1;
    s->seq = h->seq;
    s->len = (uint16_t)n;
    memcpy(s->data, pl, n);
    if (!p->ajb_started) {
        p->ajb_started = 1;
        p->aplay_seq = h->seq;
        p->ahigh_seq = h->seq;
        p->anext_us = now_us + AUDIO_TARGET_FRAMES * 20000;   /* build up the playout delay */
    } else if (seq_newer(h->seq, p->ahigh_seq)) {
        p->ahigh_seq = h->seq;
    }
}

static void ring_write(peer_t *p, const int16_t *pcm, int n) {
    int i;
    SDL_LockMutex(g.lock);
    for (i = 0; i < n; i++) {
        if (p->aring_n == AUDIO_RING) {                 /* full: drop oldest (clock drift) */
            p->aring_r = (p->aring_r + 1) % AUDIO_RING;
            p->aring_n--;
        }
        p->aring[p->aring_w] = pcm[i];
        p->aring_w = (p->aring_w + 1) % AUDIO_RING;
        p->aring_n++;
    }
    SDL_UnlockMutex(g.lock);
}

static void audio_tick(peer_t *p, uint64_t now_us) {
    int16_t pcm[AUDIO_FRAME];
    int guard = 0;
    if (!p->ajb_started) return;
    if (!p->adec) {
        int err;
        p->adec = opus_decoder_create(AUDIO_RATE, 1, &err);
        if (!p->adec) return;
    }
    if (now_us - p->alast_rx_us > 500000) {             /* sender went quiet: restart later */
        p->ajb_started = 0;
        return;
    }
    while (now_us >= p->anext_us && guard++ < 10) {
        aslot_t *cur = &p->ajb[p->aplay_seq % AJB_SLOTS];
        aslot_t *nxt = &p->ajb[(uint16_t)(p->aplay_seq + 1) % AJB_SLOTS];
        int n;
        /* Too far behind the newest packet: jump forward to keep latency bounded. */
        if ((uint16_t)(p->ahigh_seq - p->aplay_seq) > AUDIO_TARGET_FRAMES + 12 &&
            (uint16_t)(p->ahigh_seq - p->aplay_seq) < 1000) {
            uint16_t z;
            for (z = p->aplay_seq; z != (uint16_t)(p->ahigh_seq - AUDIO_TARGET_FRAMES); z++) p->ajb[z % AJB_SLOTS].used = 0;
            p->aplay_seq = (uint16_t)(p->ahigh_seq - AUDIO_TARGET_FRAMES);
            continue;
        }
        if (cur->used && cur->seq == p->aplay_seq) {
            n = opus_decode(p->adec, cur->data, cur->len, pcm, AUDIO_FRAME, 0);
            cur->used = 0;
        } else if (nxt->used && nxt->seq == (uint16_t)(p->aplay_seq + 1)) {
            /* Lost, but the next packet carries in-band FEC for this one. */
            n = opus_decode(p->adec, nxt->data, nxt->len, pcm, AUDIO_FRAME, 1);
            p->afec++;
        } else {
            /* No data at all: packet loss concealment extrapolates the signal. */
            n = opus_decode(p->adec, NULL, 0, pcm, AUDIO_FRAME, 0);
            p->aconcealed++;
        }
        p->aframes++;
        if (n > 0) ring_write(p, pcm, n);
        p->aplay_seq++;
        p->anext_us += 20000;
    }
}

void rx_tick(peer_t *p, uint64_t now_us) {
    if (p->rxv.init) {
        nack_tick(p, now_us);
        video_tick(p, now_us);
    }
    audio_tick(p, now_us);
}

void peer_media_free(peer_t *p) {
    if (p->vdec_ok) vpx_codec_destroy(&p->vdec);
    p->vdec_ok = 0;
    if (p->adec) opus_decoder_destroy(p->adec);
    p->adec = NULL;
}

/* ================= playback ================= */

static SDL_AudioDeviceID audio_dev;

static void audio_callback(void *ud, Uint8 *stream, int len) {
    int16_t *out = (int16_t *)stream;
    int n = len / 2, i, j;
    (void)ud;
    memset(stream, 0, (size_t)len);
    SDL_LockMutex(g.lock);
    for (j = 0; j < MAX_PEERS; j++) {
        peer_t *p = &g.peers[j];
        if (!p->used) continue;
        for (i = 0; i < n && p->aring_n > 0; i++) {
            int v = out[i] + p->aring[p->aring_r];       /* mix by summing, then clip */
            out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            p->aring_r = (p->aring_r + 1) % AUDIO_RING;
            p->aring_n--;
        }
    }
    SDL_UnlockMutex(g.lock);
}

void audio_out_start(void) {
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = audio_callback;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!audio_dev) {
        log_msg("audio", "no playback device (%s); continuing without sound", SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(audio_dev, 0);
}

/* ================= window ================= */

typedef struct {
    SDL_Texture *tex;
    int w, h;
} tile_tex_t;

static void upload(SDL_Renderer *r, tile_tex_t *t, const uint8_t *i420, int w, int h) {
    if (!t->tex || t->w != w || t->h != h) {
        if (t->tex) SDL_DestroyTexture(t->tex);
        t->tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, w, h);
        t->w = w;
        t->h = h;
    }
    SDL_UpdateYUVTexture(t->tex, NULL, i420, w, i420 + w * h, w / 2, i420 + w * h * 5 / 4, w / 2);
}

static void draw_tile(SDL_Renderer *r, tile_tex_t *t, SDL_Rect cell, const char *label, char lines[][96], int nlines) {
    SDL_Rect dst = cell, box;
    int i;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderFillRect(r, &cell);
    if (t->tex) {
        /* letterbox: keep the aspect ratio */
        double sa = (double)t->w / t->h, ca = (double)cell.w / cell.h;
        if (sa > ca) {
            dst.h = (int)(cell.w / sa);
            dst.y = cell.y + (cell.h - dst.h) / 2;
        } else {
            dst.w = (int)(cell.h * sa);
            dst.x = cell.x + (cell.w - dst.w) / 2;
        }
        SDL_RenderCopy(r, t->tex, NULL, &dst);
    }
    box.x = cell.x + 8;
    box.y = cell.y + cell.h - 26;
    box.w = font_width(label, 3) + 12;
    box.h = 20;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 160);
    SDL_RenderFillRect(r, &box);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    font_draw(r, box.x + 6, box.y + 3, 3, label);
    if (g.show_stats && nlines) {
        int wmax = 0;
        for (i = 0; i < nlines; i++)
            if (font_width(lines[i], 2) > wmax) wmax = font_width(lines[i], 2);
        box.w = wmax + 12;
        box.h = nlines * 14 + 8;
        box.x = cell.x + cell.w - box.w - 8;
        box.y = cell.y + 8;
        SDL_SetRenderDrawColor(r, 0, 0, 0, 170);
        SDL_RenderFillRect(r, &box);
        SDL_SetRenderDrawColor(r, 140, 255, 140, 255);
        for (i = 0; i < nlines; i++) font_draw(r, box.x + 6, box.y + 5 + i * 14, 2, lines[i]);
    }
}

void window_run(void) {
    SDL_Window *win;
    SDL_Renderer *r;
    tile_tex_t self_t = {0}, peer_t_[MAX_PEERS] = {{0}};
    char title[128];

    snprintf(title, sizeof title, "Mini-Meet - room %s - %s", g.room, g.name);
    win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                           SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) {
        log_msg("ui", "cannot open window (%s); running headless", SDL_GetError());
        while (g.running) sleep_ms(50);
        return;
    }
    r = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!r) r = SDL_CreateRenderer(win, -1, 0);

    while (g.running) {
        SDL_Event ev;
        int ww, wh, ntiles, cols, rows, i, k;
        SDL_Rect cells[MM_MAX_ROOM];
        int idx[MAX_PEERS], npeers = 0;

        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) g.running = 0;
            if (ev.type == SDL_KEYDOWN) {
                switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE: case SDLK_q: g.running = 0; break;
                case SDLK_m: g.mic_on = !g.mic_on; log_msg("ui", "mic %s", g.mic_on ? "on" : "muted"); break;
                case SDLK_v: g.cam_on = !g.cam_on; log_msg("ui", "camera %s", g.cam_on ? "on" : "off"); break;
                case SDLK_s: g.show_stats = !g.show_stats; break;
                default: break;
                }
            }
        }

        SDL_LockMutex(g.lock);
        if (g.self_new && g.self_disp) {
            upload(r, &self_t, g.self_disp, g.self_w, g.self_h);
            g.self_new = 0;
        }
        for (i = 0; i < MAX_PEERS; i++) {
            peer_t *p = &g.peers[i];
            if (!p->used) {
                if (peer_t_[i].tex) {
                    SDL_DestroyTexture(peer_t_[i].tex);
                    peer_t_[i].tex = NULL;
                }
                continue;
            }
            idx[npeers++] = i;
            if (p->disp_new && p->disp) {
                upload(r, &peer_t_[i], p->disp, p->disp_w, p->disp_h);
                p->disp_new = 0;
            }
        }
        SDL_UnlockMutex(g.lock);

        /* grid: 1 tile full, 2 side by side, 3-4 in a 2x2 */
        SDL_GetRendererOutputSize(r, &ww, &wh);
        ntiles = 1 + npeers;
        cols = ntiles == 1 ? 1 : 2;
        rows = ntiles <= 2 ? 1 : 2;
        for (k = 0; k < ntiles; k++) {
            cells[k].w = (ww - 8 * (cols + 1)) / cols;
            cells[k].h = (wh - 8 * (rows + 1) - 30) / rows;
            cells[k].x = 8 + (k % cols) * (cells[k].w + 8);
            cells[k].y = 8 + (k / cols) * (cells[k].h + 8);
        }

        SDL_SetRenderDrawColor(r, 17, 20, 24, 255);
        SDL_RenderClear(r);
        {
            char lines[3][96], label[64];
            snprintf(label, sizeof label, "%s (you)%s%s", g.name, g.mic_on ? "" : " MUTED", g.cam_on ? "" : " CAM OFF");
            snprintf(lines[0], 96, "SEND %d KBPS %dX%d LVL %d", CC_LADDER[g.enc_level].kbps,
                     CC_LADDER[g.enc_level].width, CC_LADDER[g.enc_level].height, g.enc_level);
            snprintf(lines[1], 96, "ADAPT %s", g.adapt ? "ON" : "OFF");
            draw_tile(r, &self_t, cells[0], label, lines, 2);
        }
        for (k = 0; k < npeers; k++) {
            peer_t *p = &g.peers[idx[k]];
            char lines[6][96];
            int nl = 0;
            if (p->ice_state != ICE_CONNECTED) {
                snprintf(lines[nl++], 96, p->ice_state == ICE_FAILED ? "ICE FAILED (NEEDS TURN)" : "CONNECTING...");
            } else {
                snprintf(lines[nl++], 96, "PATH %s->%s UDP", p->ltype, p->rtype);
                snprintf(lines[nl++], 96, "RTT %.0f MS", p->rtt_ms < 0 ? 0 : p->rtt_ms);
                snprintf(lines[nl++], 96, "V-IN %dX%d LOSS %.1f%%", p->disp_w, p->disp_h, p->lost_pct_v);
                snprintf(lines[nl++], 96, "JITTER %.0f MS FREEZES %d", p->rxv.jitter / 90.0, p->freezes);
                snprintf(lines[nl++], 96, "A-IN LOSS %.1f%%", p->lost_pct_a);
                snprintf(lines[nl++], 96, "THEY SEE LOSS %.1f%%", p->rloss_v * 100);
            }
            draw_tile(r, &peer_t_[idx[k]], cells[k + 1], p->name, lines, nl);
        }
        SDL_SetRenderDrawColor(r, 160, 170, 180, 255);
        font_draw(r, 10, wh - 22, 2, "M MUTE   V CAMERA   S STATS   Q QUIT");
        SDL_RenderPresent(r);
        SDL_Delay(15);
    }
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
}
