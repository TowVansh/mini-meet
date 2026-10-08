/* Congestion / quality control. Mini-Meet has no browser underneath any
 * more, so this is the only rate controller in the system.
 *
 * AIMD over a ladder of (bitrate, resolution) levels, driven by RTCP
 * receiver reports from each peer (about once a second):
 *   congestion  = loss > 8 %  or  queuing delay > 120 ms
 *                 -> multiplicative decrease: jump DOWN two levels
 *   clean       = loss < 2 %  and queuing delay < 40 ms, 3 reports in a row
 *                 -> additive increase: step UP one level
 * Queuing delay = smoothed RTT - minimum smoothed RTT over the last 10 s. The
 * windowed minimum (as in BBR) lets the baseline move when the path itself
 * gets longer, so a constant extra delay is not mistaken for a growing queue,
 * while a queue that keeps filling still shows up. */
#ifndef MM_CC_H
#define MM_CC_H

#include <stdint.h>

typedef struct {
    int kbps;
    int width, height;
} cc_level_t;

#define CC_LEVELS 6
#define CC_RTT_WINDOW 10
extern const cc_level_t CC_LADDER[CC_LEVELS];

typedef struct {
    int      level;
    int      clean;
    double   srtt_ms;                       /* EWMA of RTT, -1 = none yet */
    double   rtt_hist[CC_RTT_WINDOW];       /* one sample per report, ring */
    int      nhist, hist_pos;
} cc_state_t;

void   cc_init(cc_state_t *cc);
/* Feed one receiver report; returns 1 if the level changed. */
int    cc_on_report(cc_state_t *cc, double loss_frac, double rtt_ms);
double cc_queuing_delay(const cc_state_t *cc);

#endif
