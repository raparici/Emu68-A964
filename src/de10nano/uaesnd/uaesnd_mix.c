/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: GPL-3.0-or-later
*/

/* uaesnd: the descriptor walk, the resampler and the mixer. */

#include <stdint.h>
#include "uaesnd_card.h"
#include "uaesnd_ring.h"
#include "amiga_memmap.h"
#include <support.h>

extern unsigned ps_read_8(unsigned addr);
extern unsigned ps_read_32(unsigned addr);
static inline int g_is_chip(uint32_t a) { return a < 0x00200000u; }
static uint32_t g_chip_line = 0xFFFFFFFFu, g_chip_val;
static inline uint8_t g_rd8(uint32_t a)
{
    if (g_is_chip(a)) {
        uint32_t base = a & ~3u;
        if (base != g_chip_line) { g_chip_line = base; g_chip_val = ps_read_32(base); }
        return (uint8_t)(g_chip_val >> (24u - 8u * (a & 3u)));
    }
    return *(const volatile uint8_t *)(uintptr_t)a;
}

static inline uint16_t g_rd16(uint32_t a)
{
    return (uint16_t)(((uint16_t)g_rd8(a) << 8) | g_rd8(a + 1));
}
static inline uint32_t g_rd32(uint32_t a)
{
    return ((uint32_t)g_rd16(a) << 16) | g_rd16(a + 2);
}

extern uint8_t *uaesnd_stream_regs(unsigned n);
static uint32_t freq_to_step(uint32_t f);
static void raise_int(struct uaesnd_card *c, unsigned n, uint8_t bit);
static void stg_on_start(struct uaesnd_card *c, unsigned n);
static void stg_on_param(struct uaesnd_card *c, unsigned n);
static int  stg_set_flags(struct uaesnd_card *c, unsigned n, int from_start);
extern uint32_t g_uaesnd_sticky;      /* uaesnd_ring.c publishes it */

/* Big-endian accessors for the card's own register block (the guest's view). */
static inline uint32_t r_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}
static inline uint16_t r_be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline void w_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}
static inline void w_be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

static void set_to_regs(struct uaesnd_stream *st)
{
    uint8_t *r = uaesnd_stream_regs((unsigned)(st->idx));
    w_be32(r + 4,  st->cur.pointer + st->pos);
    w_be32(r + 8,  st->cur.length);
    w_be16(r + 24, st->cur.repeat_count);
}

/* ---- the 40-byte sample set, read from guest memory ---------------------- */
void uaesnd_fetch_set(struct uaesnd_set *s, uint32_t addr)
{
    s->flags          = g_rd16(addr +  0);
    s->format         = g_rd16(addr +  2);
    s->pointer        = g_rd32(addr +  4);
    s->length         = g_rd32(addr +  8);
    s->frequency      = g_rd32(addr + 12);
    s->repeat_pointer = g_rd32(addr + 16);
    s->repeat_length  = g_rd32(addr + 20);
    s->repeat_count   = g_rd16(addr + 24);
    s->volume         = g_rd16(addr + 26);
    s->next           = g_rd32(addr + 28);
    s->channels       = g_rd8 (addr + 32);
    s->type           = g_rd8 (addr + 33);
    s->intena         = g_rd8 (addr + 34);
    s->mask           = g_rd8 (addr + 35);
    s->hpan           = g_rd16(addr + 36);
    s->dpan           = g_rd16(addr + 38);
    s->orig_pointer   = s->pointer;
    s->orig_length    = s->length;
}

static inline unsigned sample_bytes(uint8_t type)
{
    switch (type & 7u) { case 3: return 4; case 2: return 3; case 1: return 2; default: return 1; }
}

