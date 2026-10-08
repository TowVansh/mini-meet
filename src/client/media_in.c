/* Send side: capture (camera/mic via FFmpeg, a file, or a built-in test
 * pattern and tone), encode (VP8 via libvpx, Opus via libopus),
 * packetize into RTP and send to every connected peer. */
#include "client.h"
#include "../common/util.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <vpx/vpx_encoder.h>
#include <vpx/vp8cx.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>

#define VIDEO_FPS 20
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static SDL_Thread *vthread, *athread;

/* ================= RTP send ================= */

static void fanout(const uint8_t *pkt, size_t len, int video) {
    int i;
    for (i = 0; i < MAX_PEERS; i++) {
        peer_t *p = &g.peers[i];
        if (!p->used || p->ice_state != ICE_CONNECTED) continue;
        udp_send(g.udp, &p->sel, pkt, len);
        if (video) p->tx_bytes_v += len;
        else p->tx_bytes_a += len;
    }
}

void send_video_frame(const uint8_t *data, size_t len, int key, uint32_t ts) {
    const size_t chunk = RTP_MAX_PAYLOAD - 1;
    size_t off = 0;
    SDL_LockMutex(g.lock);
    while (off < len) {
        uint8_t pkt[PKT_MAX];
        size_t n = len - off < chunk ? len - off : chunk, plen;
        rtp_hdr_t h;
        h.marker = off + n >= len;          /* marker = last packet of this frame */
        h.pt = RTP_PT_VP8;
        h.seq = g.vseq++;
        h.ts = ts;
        h.ssrc = g.vssrc;
        plen = rtp_write_header(pkt, &h);
        pkt[plen++] = (uint8_t)((off == 0 ? VP_START : 0) | (key ? VP_KEY : 0));
        memcpy(pkt + plen, data + off, n);
        plen += n;
        {   /* keep a copy for NACK retransmission */
            int slot = h.seq % TX_HISTORY;
            g.txhist[slot].used = 1;
            g.txhist[slot].seq = h.seq;
            g.txhist[slot].len = (uint16_t)plen;
            g.txhist[slot].sent_us = now_us();
            memcpy(g.txhist[slot].data, pkt, plen);
        }
        fanout(pkt, plen, 1);
        g.vpackets++;
        g.voctets += (uint32_t)n;
        off += n;
    }
    g.last_vts = ts;
    SDL_UnlockMutex(g.lock);
}

int tx_resend(peer_t *p, uint16_t seq) {
    int slot = seq % TX_HISTORY, ok = 0;
    SDL_LockMutex(g.lock);
    if (g.txhist[slot].used && g.txhist[slot].seq == seq && now_us() - g.txhist[slot].sent_us < 1000000) {
        uint8_t pkt[PKT_MAX];
        size_t len = g.txhist[slot].len;
        memcpy(pkt, g.txhist[slot].data, len);
        pkt[1] = (uint8_t)((pkt[1] & 0x80) | RTP_PT_VP8_RTX);  /* mark as retransmission */
        udp_send(g.udp, &p->sel, pkt, len);
        p->tx_bytes_v += len;
        ok = 1;
    }
    SDL_UnlockMutex(g.lock);
    return ok;
}

void send_audio_frame(const uint8_t *data, size_t len, uint32_t ts) {
    uint8_t pkt[PKT_MAX];
    rtp_hdr_t h;
    size_t plen;
    SDL_LockMutex(g.lock);
    h.marker = 0;
    h.pt = RTP_PT_OPUS;
    h.seq = g.aseq++;
    h.ts = ts;
    h.ssrc = g.assrc;
    plen = rtp_write_header(pkt, &h);
    memcpy(pkt + plen, data, len);
    fanout(pkt, plen + len, 0);
    SDL_UnlockMutex(g.lock);
}

/* ================= FFmpeg input helpers ================= */

typedef struct {
    AVFormatContext *fmt;
    AVCodecContext  *dec;
    int              stream;
    int              is_file;
} av_in_t;

