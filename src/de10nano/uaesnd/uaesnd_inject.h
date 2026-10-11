/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: MIT
*/

/* uaesnd: the EXTER interrupt decision. */

#ifndef UAESND_INJECT_H
#define UAESND_INJECT_H

#include <stdint.h>

#ifndef UAESND_REASSERT_TICKS
#define UAESND_REASSERT_TICKS 96u
#endif

enum {
    UAS_FIRE_NONE     = 0,
    UAS_FIRE_EDGE     = 1,
    UAS_FIRE_CLRFIRE  = 2,
    UAS_FIRE_REASSERT = 3,
};

struct uas_inject_state {
    uint32_t prev_pending;   /* pending mask as of the previous tick          */
    uint32_t since;          /* ticks since the last fire while pending       */
    uint32_t age;            /* ticks the current pending episode has lasted  */
};

static inline int uaesnd_inject_step(struct uas_inject_state *st,
                                     uint32_t pending, uint32_t clear_hint)
{
    int why = UAS_FIRE_NONE;

    if (pending & ~st->prev_pending)      why = UAS_FIRE_EDGE;
    else if (pending && clear_hint)       why = UAS_FIRE_CLRFIRE;
    else if (pending && ++st->since >= UAESND_REASSERT_TICKS)
                                          why = UAS_FIRE_REASSERT;

    if (why != UAS_FIRE_NONE)
        st->since = 0;

    if (pending) {
        st->age++;
    } else {
        st->age = 0;
        st->since = 0;
    }
    st->prev_pending = pending;
    return why;
}

#endif /* UAESND_INJECT_H */