static unsigned decode_frame(const struct uaesnd_set *set, uint32_t addr,
                             int16_t *l, int16_t *r)
{
    unsigned st = set->type & 7u;
    int      le = (set->type & 0x80u) != 0;
    int      bias = (set->type & 0x40u) != 0;
    unsigned bytes = sample_bytes(set->type);
    unsigned ch = set->channels ? set->channels : 1u;
    int16_t  v[2] = { 0, 0 };

    for (unsigned i = 0; i < ch; i++) {
        uint16_t s;
        switch (st) {
        case 3:  s = g_rd16(le ? addr + 2 : addr); break;      /* 32-bit: top 16 */
        case 2:  s = g_rd16(le ? addr + 1 : addr); break;      /* 24-bit         */
        case 1:  s = g_rd16(addr);                 break;      /* 16-bit         */
        default: { uint8_t b = g_rd8(addr); s = (uint16_t)((b << 8) | b); break; }
        }
        addr += bytes;
        if (le) s = (uint16_t)((s >> 8) | (s << 8));
        if (bias) s = (uint16_t)(s - 0x8000u);
        if (i < 2) v[i] = (int16_t)s;
    }
    if (ch == 1u) { *l = v[0]; *r = v[0]; }
    else          { *l = v[0]; *r = v[1]; }
    return bytes * ch;
}

static void stream_frame(struct uaesnd_card *c, unsigned n, int32_t *ml, int32_t *mr)
{
    struct uaesnd_stream *st = &c->s[n];
    struct uaesnd_set    *set = &st->cur;

    if (!(st->status & 1u) || !(c->enable_mask & (1u << n)))
        return;
    if (set->length == 0u && !st->repeating)
        return;

    static uint32_t sf_freq[UAESND_STREAMS], sf_step[UAESND_STREAMS];
    const uint32_t rawfreq = r_be32(uaesnd_stream_regs(n) + 12);
    if (rawfreq != sf_freq[n]) { sf_freq[n] = rawfreq; sf_step[n] = freq_to_step(rawfreq); }
    const uint32_t step = sf_step[n];

    while (st->phase >= 0x10000u) {
        st->phase -= 0x10000u;
        st->pl = st->cl;
        st->pr = st->cr;
        unsigned used = decode_frame(set, set->pointer + st->pos, &st->cl, &st->cr);
        st->pos += used;
        if (set->length > 0u && --set->length == 0u) {
            uaesnd_stream_advance(c, n);       /* repeat / next set / stop      */
            return;
        }
    }

    uint32_t f = st->phase & 0xFFFFu;
    int32_t  l = st->pl + (int32_t)(((int64_t)(st->cl - st->pl) * (int64_t)f) >> 16);
    int32_t  r = st->pr + (int32_t)(((int64_t)(st->cr - st->pr) * (int64_t)f) >> 16);

    /* set volume is 0..32768 */
    l = (l * (int32_t)set->volume) >> 15;
    r = (r * (int32_t)set->volume) >> 15;

    /* a mono stream can be steered by the channel mask (0 = both) */
    if (set->channels == 1u && set->mask) {
        if (!(set->mask & 1u)) l = 0;
        if (!(set->mask & 2u)) r = 0;
    }

    *ml += l;
    *mr += r;
    st->phase += step;
}

void uaesnd_mix(struct uaesnd_card *c, uint8_t *dst, uint32_t frames)
{
    for (unsigned n = 0; n < UAESND_STREAMS; n++) {
        if (!(c->s[n].status & 1u)) continue;
        set_to_regs(&c->s[n]);
        c->s[n].cur.frequency = r_be32(uaesnd_stream_regs(n) + 12);
        c->s[n].step = freq_to_step(c->s[n].cur.frequency);
    }

    for (unsigned n = 0; n < UAESND_STREAMS; n++) {
        struct uaesnd_stream *st = &c->s[n];
        if (!(st->status & 2u)) continue;                 /* allocated?        */
        uint32_t tf = r_be32(uaesnd_stream_regs(n) + 0x94);
        if (tf == 0u) continue;                           /* 0 = disabled      */
        if (tf < 256u) tf *= 256u;
        uint32_t tstep = freq_to_step(tf);                /* Hz*256 form       */
        for (uint32_t i = 0; i < frames; i++) {
            st->tacc += tstep;
            if (st->tacc >= 0x10000u) {
                st->tacc -= 0x10000u;
                raise_int(c, n, 0x10u);
            }
        }
    }

    for (uint32_t i = 0; i < frames; i++) {
        int32_t ml = 0, mr = 0;
        for (unsigned n = 0; n < UAESND_STREAMS; n++)
            stream_frame(c, n, &ml, &mr);
        if (ml >  32767) ml =  32767; else if (ml < -32768) ml = -32768;
        if (mr >  32767) mr =  32767; else if (mr < -32768) mr = -32768;
        dst[0] = (uint8_t)((uint16_t)ml & 0xFFu);      /* S16LE, as the sink wants */
        dst[1] = (uint8_t)(((uint16_t)ml >> 8) & 0xFFu);
        dst[2] = (uint8_t)((uint16_t)mr & 0xFFu);
        dst[3] = (uint8_t)(((uint16_t)mr >> 8) & 0xFFu);
        dst += 4;
    }
}

