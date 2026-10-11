/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: GPL-3.0-or-later
*/

/* uaesnd: the card's register file. */

#include <stdint.h>
#include "uaesnd_card.h"
#include "uaesnd_ring.h"

struct uaesnd_card g_uaesnd;

#define UAESND_UAE_VERSION  0x00010000u   /* 1.0.0 - BigMig's uaesnd            */
#define UAESND_HW_VERSION   4u
#define UAESND_HW_REVISION  1u

#define UAS_REGS_SIZE   (0x100u + UAESND_STREAMS * 0x100u)
static uint8_t uas_regs[UAS_REGS_SIZE];

uint8_t *uaesnd_stream_regs(unsigned n)
{
    return &uas_regs[0x100u + n * 0x100u];
}

static inline void wr8 (uint32_t o, uint8_t v)  { uas_regs[o] = v; }
static inline void wr16(uint32_t o, uint16_t v) { uas_regs[o] = (uint8_t)(v >> 8);
                                                  uas_regs[o+1] = (uint8_t)v; }
static inline void wr32(uint32_t o, uint32_t v) { wr16(o, (uint16_t)(v >> 16));
                                                  wr16(o + 2, (uint16_t)v); }
static inline uint32_t rd32(uint32_t o)
{
    return ((uint32_t)uas_regs[o] << 24) | ((uint32_t)uas_regs[o+1] << 16) |
           ((uint32_t)uas_regs[o+2] << 8) | (uint32_t)uas_regs[o+3];
}

/* Fill the read-only identity block.  Called from the board's map(). */
void uaesnd_card_init(struct uaesnd_card *c)
{
    for (unsigned i = 0; i < UAS_REGS_SIZE; i++) uas_regs[i] = 0;
    wr32(0x80, UAESND_UAE_VERSION);
    wr16(0x84, UAESND_HW_VERSION);
    wr16(0x86, UAESND_HW_REVISION);
    wr32(0x88, UAESND_OUT_RATE);
    wr8 (0x8C, UAESND_MAX_CHANS);       /* max channels per stream             */
    wr8 (0x8D, UAESND_MAX_CHANS);       /* active channels                     */
    wr8 (0x8E, UAESND_STREAMS);
    wr32(0x90, 0);                      /* no board RAM (the driver never      */
    wr32(0x94, 0);
    c->alloc_mask = c->enable_mask = c->intena_mask = c->intreq_mask = 0;
    for (unsigned i = 0; i < UAESND_STREAMS; i++) {
        static const struct uaesnd_stream idle_stream;
        c->s[i] = idle_stream;
        c->s[i].idx = (uint8_t)i;
    }
    { extern void uaesnd_stage_negotiate(void); uaesnd_stage_negotiate(); }
}

uint32_t uaesnd_card_read(struct uaesnd_card *c, uint32_t off, unsigned size)
{
    if ((size == 2 && (off & 1u)) || (size == 4 && (off & 3u)))
        return (size == 2) ? 0xffffu : 0xffffffffu;
    if (off + size > UAS_REGS_SIZE)
        return 0;

    /* live fields that are not simply stored */
    if (off >= 0x100u) {
        unsigned n = (off - 0x100u) >> 8;
        uint32_t so = off & 0xFFu;
        if (so == 0x87 && size == 1) {          /* reading clears (spec)        */
            uint8_t v = c->s[n].intreq;
            c->s[n].intreq = 0;
            wr8(off, 0);
            c->intreq_mask &= ~(1u << n);
            return v;
        }
        if (so == 0x8B && size == 1) return c->s[n].intreq;   /* does not clear */
    } else {
        switch (off) {
        case 0xE0: return c->alloc_mask;
        case 0xF0: return c->enable_mask;
        case 0xF4: return c->intena_mask;
        case 0xF8: return c->intreq_mask;
        default: break;
        }
    }

    switch (size) {
    case 1:  return uas_regs[off];
    case 2:  return ((uint32_t)uas_regs[off] << 8) | uas_regs[off + 1];
    default: return rd32(off);
    }
}

