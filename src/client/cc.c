#include "cc.h"

const cc_level_t CC_LADDER[CC_LEVELS] = {
    {1200, 640, 480},
    { 800, 640, 480},
    { 500, 480, 360},
    { 300, 320, 240},
    { 180, 320, 240},
    { 100, 160, 120},
};

#define LOSS_BAD   0.08
#define LOSS_OK    0.02
#define QDELAY_BAD 120.0
#define QDELAY_OK  40.0
#define CLEAN_ROUNDS 3

void cc_init(cc_state_t *cc) {
    cc->level = 0;
    cc->clean = 0;
    cc->srtt_ms = -1;
    cc->nhist = 0;
    cc->hist_pos = 0;
}

double cc_queuing_delay(const cc_state_t *cc) {
    double mn;
    int i;
    if (cc->nhist == 0 || cc->srtt_ms < 0) return 0;
    mn = cc->rtt_hist[0];
    for (i = 1; i < cc->nhist; i++)
        if (cc->rtt_hist[i] < mn) mn = cc->rtt_hist[i];
    return cc->srtt_ms > mn ? cc->srtt_ms - mn : 0;
}

int cc_on_report(cc_state_t *cc, double loss, double rtt_ms) {
    int old = cc->level;
    double qdelay;
    if (rtt_ms >= 0) {
        /* Smooth out jitter in single samples (like TCP's SRTT, alpha = 1/4). */
        cc->srtt_ms = cc->srtt_ms < 0 ? rtt_ms : 0.75 * cc->srtt_ms + 0.25 * rtt_ms;
        /* Window holds smoothed values too, so random jitter in single samples
         * does not drag the baseline down and look like queuing delay. */
        cc->rtt_hist[cc->hist_pos] = cc->srtt_ms;
        cc->hist_pos = (cc->hist_pos + 1) % CC_RTT_WINDOW;
        if (cc->nhist < CC_RTT_WINDOW) cc->nhist++;
    }
    qdelay = cc_queuing_delay(cc);
    if (loss > LOSS_BAD || qdelay > QDELAY_BAD) {
        cc->level += 2;
        if (cc->level >= CC_LEVELS) cc->level = CC_LEVELS - 1;
        cc->clean = 0;
    } else if (loss < LOSS_OK && qdelay < QDELAY_OK) {
        if (++cc->clean >= CLEAN_ROUNDS) {
            if (cc->level > 0) cc->level--;
            cc->clean = 0;
        }
    } else {
        cc->clean = 0;
    }
    return cc->level != old;
}