static void stg_ev_push(struct uaesnd_card *c, unsigned n, uint8_t causes);
static uint8_t stg_defer_for;
static void raise_int_now(struct uaesnd_card *c, unsigned n, uint8_t bit);
static void raise_int(struct uaesnd_card *c, unsigned n, uint8_t bit)
{
    if (stg_defer_for == (uint8_t)(n + 1u)) {
        stg_ev_push(c, n, bit);
        return;
    }
    raise_int_now(c, n, bit);
}
static void raise_int_now(struct uaesnd_card *c, unsigned n, uint8_t bit)
{
    struct uaesnd_stream *st = &c->s[n];
    st->intreq |= bit;
    if (st->intena & bit) {
        st->intreq |= 0x80u;
        c->intreq_mask |= (1u << n);
    }
    uaesnd_stream_regs(n)[0x87] = st->intreq;
    uaesnd_stream_regs(n)[0x8B] = st->intreq;
}

void uaesnd_set_field(struct uaesnd_card *c, unsigned n, uint32_t so)
{
    struct uaesnd_stream *st = &c->s[n];
    const uint8_t *r = uaesnd_stream_regs(n);
    if (so == 0x0Cu) {
        st->cur.frequency = r_be32(r + 12);
        /* the mix loop re-derives the step from the register each batch */
        stg_on_param(c, n);
    }
    if (so == 0x18u) {
        st->cur.volume = r_be16(r + 26);
        stg_on_param(c, n);
    }
}

#if defined(EMU68_UAESND) && defined(UAESND_STG_MAGIC)

static inline int stream_will_repeat(const struct uaesnd_stream *st)
{
    const struct uaesnd_set *set = &st->cur;
    if (!st->repeating)
        return st->reps_left != 0u && set->repeat_pointer && set->repeat_length != 0u;
    return st->reps_left != 0u;
}

uint32_t g_uaesnd_staging;      /* 1 = negotiated: stage for core#0; 0 = legacy */

uint32_t uaesnd_exter_line(void)
{
    return g_uaesnd.intreq_mask & g_uaesnd.intena_mask;
}
volatile uint32_t g_uaesnd_stage_in_trap;

static volatile uint32_t *const stg_ctl  = (volatile uint32_t *)UAESND_CTRL_DDR;
static uint8_t           *const stg_ring = (uint8_t *)UAESND_RING_DDR;

#define STG_BURST_BYTES  256u
#define STG_LEAD_MS      64u
#define STG_LEAD_FLOOR   512u
#define STG_MAXDELTA     9600u

static struct {
    uint32_t prod;
    uint32_t epoch;         /* mirrors S_EPOCH                                 */
    uint32_t fmt, step, vol;/* last published params (change detection)        */
    uint32_t lead;          /* staging lead target for this stream, in bytes   */
    uint8_t  flags;         /* mirrors S_FLAGS bit0 (ACTIVE)                   */
    uint8_t  nofmt;         /* current set is inexpressible in S_FMT           */
    struct { uint32_t off; uint8_t causes; } evq[8];
    uint8_t  evh, evn;      /* head index, count (fire-oldest on overflow)     */
    uint8_t  hold;
    uint32_t hold_at;
} stg[UAESND_STREAMS];
#define STG_HOLD_SLACK_FRAMES  2u
static uint32_t stg_last_out_frames;    /* last STG_CONS_FRAMES seen (timer)   */
static uint32_t stg_last_state;         /* last sticky value published         */
static uint8_t  stg_chain;              /* inside advance()'s chain into start */

