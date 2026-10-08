/* Shared state of the mm client. Threads:
 *   main      SDL window + event loop (or idle wait when --headless)
 *   net       select() over the signalling TCP socket and the media UDP socket:
 *             signalling, ICE checks, RTP receive, jitter buffers, decoding,
 *             RTCP, NACK, stats
 *   video-in  capture -> scale -> VP8 encode -> packetize -> send
 *   audio-in  capture -> resample -> Opus encode -> send
 * g_lock protects the peer table and the shared sender state. */
#ifndef MM_CLIENT_H
#define MM_CLIENT_H

#include <stdio.h>
#include <stdint.h>
#include <SDL.h>
#include <vpx/vpx_decoder.h>
#include <opus/opus.h>

#include "../common/net.h"
#include "../common/proto.h"
#include "rtp.h"
#include "cc.h"

#define MAX_PEERS      (MM_MAX_ROOM - 1)
#define MAX_CANDS      6
#define VJB_SLOTS      1024     /* video jitter buffer, indexed by seq % slots */
#define AJB_SLOTS      64       /* audio jitter buffer */
#define TX_HISTORY     1024     /* sent video packets kept for retransmission */
#define NACK_MAX       256
#define AUDIO_RATE     48000
#define AUDIO_FRAME    960      /* 20 ms at 48 kHz */
#define AUDIO_RING     (AUDIO_RATE / 2)  /* 500 ms playback ring per peer */
#define PKT_MAX        1500

enum { ICE_NEW, ICE_CHECKING, ICE_CONNECTED, ICE_FAILED };

typedef struct {
    endpoint_t addr;
    char       type[8];         /* host | srflx | prflx */
    uint8_t    tid[12];         /* transaction ID of our last check to it */
} cand_t;

typedef struct {
    int      used;
    uint16_t seq;
    uint32_t ts;
    int      marker, start, key;
    uint16_t len;
    uint8_t  data[PKT_MAX];
} vslot_t;

typedef struct {
    int      used;
    uint16_t seq;
    uint16_t len;
    uint8_t  data[512];
} aslot_t;

typedef struct {
    uint16_t seq;
    uint64_t first_us, last_sent_us;
    int      tries;
} nack_t;

typedef struct {
    int  used;
    char id[MM_ID_LEN + 1];
    char name[MM_NAME_MAX + 1];

    /* --- ICE / connectivity --- */
    cand_t     cands[MAX_CANDS];
    int        ncands;
    int        ice_state;
    endpoint_t sel;             /* selected remote address for media */
    char       ltype[8], rtype[8];
    uint64_t   ice_start_ms, last_check_ms, last_rx_ms, last_keepalive_ms;

    /* --- receive side --- */
    rx_stats_t rxv, rxa;
    vslot_t   *vjb;             /* VJB_SLOTS */
    int        vjb_started;
    uint16_t   play_seq;        /* first packet of next frame to assemble */
    uint16_t   high_seq;
    uint64_t   stall_us;        /* when assembly got stuck at play_seq (0 = not stuck) */
    nack_t     nacks[NACK_MAX];
    int        nnacks;
    vpx_codec_ctx_t vdec;
    int        vdec_ok;
    int        need_key;
    uint64_t   last_pli_us;
    uint32_t   remote_vssrc, remote_assrc;

    aslot_t    ajb[AJB_SLOTS];
    int        ajb_started;
    uint16_t   aplay_seq, ahigh_seq;
    uint64_t   anext_us;        /* next 20 ms playout tick */
    uint64_t   alast_rx_us;
    OpusDecoder *adec;
    int16_t    aring[AUDIO_RING];   /* decoded PCM for the SDL callback */
    int        aring_r, aring_w, aring_n;

    /* decoded picture for the window (g_lock) */
    uint8_t   *disp;
    int        disp_w, disp_h, disp_new;

    /* --- feedback about OUR streams, from this peer's reports --- */
    double     rtt_ms;
    double     rloss_v, rjitter_v_ms, rloss_a, rjitter_a_ms;
    cc_state_t cc;

    /* --- per-second counters (reset by stats tick) --- */
    uint64_t   rx_bytes_v, rx_bytes_a, tx_bytes_v, tx_bytes_a;
    int        frames_decoded, freezes, nack_sent, rtx_recv, plis_sent;
    int        aframes, aconcealed, afec;   /* afec = frames rebuilt from in-band FEC */
    int        alate;
    double     avg_frame_ms;
    uint64_t   last_frame_us;
    double     lost_pct_v, lost_pct_a;   /* receive loss over last interval */
    uint32_t   exp_prior_v, rec_prior_v, exp_prior_a, rec_prior_a;
} peer_t;

