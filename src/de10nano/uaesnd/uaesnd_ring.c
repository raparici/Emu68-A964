/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: GPL-3.0-or-later
*/

/* uaesnd: the PCM ring to the Main on core #0. */

#include <stdint.h>
#include "amiga_memmap.h"
#include "uaesnd_ring.h"
#ifdef EMU68_UAESND
#include "uaesnd_card.h"
#endif

volatile uint32_t g_uaesnd_active;

static volatile uint32_t *const ctl  = (volatile uint32_t *)UAESND_CTRL_DDR;
static uint8_t          *const ring  = (uint8_t *)UAESND_RING_DDR;

uint32_t g_uaesnd_sticky;       /* mirrored to UAESND_CTRL_STATE, never cleared */
#include "uaesnd_inject.h"

volatile uint32_t g_uaesnd_intreq_clear_hint;

volatile uint32_t g_uaesnd_defer_inject;
static uint32_t pending_inject;
#define sticky g_uaesnd_sticky
static uint32_t ticks;
static uint32_t ph_l, ph_r;     /* 16.16 phase into the sine table              */
static uint32_t ramp_n;
static uint32_t full_run;       /* consecutive ticks with the ring at target */

static const int16_t sine256[256] = {
         0,    402,    804,   1205,   1606,   2006,   2404,   2801,
      3196,   3590,   3981,   4370,   4756,   5139,   5520,   5897,
      6270,   6639,   7005,   7366,   7723,   8076,   8423,   8765,
      9102,   9434,   9760,  10080,  10394,  10702,  11003,  11297,
     11585,  11866,  12140,  12406,  12665,  12916,  13160,  13395,
     13623,  13842,  14053,  14256,  14449,  14635,  14811,  14978,
     15137,  15286,  15426,  15557,  15679,  15791,  15893,  15986,
     16069,  16143,  16207,  16261,  16305,  16340,  16364,  16379,
     16384,  16379,  16364,  16340,  16305,  16261,  16207,  16143,
     16069,  15986,  15893,  15791,  15679,  15557,  15426,  15286,
     15137,  14978,  14811,  14635,  14449,  14256,  14053,  13842,
     13623,  13395,  13160,  12916,  12665,  12406,  12140,  11866,
     11585,  11297,  11003,  10702,  10394,  10080,   9760,   9434,
      9102,   8765,   8423,   8076,   7723,   7366,   7005,   6639,
      6270,   5897,   5520,   5139,   4756,   4370,   3981,   3590,
      3196,   2801,   2404,   2006,   1606,   1205,    804,    402,
         0,   -402,   -804,  -1205,  -1606,  -2006,  -2404,  -2801,
     -3196,  -3590,  -3981,  -4370,  -4756,  -5139,  -5520,  -5897,
     -6270,  -6639,  -7005,  -7366,  -7723,  -8076,  -8423,  -8765,
     -9102,  -9434,  -9760, -10080, -10394, -10702, -11003, -11297,
    -11585, -11866, -12140, -12406, -12665, -12916, -13160, -13395,
    -13623, -13842, -14053, -14256, -14449, -14635, -14811, -14978,
    -15137, -15286, -15426, -15557, -15679, -15791, -15893, -15986,
    -16069, -16143, -16207, -16261, -16305, -16340, -16364, -16379,
    -16384, -16379, -16364, -16340, -16305, -16261, -16207, -16143,
    -16069, -15986, -15893, -15791, -15679, -15557, -15426, -15286,
    -15137, -14978, -14811, -14635, -14449, -14256, -14053, -13842,
    -13623, -13395, -13160, -12916, -12665, -12406, -12140, -11866,
    -11585, -11297, -11003, -10702, -10394, -10080,  -9760,  -9434,
     -9102,  -8765,  -8423,  -8076,  -7723,  -7366,  -7005,  -6639,
     -6270,  -5897,  -5520,  -5139,  -4756,  -4370,  -3981,  -3590,
     -3196,  -2801,  -2404,  -2006,  -1606,  -1205,   -804,   -402,};

/* step in 16.16 table-entries per output frame for a given Hz */
static inline uint32_t phase_step(uint32_t hz)
{
    return (uint32_t)(((uint64_t)hz * 256u * 65536u) / UAESND_OUT_RATE);
}

#ifdef EMU68_UAESND
void uaesnd_card_playing(int on)
{
    if (on) {
        if (ctl[UAESND_CTRL_MODE / 4] != 3u) {
            extern void uaesnd_stage_negotiate(void);
            uaesnd_stage_negotiate();
        }
        g_uaesnd_active = 1u;
    }
    ctl[UAESND_CTRL_MODE / 4] = on ? 3u : 0u;
}
#endif

void uaesnd_ring_disarm(void)
{
    ctl[UAESND_CTRL_MAGIC / 4] = 0u;
}

void uaesnd_ring_arm(void)
{
    ctl[UAESND_CTRL_PROD    / 4] = 0;
    ctl[UAESND_CTRL_CONS    / 4] = 0;
    ctl[UAESND_CTRL_MAIN_UP / 4] = 0;
    ctl[UAESND_CTRL_UNDER   / 4] = 0;
    ctl[UAESND_CTRL_TICKS   / 4] = 0;
    ctl[UAESND_CTRL_MODE    / 4] = 0;
    sticky = UAESND_ST_ARMED;
#if defined(EMU68_UAESND) && defined(UAESND_STG_MAGIC)
    { extern void uaesnd_stage_negotiate(void); uaesnd_stage_negotiate(); }
#endif
    ctl[UAESND_CTRL_STATE   / 4] = sticky;
    /* magic LAST: the pump treats it as "everything above is valid" */
    ctl[UAESND_CTRL_MAGIC   / 4] = UAESND_MAGIC;
}