static void stg_wr(uint32_t off, uint32_t val)
{
    stg_ctl[off / 4u] = val;
}

static inline uint32_t stg_desc(unsigned n)
{
    return UAESND_STG_DESC_BASE + n * UAESND_STG_DESC_STRIDE;
}

static void stg_ev_push(struct uaesnd_card *c, unsigned n, uint8_t causes)
{
    if (stg[n].evn == 8u) {
        uint8_t oldest = stg[n].evq[stg[n].evh].causes;
        stg[n].evh = (uint8_t)((stg[n].evh + 1u) & 7u);
        stg[n].evn--;
        raise_int_now(c, n, oldest);
    }
    unsigned t = (stg[n].evh + stg[n].evn) & 7u;
    stg[n].evq[t].off    = stg[n].prod;
    stg[n].evq[t].causes = causes;
    stg[n].evn++;
}

static void stg_ev_fire_upto(struct uaesnd_card *c, unsigned n, uint32_t limit)
{
    while (stg[n].evn) {
        struct { uint32_t off; uint8_t causes; } *e = (void *)&stg[n].evq[stg[n].evh];
        if ((int32_t)(limit - e->off) < 0)
            break;
        uint8_t causes = e->causes;
        stg[n].evh = (uint8_t)((stg[n].evh + 1u) & 7u);
        stg[n].evn--;
        raise_int_now(c, n, causes);
    }
}

static void stg_ev_fire_due(struct uaesnd_card *c, unsigned n, int flush_all)
{
    uint32_t cons = stg_ctl[(stg_desc(n) + UAESND_STG_S_CONS) / 4u];
    while (stg[n].evn) {
        struct { uint32_t off; uint8_t causes; } *e = (void *)&stg[n].evq[stg[n].evh];
        if (!flush_all && (int32_t)(cons - e->off) < 0)
            break;
        uint8_t causes = e->causes;
        stg[n].evh = (uint8_t)((stg[n].evh + 1u) & 7u);
        stg[n].evn--;
        raise_int_now(c, n, causes);
    }
}

static inline unsigned stg_out_frame_bytes(uint32_t fmt)
{
    return ((fmt & 1u) ? 2u : 1u) * ((fmt & 2u) ? 2u : 1u);
}

static int stg_fmt_of(const struct uaesnd_set *s, uint32_t *fmt)
{
    if (s->channels == 0u || s->channels > 8u)
        return 0;
    *fmt = (((s->type & 7u) != 0u) ? 1u : 0u) | (s->channels >= 2u ? 2u : 0u);
    return 1;
}

static uint32_t stg_vol_of(const struct uaesnd_set *s)
{
    uint32_t v8 = (s->volume >= 32640u) ? 255u : ((uint32_t)s->volume >> 7);
    uint32_t l = v8, r = v8;
    if (s->channels == 1u && s->mask) {
        if (!(s->mask & 1u)) l = 0u;
        if (!(s->mask & 2u)) r = 0u;
    }
    return (r << 16) | l;
}

static uint32_t stg_lead_of(const struct uaesnd_set *s, unsigned fb)
{
    uint32_t f = s->frequency, hz;
    if ((f >> 16) == 0xFFFFu) {
        uint32_t period = f & 0xFFFFu;
        hz = period ? 3546895u / period : 0u;      /* PAL colour clock */
    } else {
        hz = f >> 8;                               /* Hz*256 form      */
    }
    uint32_t lead = (hz * fb / 1000u) * STG_LEAD_MS;
    if (lead < STG_LEAD_FLOOR) lead = STG_LEAD_FLOOR;
    if (lead > UAESND_STG_STRIDE) lead = UAESND_STG_STRIDE;
    return lead;
}

static void stg_bump(unsigned n, uint32_t base)
{
    stg_wr(stg_desc(n) + UAESND_STG_S_EPOCH_BASE, base);
    stg_wr(stg_desc(n) + UAESND_STG_S_EPOCH, ++stg[n].epoch);
}

