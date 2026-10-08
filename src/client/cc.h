/* Congestion / quality control. Mini-Meet has no browser underneath any
 * more, so this is the only rate controller in the system.
 *
 * AIMD over a ladder of (bitrate, resolution) levels, driven by RTCP
 * receiver reports from each peer (about once a second):
 *   congestion  = loss > 8 %  or  queuing delay (RTT - min RTT) > 120 ms
 *                 -> multiplicative decrease: jump DOWN two levels
 *   clean       = loss < 2 %  and queuing delay < 40 ms, 3 reports in a row
 *                 -> additive increase: step UP one level
 * Watching queuing delay as well as loss means the controller backs off
 * when a bottleneck queue starts filling, before packets are dropped. */
#ifndef MM_CC_H
#define MM_CC_H

typedef struct {
    int kbps;
    int width, height;
} cc_level_t;

#define CC_LEVELS 6
extern const cc_level_t CC_LADDER[CC_LEVELS];

typedef struct {
    int    level;
    int    clean;
    double min_rtt_ms;
} cc_state_t;

void cc_init(cc_state_t *cc);
/* Feed one receiver report; returns 1 if the level changed. */
int  cc_on_report(cc_state_t *cc, double loss_frac, double rtt_ms);

#endif
