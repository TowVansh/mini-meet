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
    cc->min_rtt_ms = -1;
}

int cc_on_report(cc_state_t *cc, double loss, double rtt_ms) {
    int old = cc->level;
    double qdelay = 0;
    if (rtt_ms >= 0) {
        if (cc->min_rtt_ms < 0 || rtt_ms < cc->min_rtt_ms) cc->min_rtt_ms = rtt_ms;
        qdelay = rtt_ms - cc->min_rtt_ms;
    }
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