static int stg_set_flags(struct uaesnd_card *c, unsigned n, int from_start)
{
    struct uaesnd_stream *st = &c->s[n];
    uint8_t active = ((st->status & 1u) && (c->enable_mask & (1u << n))
                      && !stg[n].nofmt) ? 1u : 0u;
    if (active == stg[n].flags)
        return 0;
    stg[n].flags = active;
    stg_wr(stg_desc(n) + UAESND_STG_S_FLAGS, active);
    if ((active && from_start) || (!active && !(st->status & 1u))) {
        stg_ev_fire_due(c, n, 1);
        stg_bump(n, stg[n].prod);
    } else {
        stg_bump(n, stg_ctl[(stg_desc(n) + UAESND_STG_S_CONS) / 4u]);
    }
    return 1;
}

static int stg_publish_params(struct uaesnd_card *c, unsigned n)
{
    struct uaesnd_stream *st = &c->s[n];
    uint32_t fmt;
    if (!stg_fmt_of(&st->cur, &fmt)) {
        if (!stg[n].nofmt) {
            stg[n].nofmt = 1u;
            g_uaesnd_sticky |= UAESND_ST_STG_NOFMT;
        }
        return 0;
    }
    stg[n].nofmt = 0u;
    uint32_t step  = freq_to_step(r_be32(uaesnd_stream_regs(n) + 12));
    uint32_t vol   = stg_vol_of(&st->cur);
    stg[n].lead    = stg_lead_of(&st->cur, stg_out_frame_bytes(fmt));
    if (fmt == stg[n].fmt && step == stg[n].step && vol == stg[n].vol)
        return 0;
    stg[n].fmt = fmt; stg[n].step = step; stg[n].vol = vol;
    stg_wr(stg_desc(n) + UAESND_STG_S_FMT,  fmt);
    stg_wr(stg_desc(n) + UAESND_STG_S_STEP, step);
    stg_wr(stg_desc(n) + UAESND_STG_S_VOL,  vol);
    return 1;
}

static void stg_on_start(struct uaesnd_card *c, unsigned n)
{
    if (!g_uaesnd_staging)
        return;
    stg[n].hold = 0u;
    int chg    = stg_publish_params(c, n);
    int bumped = stg_set_flags(c, n, 1);    /* an activation already fences */
    if (bumped || (stg_chain && !chg && stg[n].flags))
        return;
    stg_bump(n, stg[n].prod);
}

static void stg_on_param(struct uaesnd_card *c, unsigned n)
{
    if (!g_uaesnd_staging)
        return;
    if (!stg_publish_params(c, n))
        return;                          /* no effective change: no fence */
    stg_bump(n, stg_ctl[(stg_desc(n) + UAESND_STG_S_CONS) / 4u]);
}

static void stg_timer(struct uaesnd_card *c)
{
    uint32_t total = stg_ctl[UAESND_STG_CONS_FRAMES / 4u];
    uint32_t d = total - stg_last_out_frames;
    stg_last_out_frames = total;
    if (d == 0u)
        return;
    if (d > STG_MAXDELTA)
        d = (d >= 0x80000000u) ? 0u : STG_MAXDELTA;
    if (d == 0u)
        return;
    for (unsigned n = 0; n < UAESND_STREAMS; n++) {
        struct uaesnd_stream *st = &c->s[n];
        if (!(st->status & 2u))
            continue;                            /* allocated?               */
        uint32_t tf = r_be32(uaesnd_stream_regs(n) + 0x94);
        if (tf == 0u)
            continue;                            /* 0 = disabled             */
        if (tf < 256u) tf *= 256u;               /* plain-Hz rule, as legacy */
        static uint32_t tf_last[UAESND_STREAMS], ts_cache[UAESND_STREAMS];
        if (tf != tf_last[n]) { tf_last[n] = tf; ts_cache[n] = freq_to_step(tf); }
        uint64_t acc = (uint64_t)st->tacc + (uint64_t)ts_cache[n] * d;
        if (acc >= 0x10000u)
            raise_int(c, n, 0x10u);
        st->tacc = (uint32_t)(acc & 0xFFFFu);
    }
}