static int av_open(av_in_t *in, const char *format, const char *url, enum AVMediaType type, AVDictionary *opts) {
    const AVInputFormat *ifmt = format ? av_find_input_format(format) : NULL;
    const AVCodec *codec = NULL;
    memset(in, 0, sizeof *in);
    if (format && !ifmt) return -1;
    if (avformat_open_input(&in->fmt, url, ifmt, &opts) < 0) {
        av_dict_free(&opts);
        return -1;
    }
    av_dict_free(&opts);
    if (avformat_find_stream_info(in->fmt, NULL) < 0) goto fail;
    in->stream = av_find_best_stream(in->fmt, type, -1, -1, &codec, 0);
    if (in->stream < 0 || !codec) goto fail;
    in->dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(in->dec, in->fmt->streams[in->stream]->codecpar);
    if (avcodec_open2(in->dec, codec, NULL) < 0) goto fail;
    in->is_file = format == NULL;
    return 0;
fail:
    avformat_close_input(&in->fmt);
    return -1;
}

static void av_close(av_in_t *in) {
    if (in->dec) avcodec_free_context(&in->dec);
    if (in->fmt) avformat_close_input(&in->fmt);
}

/* Read the next decoded frame; loops files forever. 0 on success. */
static int av_next(av_in_t *in, AVFrame *frame) {
    AVPacket *pkt = av_packet_alloc();
    int ret = -1;
    while (g.running) {
        if (avcodec_receive_frame(in->dec, frame) == 0) {
            ret = 0;
            break;
        }
        if (av_read_frame(in->fmt, pkt) < 0) {
            if (!in->is_file) break;
            av_seek_frame(in->fmt, in->stream, 0, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(in->dec);
            continue;
        }
        if (pkt->stream_index == in->stream) avcodec_send_packet(in->dec, pkt);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    return ret;
}

/* Pick the first DirectShow camera/microphone if the user named none. */
static int find_device(enum AVMediaType want, char *out, size_t cap) {
#ifdef _WIN32
    const AVInputFormat *f = av_find_input_format("dshow");
    AVDeviceInfoList *list = NULL;
    int i, j, found = 0;
    if (!f || avdevice_list_input_sources(f, NULL, NULL, &list) < 0 || !list) return -1;
    for (i = 0; i < list->nb_devices && !found; i++) {
        AVDeviceInfo *d = list->devices[i];
        for (j = 0; j < d->nb_media_types; j++)
            if (d->media_types[j] == want) {
                snprintf(out, cap, "%s", d->device_description);
                found = 1;
                break;
            }
    }
    avdevice_free_list_devices(&list);
    return found ? 0 : -1;
#else
    if (want == AVMEDIA_TYPE_VIDEO) snprintf(out, cap, "/dev/video0");
    else snprintf(out, cap, "default");
    return 0;
#endif
}

static int open_camera(av_in_t *in) {
    char dev[300], url[320];
    AVDictionary *o = NULL;
    if (g.file_src[0]) return av_open(in, NULL, g.file_src, AVMEDIA_TYPE_VIDEO, NULL);
    if (g.video_dev[0]) snprintf(dev, sizeof dev, "%s", g.video_dev);
    else if (find_device(AVMEDIA_TYPE_VIDEO, dev, sizeof dev) != 0) return -1;
    av_dict_set(&o, "video_size", "640x480", 0);
    av_dict_set(&o, "framerate", "30", 0);
#ifdef _WIN32
    snprintf(url, sizeof url, "video=%s", dev);
    if (av_open(in, "dshow", url, AVMEDIA_TYPE_VIDEO, o) == 0) goto ok;
    if (av_open(in, "dshow", url, AVMEDIA_TYPE_VIDEO, NULL) == 0) goto ok;   /* camera's own default mode */
#else
    snprintf(url, sizeof url, "%s", dev);
    if (av_open(in, "v4l2", url, AVMEDIA_TYPE_VIDEO, o) == 0) goto ok;
#endif
    return -1;
ok:
    log_msg("video", "camera: %s (%dx%d %s)", dev, in->dec->width, in->dec->height, avcodec_get_name(in->dec->codec_id));
    return 0;
}

static int open_mic(av_in_t *in) {
    char dev[300], url[320];
    if (g.file_src[0]) return av_open(in, NULL, g.file_src, AVMEDIA_TYPE_AUDIO, NULL);
    if (g.audio_dev[0]) snprintf(dev, sizeof dev, "%s", g.audio_dev);
    else if (find_device(AVMEDIA_TYPE_AUDIO, dev, sizeof dev) != 0) return -1;
#ifdef _WIN32
    snprintf(url, sizeof url, "audio=%s", dev);
    {
        AVDictionary *o = NULL;
        av_dict_set(&o, "audio_buffer_size", "20", 0);   /* ms: low capture latency */
        if (av_open(in, "dshow", url, AVMEDIA_TYPE_AUDIO, o) == 0) goto ok;
    }
#else
    snprintf(url, sizeof url, "%s", dev);
    if (av_open(in, "pulse", url, AVMEDIA_TYPE_AUDIO, NULL) == 0) goto ok;
    if (av_open(in, "alsa", url, AVMEDIA_TYPE_AUDIO, NULL) == 0) goto ok;
#endif
    return -1;
ok:
    log_msg("audio", "microphone: %s (%d Hz)", dev, in->dec->sample_rate);
    return 0;
}

/* ================= test sources ================= */

static void test_pattern(uint8_t *buf, int w, int h, int frame) {
    uint8_t *Y = buf, *U = buf + w * h, *V = U + w * h / 4;
    int x, y, bx, by, bs = h / 5;
    char text[64];
    /* moving diagonal gradient */
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) Y[y * w + x] = (uint8_t)(((x + y) * 255 / (w + h) + frame * 3) & 0xFF);
    for (y = 0; y < h / 2; y++)
        for (x = 0; x < w / 2; x++) {
            U[y * (w / 2) + x] = (uint8_t)(128 + 60 * sin((x + frame) * 0.05));
            V[y * (w / 2) + x] = (uint8_t)(128 + 60 * cos((y + frame) * 0.04));
        }
    /* bouncing box with noise inside: gives the encoder real detail to code */
    bx = (int)((w - bs) * (0.5 + 0.5 * sin(frame * 0.07)));
    by = (int)((h - bs) * (0.5 + 0.5 * cos(frame * 0.05)));
    for (y = by; y < by + bs; y++)
        for (x = bx; x < bx + bs; x++) Y[y * w + x] = (uint8_t)(rand_u32() & 0xFF);
    snprintf(text, sizeof text, "%s %05d", g.name, frame);
    font_draw_into_i420(Y, w, w, h, 10, 10, h >= 240 ? 4 : 2, text);
}

/* ================= video thread ================= */

typedef struct {
    vpx_codec_ctx_t ctx;
    vpx_codec_enc_cfg_t cfg;
    int ok, level;
} venc_t;

static int venc_open(venc_t *e, int level) {
    const cc_level_t *L = &CC_LADDER[level];
    if (e->ok) vpx_codec_destroy(&e->ctx);
    e->ok = 0;
    vpx_codec_enc_config_default(vpx_codec_vp8_cx(), &e->cfg, 0);
    e->cfg.g_w = (unsigned)L->width;
    e->cfg.g_h = (unsigned)L->height;
    e->cfg.g_timebase.num = 1;
    e->cfg.g_timebase.den = 1000;
    e->cfg.rc_target_bitrate = (unsigned)L->kbps;
    e->cfg.rc_end_usage = VPX_CBR;
    e->cfg.g_lag_in_frames = 0;             /* real time: no look-ahead */
    e->cfg.g_error_resilient = VPX_ERROR_RESILIENT_DEFAULT;
    e->cfg.g_threads = 2;
    e->cfg.rc_min_quantizer = 4;
    e->cfg.rc_max_quantizer = 56;
    e->cfg.rc_buf_sz = 1000;
    e->cfg.rc_buf_initial_sz = 500;
    e->cfg.rc_buf_optimal_sz = 600;
    e->cfg.rc_dropframe_thresh = 0;
    e->cfg.kf_mode = VPX_KF_AUTO;
    e->cfg.kf_max_dist = VIDEO_FPS * 10;    /* periodic keyframe every 10 s */
    if (vpx_codec_enc_init(&e->ctx, vpx_codec_vp8_cx(), &e->cfg, 0) != VPX_CODEC_OK) return -1;
    vpx_codec_control(&e->ctx, VP8E_SET_CPUUSED, 8);
    vpx_codec_control(&e->ctx, VP8E_SET_NOISE_SENSITIVITY, 0);
    vpx_codec_control(&e->ctx, VP8E_SET_STATIC_THRESHOLD, 1);
    e->ok = 1;
    e->level = level;
    return 0;
}

static void venc_frame(venc_t *e, uint8_t *i420, uint64_t pts_ms, uint32_t rtp_ts) {
    vpx_image_t img;
    vpx_codec_iter_t it = NULL;
    const vpx_codec_cx_pkt_t *pkt;
    vpx_enc_frame_flags_t flags = 0;
    if (g.force_key) {
        g.force_key = 0;
        flags |= VPX_EFLAG_FORCE_KF;
    }
    vpx_img_wrap(&img, VPX_IMG_FMT_I420, e->cfg.g_w, e->cfg.g_h, 1, i420);
    if (vpx_codec_encode(&e->ctx, &img, (vpx_codec_pts_t)pts_ms, 1000 / VIDEO_FPS, flags, VPX_DL_REALTIME) != VPX_CODEC_OK)
        return;
    while ((pkt = vpx_codec_get_cx_data(&e->ctx, &it)) != NULL)
        if (pkt->kind == VPX_CODEC_CX_FRAME_PKT)
            send_video_frame(pkt->data.frame.buf, pkt->data.frame.sz, (pkt->data.frame.flags & VPX_FRAME_IS_KEY) != 0, rtp_ts);
}

static void publish_self(const uint8_t *i420, int w, int h) {
    SDL_LockMutex(g.lock);
    if (!g.self_disp || g.self_w != w || g.self_h != h) {
        free(g.self_disp);
        g.self_disp = malloc((size_t)w * h * 3 / 2);
        g.self_w = w;
        g.self_h = h;
    }
    memcpy(g.self_disp, i420, (size_t)w * h * 3 / 2);
    g.self_new = 1;
    SDL_UnlockMutex(g.lock);
}

static int video_thread(void *arg) {
    venc_t enc = {0};
    av_in_t cam;
    int have_cam = 0, frame_no = 0;
    struct SwsContext *sws = NULL;
    AVFrame *frame = av_frame_alloc();
    uint8_t *buf = malloc(640 * 480 * 3 / 2);
    uint64_t next_us = now_us(), t0 = now_ms();
    (void)arg;

    if (!g.test_source) {
        have_cam = open_camera(&cam) == 0;
        if (!have_cam) log_msg("video", "no camera found; sending the test pattern instead (use --video-dev to pick one)");
    }
    venc_open(&enc, g.enc_level);

    while (g.running) {
        int level = g.enc_level, w, h;
        if (level != enc.level) venc_open(&enc, level);   /* new resolution needs a new encoder (starts with a keyframe) */
        w = CC_LADDER[level].width;
        h = CC_LADDER[level].height;

        if (have_cam) {
            if (av_next(&cam, frame) != 0) break;
            /* Cameras run at 30 fps; drop frames to keep our 20 fps send rate. */
            if (now_us() < next_us) {
                av_frame_unref(frame);
                continue;
            }
            next_us += 1000000 / VIDEO_FPS;
            if (now_us() > next_us + 200000) next_us = now_us();
            {
                uint8_t *dst[3] = {buf, buf + w * h, buf + w * h * 5 / 4};
                int ls[3] = {w, w / 2, w / 2};
                sws = sws_getCachedContext(sws, frame->width, frame->height, (enum AVPixelFormat)frame->format, w, h,
                                           AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);
                sws_scale(sws, (const uint8_t *const *)frame->data, frame->linesize, 0, frame->height, dst, ls);
            }
            av_frame_unref(frame);
        } else {
            uint64_t t = now_us();
            if (t < next_us) {
                sleep_ms((unsigned)((next_us - t) / 1000));
                continue;
            }
            next_us += 1000000 / VIDEO_FPS;
            if (t > next_us + 200000) next_us = t;
            test_pattern(buf, w, h, frame_no);
        }
        frame_no++;
        if (!g.cam_on) {
            memset(buf, 16, (size_t)w * h);
            memset(buf + w * h, 128, (size_t)w * h / 2);
        }
        publish_self(buf, w, h);
        venc_frame(&enc, buf, now_ms() - t0, (uint32_t)(now_us() * 9 / 100));
    }
    if (have_cam) av_close(&cam);
    if (enc.ok) vpx_codec_destroy(&enc.ctx);
    sws_freeContext(sws);
    av_frame_free(&frame);
    free(buf);
    return 0;
}

/* ================= audio thread ================= */

static int audio_thread(void *arg) {
    int err, have_mic = 0, nbuf = 0;
    OpusEncoder *enc = opus_encoder_create(AUDIO_RATE, 1, OPUS_APPLICATION_VOIP, &err);
    av_in_t mic;
    SwrContext *swr = NULL;
    AVFrame *frame = av_frame_alloc();
    int16_t pcm[AUDIO_FRAME * 8];
    uint32_t ts = rand_u32();
    uint64_t next_us = now_us();
    double phase = 0;
    int tone = 330 + (int)(rand_u32() % 5) * 55;   /* different pitch per participant */
    (void)arg;

    if (!enc) return 0;
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(32000));
    opus_encoder_ctl(enc, OPUS_SET_INBAND_FEC(1));          /* each packet carries a low-rate copy of the previous one */
    opus_encoder_ctl(enc, OPUS_SET_PACKET_LOSS_PERC(10));
    opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(5));

    if (!g.test_source) {
        have_mic = open_mic(&mic) == 0;
        if (have_mic) {
            AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
            if (swr_alloc_set_opts2(&swr, &mono, AV_SAMPLE_FMT_S16, AUDIO_RATE, &mic.dec->ch_layout, mic.dec->sample_fmt,
                                    mic.dec->sample_rate, 0, NULL) < 0 || swr_init(swr) < 0) {
                av_close(&mic);
                have_mic = 0;
            }
        }
        if (!have_mic) log_msg("audio", "no microphone found; sending a test tone instead (use --audio-dev to pick one)");
    }

    while (g.running) {
        uint8_t out[400];
        int n, i, loss = 0;
        if (have_mic) {
            int got;
            uint8_t *dst = (uint8_t *)(pcm + nbuf);
            if (av_next(&mic, frame) != 0) break;
            got = swr_convert(swr, &dst, AUDIO_FRAME * 8 - nbuf, (const uint8_t **)frame->extended_data,
                              frame->nb_samples);
            av_frame_unref(frame);
            if (got > 0) nbuf += got;
        } else {
            uint64_t t = now_us();
            if (t < next_us) {
                sleep_ms((unsigned)((next_us - t) / 1000));
                continue;
            }
            next_us += 20000;
            if (t > next_us + 200000) next_us = t;
            /* beeping tone: 0.4 s on, 0.6 s off */
            for (i = 0; i < AUDIO_FRAME; i++) {
                double env = fmod(phase / AUDIO_RATE, 1.0) < 0.4 ? 0.25 : 0.0;
                pcm[nbuf + i] = (int16_t)(32767 * env * sin(2 * M_PI * tone * phase / AUDIO_RATE));
                phase += 1;
            }
            nbuf += AUDIO_FRAME;
        }

        /* Tell the encoder how lossy the worst receiver says the path is (sizes the FEC). */
        SDL_LockMutex(g.lock);
        for (i = 0; i < MAX_PEERS; i++)
            if (g.peers[i].used && g.peers[i].rloss_a * 100 > loss) loss = (int)(g.peers[i].rloss_a * 100);
        SDL_UnlockMutex(g.lock);
        opus_encoder_ctl(enc, OPUS_SET_PACKET_LOSS_PERC(loss < 5 ? 5 : loss > 40 ? 40 : loss));

        while (nbuf >= AUDIO_FRAME) {
            if (!g.mic_on) memset(pcm, 0, AUDIO_FRAME * sizeof(int16_t));
            n = opus_encode(enc, pcm, AUDIO_FRAME, out, sizeof out);
            if (n > 0) send_audio_frame(out, (size_t)n, ts);
            ts += AUDIO_FRAME;
            memmove(pcm, pcm + AUDIO_FRAME, (size_t)(nbuf - AUDIO_FRAME) * sizeof(int16_t));
            nbuf -= AUDIO_FRAME;
        }
    }
    if (have_mic) av_close(&mic);
    swr_free(&swr);
    av_frame_free(&frame);
    opus_encoder_destroy(enc);
    return 0;
}

void media_in_start(void) {
    avdevice_register_all();
    av_log_set_level(AV_LOG_ERROR);
    vthread = SDL_CreateThread(video_thread, "video-in", NULL);
    athread = SDL_CreateThread(audio_thread, "audio-in", NULL);
}

void media_in_stop(void) {
    SDL_WaitThread(vthread, NULL);
    SDL_WaitThread(athread, NULL);
}