void uaesnd_tick(void)
{
    ticks++;
    ctl[UAESND_CTRL_TICKS / 4] = ticks;

    uint32_t mode = ctl[UAESND_CTRL_MODE / 4];

    {
        extern uint32_t uaesnd_exter_line(void);
        extern struct uaesnd_card g_uaesnd;
        static struct uas_inject_state inj;
        if (mode == 3u || uaesnd_exter_line()) {
        uint32_t hint = g_uaesnd_intreq_clear_hint;
        if (hint) g_uaesnd_intreq_clear_hint = 0;
        uint32_t pending = g_uaesnd.intreq_mask & g_uaesnd.intena_mask;
        int why = uaesnd_inject_step(&inj, pending, hint);
        if (why != UAS_FIRE_NONE) {
            if (g_uaesnd_defer_inject) {
                pending_inject = 1;             /* the in-blit law, if ever on */
            } else {
                extern void ps_write_16(unsigned int, unsigned int);
                ps_write_16(0x00DFF09Cu, 0xA000u);
            }
        } else if (!g_uaesnd_defer_inject && pending_inject) {
            pending_inject = 0;
            extern void ps_write_16(unsigned int, unsigned int);
            ps_write_16(0x00DFF09Cu, 0xA000u);
        }
        }
    }

    {
        extern uint32_t g_uaesnd_staging;
        if (g_uaesnd_staging) {
            extern void uaesnd_stage_fire_due_all(struct uaesnd_card *c);
            extern struct uaesnd_card g_uaesnd;
            uaesnd_stage_fire_due_all(&g_uaesnd);
        }
    }

    if (mode == 0u) {
        g_uaesnd_active = 0u;       /* back to the free idle path */
        return;
    }

#if defined(EMU68_UAESND) && defined(UAESND_STG_MAGIC)
    {
        extern uint32_t g_uaesnd_staging;
        if (g_uaesnd_staging) {
            if (mode == 3u) {
                extern struct uaesnd_card g_uaesnd;
                extern void uaesnd_stage_tick(struct uaesnd_card *c);
                uaesnd_stage_tick(&g_uaesnd);
            }
            return;
        }
    }
#endif

    uint32_t prod = ctl[UAESND_CTRL_PROD / 4];
    uint32_t cons = ctl[UAESND_CTRL_CONS / 4];
    uint32_t inflight = prod - cons;        /* free-running: wraps correctly */

    if (cons != 0u) sticky |= UAESND_ST_MAINSEEN;
    if (inflight > UAESND_RING_BYTES) {
        sticky |= UAESND_ST_CONSBAD;
        ctl[UAESND_CTRL_STATE / 4] = sticky;
        ctl[UAESND_CTRL_CONS  / 4] = prod;
        return;
    }
    if (inflight == 0u && (sticky & UAESND_ST_PRODUCED)) {
        sticky |= UAESND_ST_UNDERRUN;
        ctl[UAESND_CTRL_UNDER / 4] = ctl[UAESND_CTRL_UNDER / 4] + 1u;
    }

    uint32_t want = UAESND_LEAD_FRAMES * UAESND_BYTES_FRAME;
    if (g_uaesnd_defer_inject) {
        uint32_t cap = (UAESND_OUT_RATE / 100u) * UAESND_BYTES_FRAME;  /* 10 ms */
        if (want > inflight + cap) want = inflight + cap;
    }
    if (inflight >= want) {
        if (++full_run > 86000u) {
            sticky |= UAESND_ST_OVERRUN;
            ctl[UAESND_CTRL_STATE / 4] = sticky;
            full_run = 0;
        }
        return;
    }
    full_run = 0;
    uint32_t frames = (want - inflight) / UAESND_BYTES_FRAME;

#ifdef EMU68_UAESND
    if (mode == 3u) {
        extern void uaesnd_mix(struct uaesnd_card *c, uint8_t *dst, uint32_t n);
        extern struct uaesnd_card g_uaesnd;
        for (uint32_t i = 0; i < frames; i++) {
            uaesnd_mix(&g_uaesnd, ring + (prod & (UAESND_RING_BYTES - 1u)), 1u);
            prod += UAESND_BYTES_FRAME;
        }
        sticky |= UAESND_ST_PRODUCED;
        ctl[UAESND_CTRL_STATE / 4] = sticky;
        ctl[UAESND_CTRL_PROD  / 4] = prod;
        return;
    }
#endif
    uint32_t sl = phase_step(440u);          /* left  440 Hz */
    uint32_t sr = phase_step(660u);
    for (uint32_t i = 0; i < frames; i++) {
        int16_t l, r;
        if (mode == 2u) {
            l = (int16_t)(uint16_t)(ramp_n & 0xFFFFu);
            r = (int16_t)(uint16_t)((ramp_n & 0xFFFFu) ^ 0x5A5Au);
            ramp_n++;
        } else {
            l = sine256[(ph_l >> 16) & 0xFFu];
            r = sine256[(ph_r >> 16) & 0xFFu];
            ph_l += sl;
            ph_r += sr;
        }

        uint8_t *p = ring + (prod & (UAESND_RING_BYTES - 1u));
        p[0] = (uint8_t)((uint16_t)l & 0xFFu);
        p[1] = (uint8_t)(((uint16_t)l >> 8) & 0xFFu);
        p[2] = (uint8_t)((uint16_t)r & 0xFFu);
        p[3] = (uint8_t)(((uint16_t)r >> 8) & 0xFFu);
        prod += UAESND_BYTES_FRAME;
    }

    sticky |= UAESND_ST_PRODUCED;
    ctl[UAESND_CTRL_STATE / 4] = sticky;
    ctl[UAESND_CTRL_PROD  / 4] = prod;
}