struct stg_cv {
    unsigned le, bias, sb, keep, fbs, fbo;
};
static struct stg_cv stg_cv_of(const struct uaesnd_set *s)
{
    struct stg_cv cv;
    unsigned ch = s->channels ? s->channels : 1u;
    cv.le   = (s->type & 0x80u) != 0u;
    cv.bias = (s->type & 0x40u) != 0u;
    cv.sb   = sample_bytes(s->type);
    cv.keep = (ch >= 2u) ? 2u : 1u;
    cv.fbs  = cv.sb * ch;                       /* source bytes per frame */
    cv.fbo  = cv.keep * ((s->type & 7u) ? 2u : 1u);
    return cv;
}

static int stg_cross_boundary(struct uaesnd_card *c, unsigned n)
{
    struct uaesnd_stream *st = &c->s[n];

    stg_chain = 1u;
    {
        stg_defer_for = (uint8_t)(n + 1u);
        uaesnd_stream_advance(c, n);
        stg_defer_for = 0u;
    }
    stg_chain = 0u;
    if (!(st->status & 1u) || stg[n].nofmt) {
        stg[n].flags = 0u;
        stg_wr(stg_desc(n) + UAESND_STG_S_FLAGS, 0u);
        stg_bump(n, stg_ctl[(stg_desc(n) + UAESND_STG_S_CONS) / 4u]);
        return 0;
    }
    return 1;
}

static void stg_stream_tick(struct uaesnd_card *c, unsigned n)
{
    struct uaesnd_stream *st = &c->s[n];
    struct uaesnd_set    *set = &st->cur;

    stg_set_flags(c, n, 0);
    stg_ev_fire_due(c, n, 0);
    if (!stg[n].flags)
        return;

    if (g_uaesnd_stage_in_trap && g_is_chip(set->pointer + st->pos))
        return;     /* trap-context lifeline: no seam traffic from a storm */
    uint32_t cons = stg_ctl[(stg_desc(n) + UAESND_STG_S_CONS) / 4u];
    uint32_t inflight = stg[n].prod - cons;
    if (inflight > UAESND_STG_STRIDE) {
        g_uaesnd_sticky |= UAESND_ST_STG_CONSBAD;
        return;
    }
    if (stg[n].hold) {
        if (inflight > STG_HOLD_SLACK_FRAMES * stg_out_frame_bytes(stg[n].fmt))
            return;
        if (g_uaesnd_stage_in_trap && g_is_chip(set->next))
            return;     /* the set fetch is seam traffic too (lifeline law) */
        stg[n].hold = 0u;
        int more = stg_cross_boundary(c, n);
        stg_ev_fire_upto(c, n, stg[n].hold_at);
        if (!more)
            return;
        if (g_uaesnd_stage_in_trap && g_is_chip(set->pointer + st->pos))
            return;
    }
    uint32_t room = (stg[n].lead > inflight) ? stg[n].lead - inflight : 0u;
    if (room > STG_BURST_BYTES)
        room = STG_BURST_BYTES;
    struct stg_cv cv = stg_cv_of(set);
    uint8_t *r0 = stg_ring + n * UAESND_STG_STRIDE;
    int staged = 0;

    while (room >= cv.fbo) {
        if (set->length == 0u)
            break;              /* zero-length set: hold, exactly as the mixer */
        for (unsigned k = 0; k < cv.keep; k++) {
            uint32_t src = set->pointer + st->pos + k * cv.sb;
            if (cv.sb == 1u) {
                uint8_t b = g_rd8(src);
                if (cv.bias) b ^= 0x80u;
                r0[stg[n].prod & (UAESND_STG_STRIDE - 1u)] = b;
                stg[n].prod += 1u;
            } else {
                uint8_t hi = g_rd8(src + (cv.le ? cv.sb - 1u : 0u));
                uint8_t lo = g_rd8(src + (cv.le ? cv.sb - 2u : 1u));
                if (cv.bias) hi ^= 0x80u;
                r0[(stg[n].prod)      & (UAESND_STG_STRIDE - 1u)] = hi;
                r0[(stg[n].prod + 1u) & (UAESND_STG_STRIDE - 1u)] = lo;
                stg[n].prod += 2u;
            }
        }
        st->pos += cv.fbs;
        room    -= cv.fbo;
        staged = 1;
        if (--set->length == 0u) {
            stg_wr(stg_desc(n) + UAESND_STG_S_PROD, stg[n].prod);
            if (!stream_will_repeat(st) && set->next != 0u) {
                stg[n].hold    = 1u;
                stg[n].hold_at = stg[n].prod;
                break;
            }
            if (!stg_cross_boundary(c, n))
                break;
            cv = stg_cv_of(set);
        }
    }

    if (staged) {
        stg_wr(stg_desc(n) + UAESND_STG_S_PROD, stg[n].prod);
        set_to_regs(st);
    }
}