void uaesnd_card_write(struct uaesnd_card *c, uint32_t off, uint32_t val, unsigned size)
{
    if ((size == 2 && (off & 1u)) || (size == 4 && (off & 3u)))
        return;                              /* unaligned write is ignored (spec) */
    if (off + size > UAS_REGS_SIZE)
        return;

    /* the identity block is read-only; everything from $E0 up is control */
    if (off >= 0x80u && off < 0xE0u)
        return;

    switch (size) {
    case 1:  wr8 (off, (uint8_t)val);  break;
    case 2:  wr16(off, (uint16_t)val); break;
    default: wr32(off, val);           break;
    }

    if (off >= 0x100u) {
        unsigned n = (off - 0x100u) >> 8;
        uint32_t so = off & 0xFFu;
        struct uaesnd_stream *st = &c->s[n];
        if (so < 0x40u) {
            extern void uaesnd_set_field(struct uaesnd_card *c, unsigned n, uint32_t so);
            switch (size) {
            case 1:  wr8 (off, (uint8_t)val);  break;
            case 2:  wr16(off, (uint16_t)val); break;
            default: wr32(off, val);           break;
            }
            if ((so & ~3u) == 0x0Cu || (so & ~3u) == 0x18u)
                uaesnd_set_field(c, n, so & ~3u);
            return;
        }
        if (so == 0x80 && size == 4) {          /* current sample set pointer    */
            if (val == 0) {                     /* zero stops immediately (spec) */
                st->set_ptr = 0;
                st->status &= ~1u;
            } else if (c->enable_mask & (1u << n)) {
                uaesnd_stream_start(c, n, val); /* reads the set from guest RAM  */
            } else {
                st->set_ptr = val;              /* armed, waits for the enable   */
            }
            wr8(0x100u + n*0x100u + 0x93, st->status);
        } else if (so == 0xA0 && size == 4) {   /* force load                    */
            uaesnd_stream_start(c, n, val);
            wr8(0x100u + n*0x100u + 0x93, st->status);
        } else if (so == 0x93 && size == 1) {
            if (st->set_ptr && !(st->status & (1u | 4u)))
                uaesnd_stream_start(c, n, st->set_ptr);
            wr8(0x100u + n*0x100u + 0x93, st->status);
        } else if (so == 0x8F && size == 1) {
            st->intena = (uint8_t)val;
        } else if (so >= 0x94u && so < 0x98u) {
            st->timer_freq = rd32(0x100u + n*0x100u + 0x94);
            st->tacc = 0u;
            if (st->timer_freq) {
                extern void uaesnd_card_playing(int on);
                uaesnd_card_playing(1);
            }
        }
        return;
    }

    switch (off) {
    case 0xE0: case 0xE3: {
        uint32_t had = c->alloc_mask;
        c->alloc_mask = (off == 0xE0) ? val
                      : ((c->alloc_mask & 0xffffff00u) | (val & 0xffu));
        for (unsigned i = 0; i < UAESND_STREAMS; i++) {
            if ((had & ~c->alloc_mask) & (1u << i)) {
                c->s[i].status   &= ~(1u | 4u);
                c->s[i].repeating = 0;
                c->s[i].set_ptr   = 0;
                c->s[i].intreq    = 0;
                wr8(0x100u + i*0x100u + 0x87, 0);
                wr8(0x100u + i*0x100u + 0x8B, 0);
                c->intreq_mask &= ~(1u << i);
            }
            if (c->alloc_mask & (1u << i)) c->s[i].status |= 2u;
            else                           c->s[i].status &= ~2u;
            wr8(0x100u + i*0x100u + 0x93, c->s[i].status);
        }
        break;
    }
    case 0xE4:                                   /* latch   */
        for (unsigned i = 0; i < UAESND_STREAMS; i++)
            if (val & (1u << i))
                for (unsigned k = 0; k < 0x40u; k++)
                    uas_regs[0x100u + i*0x100u + 0x40u + k] = uas_regs[0x100u + i*0x100u + k];
        break;
    case 0xE8:                                   /* unlatch */
        for (unsigned i = 0; i < UAESND_STREAMS; i++)
            if (val & (1u << i))
                for (unsigned k = 0; k < 0x40u; k++)
                    uas_regs[0x100u + i*0x100u + k] = uas_regs[0x100u + i*0x100u + 0x40u + k];
        break;
    case 0xF0: {
        uint32_t was = c->enable_mask;
        c->enable_mask = val;
        /* "Changing stream mask always clears stream's interrupt status" */
        for (unsigned i = 0; i < UAESND_STREAMS; i++) {
            c->s[i].intreq = 0;
            wr8(0x100u + i*0x100u + 0x87, 0);
            if ((was & ~val) & (1u << i)) {
                if (c->s[i].status & 1u)
                    c->s[i].set_ptr = 0;
                c->s[i].status   &= ~(1u | 4u);
                c->s[i].repeating = 0;
                wr8(0x100u + i*0x100u + 0x93, c->s[i].status);
            }
            if ((val & ~was & (1u << i)) && c->s[i].set_ptr && !(c->s[i].status & 1u)) {
                uaesnd_stream_start(c, i, c->s[i].set_ptr);
                wr8(0x100u + i*0x100u + 0x93, c->s[i].status);
            }
        }
        c->intreq_mask = 0;
        break;
    }
    case 0xF4: c->intena_mask = val; break;
    default:   break;
    }
}
