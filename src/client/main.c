/* mm: Mini-Meet call client.
 *
 *   mm --server HOST[:PORT] --room ROOM --name NAME [options]
 *
 * Options:
 *   --test               send a test pattern and tone instead of camera/mic
 *   --file PATH          send a video file (looped) instead of camera/mic
 *   --video-dev NAME     camera (Windows: DirectShow name, Linux: /dev/videoN)
 *   --audio-dev NAME     microphone (Windows: DirectShow name, Linux: pulse/alsa name)
 *   --stun HOST[:PORT]   STUN server (default stun.l.google.com:19302)
 *   --no-stun            host candidates only (same machine / LAN)
 *   --no-adapt           fixed 1200 kbps, no AIMD quality control
 *   --headless           no window and no playback (benchmarks)
 *   --csv PATH --run ID  write per-second stats
 *   --duration SECS      leave after SECS seconds
 *   --port N             local UDP port (default: any)
 *   --list-devices       print cameras and microphones and exit
 */
#include "client.h"
#include "../common/util.h"

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>

app_t g;

static void usage(void) {
    fprintf(stderr,
            "usage: mm --server HOST[:PORT] --room ROOM --name NAME [--test | --file PATH]\n"
            "          [--video-dev NAME] [--audio-dev NAME] [--stun HOST[:PORT] | --no-stun]\n"
            "          [--no-adapt] [--headless] [--csv PATH --run ID] [--duration SECS] [--port N]\n"
            "       mm --list-devices\n");
    exit(2);
}

static void split_host_port(const char *s, char *host, size_t cap, int *port) {
    const char *colon = strrchr(s, ':');
    if (colon) {
        size_t n = (size_t)(colon - s) < cap - 1 ? (size_t)(colon - s) : cap - 1;
        memcpy(host, s, n);
        host[n] = '\0';
        *port = atoi(colon + 1);
    } else {
        snprintf(host, cap, "%s", s);
    }
}

static void list_devices(void) {
#ifdef _WIN32
    const AVInputFormat *f;
    AVDeviceInfoList *list = NULL;
    int i, j;
    avdevice_register_all();
    f = av_find_input_format("dshow");
    if (!f || avdevice_list_input_sources(f, NULL, NULL, &list) < 0 || !list) {
        printf("no DirectShow devices\n");
        return;
    }
    for (i = 0; i < list->nb_devices; i++) {
        AVDeviceInfo *d = list->devices[i];
        const char *kind = "?";
        for (j = 0; j < d->nb_media_types; j++)
            kind = d->media_types[j] == AVMEDIA_TYPE_VIDEO ? "camera" : d->media_types[j] == AVMEDIA_TYPE_AUDIO ? "mic" : kind;
        printf("%-7s \"%s\"\n", kind, d->device_description);
    }
    avdevice_free_list_devices(&list);
#else
    printf("cameras: /dev/video*   microphones: pulse/alsa \"default\"\n");
#endif
}

static void on_signal(int sig) {
    (void)sig;
    g.running = 0;
}

int main(int argc, char **argv) {
    int i;
    SDL_Thread *net;

    memset(&g, 0, sizeof g);
    snprintf(g.server_host, sizeof g.server_host, "127.0.0.1");
    g.server_port = MM_DEFAULT_PORT;
    snprintf(g.stun_host, sizeof g.stun_host, "stun.l.google.com");
    g.stun_port = 19302;
    g.use_stun = 1;
    g.adapt = 1;
    g.mic_on = g.cam_on = 1;
    g.show_stats = 1;
    g.tcp = g.udp = SOCK_INVALID;
    snprintf(g.run, sizeof g.run, "live");

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
#define NEXT() (i + 1 < argc ? argv[++i] : (usage(), ""))
        if (!strcmp(a, "--server")) split_host_port(NEXT(), g.server_host, sizeof g.server_host, &g.server_port);
        else if (!strcmp(a, "--room")) snprintf(g.room, sizeof g.room, "%s", NEXT());
        else if (!strcmp(a, "--name")) snprintf(g.name, sizeof g.name, "%s", NEXT());
        else if (!strcmp(a, "--stun")) split_host_port(NEXT(), g.stun_host, sizeof g.stun_host, &g.stun_port);
        else if (!strcmp(a, "--no-stun")) g.use_stun = 0;
        else if (!strcmp(a, "--no-adapt")) g.adapt = 0;
        else if (!strcmp(a, "--headless")) g.headless = 1;
        else if (!strcmp(a, "--test")) g.test_source = 1;
        else if (!strcmp(a, "--file")) snprintf(g.file_src, sizeof g.file_src, "%s", NEXT());
        else if (!strcmp(a, "--video-dev")) snprintf(g.video_dev, sizeof g.video_dev, "%s", NEXT());
        else if (!strcmp(a, "--audio-dev")) snprintf(g.audio_dev, sizeof g.audio_dev, "%s", NEXT());
        else if (!strcmp(a, "--csv")) snprintf(g.csv_path, sizeof g.csv_path, "%s", NEXT());
        else if (!strcmp(a, "--run")) snprintf(g.run, sizeof g.run, "%s", NEXT());
        else if (!strcmp(a, "--duration")) g.duration = atoi(NEXT());
        else if (!strcmp(a, "--port")) g.udp_port = atoi(NEXT());
        else if (!strcmp(a, "--list-devices")) {
            list_devices();
            return 0;
        } else usage();
#undef NEXT
    }
    if (!g.room[0] || !g.name[0] || !mm_valid_name(g.room) || !mm_valid_name(g.name)) {
        fprintf(stderr, "--room and --name are required (1-32 chars of A-Z a-z 0-9 _ -)\n");
        usage();
    }

    if (net_init() != 0) return 1;
    signal(SIGINT, on_signal);
    if (SDL_Init(g.headless ? SDL_INIT_TIMER : (SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    g.lock = SDL_CreateMutex();
    g.txhist = calloc(TX_HISTORY, sizeof *g.txhist);
    g.vssrc = rand_u32();
    g.assrc = rand_u32();
    g.vseq = (uint16_t)rand_u32();
    g.aseq = (uint16_t)rand_u32();
    g.running = 1;
    g.start_ms = now_ms();

    if (g.csv_path[0]) {
        g.csv = fopen(g.csv_path, "w");
        if (g.csv)
            fputs("ts,run,peer,remote,kind,direction,bitrate_kbps,packets_lost,loss_pct,jitter_ms,rtt_ms,fps,width,"
                  "height,avail_out_kbps,limit_reason,concealed_pct,freeze_count,adapt_on,adapt_level,candidate_type\n",
                  g.csv);
        else log_msg("main", "cannot write %s", g.csv_path);
    }

    g.udp = udp_bind((uint16_t)g.udp_port);
    if (g.udp == SOCK_INVALID) {
        log_msg("main", "cannot open UDP socket");
        return 1;
    }
    ice_gather();
    if (sig_connect() != 0) return 1;

    net = net_thread_start();
    media_in_start();

    if (g.headless) {
        while (g.running) sleep_ms(50);
    } else {
        audio_out_start();
        window_run();
    }

    g.running = 0;
    sig_send("LEAVE");
    media_in_stop();
    SDL_WaitThread(net, NULL);
    if (g.csv) fclose(g.csv);
    sock_close(g.tcp);
    sock_close(g.udp);
    SDL_Quit();
    net_cleanup();
    log_msg("main", "bye");
    return 0;
}