void uaesnd_stage_tick(struct uaesnd_card *c)
{
    for (unsigned n = 0; n < UAESND_STREAMS; n++)
        stg_stream_tick(c, n);
    stg_timer(c);
    if (g_uaesnd_sticky != stg_last_state) {
        stg_last_state = g_uaesnd_sticky;
        stg_wr(UAESND_CTRL_STATE, g_uaesnd_sticky);
    }
}

void uaesnd_stage_fire_due_all(struct uaesnd_card *c)
{
    for (unsigned n = 0; n < UAESND_STREAMS; n++)
        if (stg[n].evn)
            stg_ev_fire_due(c, n, 0);
}

void uaesnd_stage_negotiate(void)
{
    g_uaesnd_staging =
        (stg_ctl[UAESND_STG_MAIN_CAP / 4u] == UAESND_STG_MAGIC) ? 1u : 0u;
    stg_wr(UAESND_STG_FW_ON, g_uaesnd_staging);
    if (!g_uaesnd_staging)
        return;
    g_uaesnd_sticky |= UAESND_ST_STAGING;
    stg_last_out_frames = stg_ctl[UAESND_STG_CONS_FRAMES / 4u];
    stg_chain = 0u;
    for (unsigned n = 0; n < UAESND_STREAMS; n++) {
        stg[n].prod = stg[n].epoch = 0u;
        stg[n].fmt = stg[n].step = stg[n].vol = 0u;
        stg[n].lead = STG_LEAD_FLOOR;
        stg[n].flags = 0u;
        stg[n].nofmt = 0u;
        stg[n].evh = stg[n].evn = 0u;
        stg[n].hold = 0u;               /* ...and so does a held boundary */
        stg[n].hold_at = 0u;
        for (uint32_t w = 0u; w < UAESND_STG_DESC_STRIDE; w += 4u)
            stg_wr(stg_desc(n) + w, 0u);
    }
}

void uaesnd_stage_off(void)
{
    g_uaesnd_staging = 0u;
    stg_wr(UAESND_STG_FW_ON, 0u);
}

#else  /* !(EMU68_UAESND && UAESND_STG_MAGIC) */

void uaesnd_stage_negotiate(void) { }
void uaesnd_stage_fire_due_all(struct uaesnd_card *c) { (void)c; }
static void stg_ev_push(struct uaesnd_card *c, unsigned n, uint8_t causes)
{ (void)c; (void)n; (void)causes; }
void uaesnd_stage_off(void)       { }
static void stg_on_start(struct uaesnd_card *c, unsigned n)
{ (void)c; (void)n; }
static void stg_on_param(struct uaesnd_card *c, unsigned n)
{ (void)c; (void)n; }
static int stg_set_flags(struct uaesnd_card *c, unsigned n, int from_start)
{ (void)c; (void)n; (void)from_start; return 0; }

#endif

/* ---- starting, repeating and chaining ------------------------------------ */

extern uint32_t g_uaesnd_sticky;      /* uaesnd_ring.c publishes it */
extern void     uaesnd_card_playing(int on);

static void refresh_playing(struct uaesnd_card *c)
{
    for (unsigned i = 0; i < UAESND_STREAMS; i++)
        if (c->s[i].status & 1u) { uaesnd_card_playing(1); return; }
    uaesnd_card_playing(0);
}