typedef struct {
    /* options */
    char server_host[128];
    int  server_port;
    char room[MM_NAME_MAX + 1];
    char name[MM_NAME_MAX + 1];
    char stun_host[128];
    int  stun_port;
    int  use_stun;
    int  headless;
    int  adapt;
    int  test_source;
    char video_dev[256], audio_dev[256], file_src[512];
    char csv_path[512];
    char run[128];
    int  duration;              /* seconds, 0 = until quit */
    int  udp_port;

    /* runtime */
    volatile int running;
    SDL_mutex *lock;
    sock_t     udp, tcp;
    char       self_id[MM_ID_LEN + 1];
    int        joined;
    cand_t     lcands[2];       /* our host + srflx */
    int        nlcands;
    peer_t     peers[MAX_PEERS];

    /* sender (g_lock) */
    uint32_t   vssrc, assrc;
    uint16_t   vseq, aseq;
    uint32_t   vpackets, voctets, last_vts;
    struct { int used; uint16_t seq; uint16_t len; uint8_t data[PKT_MAX]; uint64_t sent_us; } *txhist;
    volatile int enc_level;     /* ladder level applied by the encoder */
    volatile int force_key;
    volatile int mic_on, cam_on;

    /* self view (g_lock) */
    uint8_t   *self_disp;
    int        self_w, self_h, self_new;

    FILE      *csv;
    int        show_stats;
    uint64_t   start_ms;
} app_t;

extern app_t g;

/* ice.c */
int   ice_gather(void);
void  ice_add_remote(peer_t *p, const char *type, const endpoint_t *ep);
void  ice_tick(peer_t *p, uint64_t now);
void  ice_on_stun(const uint8_t *buf, size_t len, const endpoint_t *from);
void  ice_send_candidates(peer_t *p);

/* sig.c */
int   sig_connect(void);
void  sig_on_readable(void);
void  sig_send(const char *fmt, ...);
void  sig_tick(uint64_t now);

/* peers (netloop.c) */
peer_t *peer_find(const char *id);
peer_t *peer_add(const char *id, const char *name);
void    peer_remove(peer_t *p);
peer_t *peer_by_addr(const endpoint_t *ep);
SDL_Thread *net_thread_start(void);

/* media_out.c */
void  rx_video(peer_t *p, const rtp_hdr_t *h, const uint8_t *pl, size_t n, int rtx, uint64_t now_us);
void  rx_audio(peer_t *p, const rtp_hdr_t *h, const uint8_t *pl, size_t n, uint64_t now_us);
void  rx_tick(peer_t *p, uint64_t now_us);
void  audio_out_start(void);
void  window_run(void);
void  peer_media_free(peer_t *p);

/* media_in.c */
void  media_in_start(void);
void  media_in_stop(void);
void  send_video_frame(const uint8_t *data, size_t len, int key, uint32_t ts);
void  send_audio_frame(const uint8_t *data, size_t len, uint32_t ts);
int   tx_resend(peer_t *p, uint16_t seq);

/* font.c */
void  font_draw(SDL_Renderer *r, int x, int y, int scale, const char *text);
int   font_width(const char *text, int scale);
void  font_draw_into_i420(uint8_t *y_plane, int stride, int w, int h, int x, int y, int scale, const char *text);

#endif