/* frequency field: Hz * 256, or a Paula period when the high word is $FFFF */
static uint32_t freq_to_step(uint32_t f)
{
    uint32_t hz256;
    if ((f >> 16) == 0xFFFFu) {
        uint32_t period = f & 0xFFFFu;
        if (period == 0u) return 0u;
        hz256 = (3546895u * 256u) / period;      /* PAL colour clock            */
    } else {
        hz256 = f;
    }
    uint32_t st = (hz256 * 2u) / 375u;
    return (st == 0u && hz256 != 0u) ? 1u : st;
}

static int set_is_sane(const struct uaesnd_set *s)
{
    if (s->flags != 0u || s->format != 0u) return 0;
    if (s->channels == 0u || s->channels > 8u) return 0;
    if ((s->type & 7u) == 2u) return 0;         /* 24-bit: the driver never asks */
    if (s->pointer == 0u) return 0;
    if (s->volume > 32768u) return 0;
    return 1;
}

void uaesnd_stream_start(struct uaesnd_card *c, unsigned n, uint32_t set_addr)
{
    struct uaesnd_stream *st = &c->s[n];
    uaesnd_fetch_set(&st->cur, set_addr);
    if (!set_is_sane(&st->cur)) {
        g_uaesnd_sticky |= UAESND_ST_BADSET;
        st->status &= ~1u;
        uaesnd_stream_regs(n)[0x93] = st->status;
        stg_set_flags(c, n, 0);
        refresh_playing(c);
        return;
    }
    {
        uint8_t *r = uaesnd_stream_regs(n);
        for (unsigned k = 0; k < 40u; k++) r[k] = g_rd8(set_addr + k);
    }
    st->idx       = (uint8_t)n;
    st->set_ptr   = set_addr;
    st->pos       = 0;
    {
        uint8_t *r = uaesnd_stream_regs(n);
        w_be32(r + 0x80, set_addr);              /* $180: current set pointer  */
        w_be32(r + 0x28, st->cur.pointer);       /* $128: ORIGINAL address     */
        w_be32(r + 0x2C, st->cur.length);        /* $12C: ORIGINAL length      */
    }
    st->phase     = 0x10000u;              /* force a fetch on the first frame  */
    st->step      = freq_to_step(st->cur.frequency);
    st->repeating = 0;
    st->reps_left = st->cur.repeat_count;
    st->pl = st->pr = st->cl = st->cr = 0;
    st->status |= 1u;
    uaesnd_stream_regs(n)[0x93] = st->status;

    g_uaesnd_sticky |= g_is_chip(st->cur.pointer) ? UAESND_ST_SRC_CHIP
                                                  : UAESND_ST_SRC_FAST;
    g_uaesnd_sticky |= UAESND_ST_PLAYING;

    raise_int(c, n, 0x01u);
    refresh_playing(c);
    stg_on_start(c, n);
}

void uaesnd_stream_advance(struct uaesnd_card *c, unsigned n)
{
    struct uaesnd_stream *st = &c->s[n];
    struct uaesnd_set    *set = &st->cur;

    if (!st->repeating && st->reps_left != 0u && set->repeat_pointer
        && set->repeat_length != 0u) {
        st->repeating = 1;
        set->pointer  = set->repeat_pointer;
        set->length   = set->repeat_length;
        st->pos       = 0;
        if (st->reps_left != 0xFFFFu) st->reps_left--;
        st->status |= 4u;
        uaesnd_stream_regs(n)[0x93] = st->status;
        raise_int(c, n, 0x04u);              /* repeat START */
        return;
    }
    if (st->repeating && st->reps_left != 0u) {
        set->pointer = set->repeat_pointer;
        set->length  = set->repeat_length;
        st->pos      = 0;
        if (st->reps_left != 0xFFFFu) st->reps_left--;
        return;
    }

    /* set finished: chain, or stop */
    st->status &= ~(1u | 4u);
    uaesnd_stream_regs(n)[0x93] = st->status;
    st->repeating = 0;
    raise_int(c, n, 0x02u);                  /* set END: status always          */

    if (set->next) {
        uaesnd_stream_start(c, n, set->next);
    } else {
        st->set_ptr = 0;                   /* "clears automatically when it ends" */
        refresh_playing(c);
    }
}
