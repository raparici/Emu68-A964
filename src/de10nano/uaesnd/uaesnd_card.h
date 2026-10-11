/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: GPL-3.0-or-later
*/

/* uaesnd: the card model (WinUAE's sound board, as the uaesnd AHI driver expects it). */

#ifndef UAESND_CARD_H
#define UAESND_CARD_H

#include <stdint.h>

#define UAESND_MANUF        0x1966u     /* 6502 decimal - the driver's FindConfigDev */
#define UAESND_PRODUCT      0x02u
#define UAESND_STREAMS      8u
#define UAESND_MAX_CHANS    2u          /* stereo out; 5.1 is a later question       */

/* the 40-byte sample set the guest builds in its own memory (spec 0..39) */
struct uaesnd_set {
    uint16_t flags, format;
    uint32_t pointer, length, frequency;      /* frequency is Hz * 256 */
    uint32_t repeat_pointer, repeat_length;
    uint16_t repeat_count, volume;
    uint32_t next;
    uint8_t  channels, type, intena, mask;
    uint16_t hpan, dpan;
    uint32_t orig_pointer, orig_length;
};

struct uaesnd_stream {
    struct uaesnd_set cur, latched;
    uint32_t set_ptr;          /* $180: current sample set pointer                */
    uint32_t timer_freq;       /* $194                                            */
    uint8_t  intreq;           /* $187 (read clears)                              */
    uint8_t  intena;           /* $18F                                            */
    uint8_t  status;           /* $193: b0 play active, b1 allocated, b2 repeating */
    /* playback cursor, ours */
    uint32_t pos;              /* byte offset into the current sample             */
    uint32_t phase;            /* 16.16 resample accumulator                      */
    uint32_t step;             /* 16.16 source frames per output frame            */
    int16_t  pl, pr, cl, cr;   /* the two frames the interpolation sits between   */
    uint8_t  repeating;        /* inside the repeat section                       */
    uint8_t  idx;              /* which stream this is, for the register block     */
    uint16_t reps_left;        /* repeat_count countdown, 0xFFFF = forever        */
    uint32_t tacc;             /* 16.16 timer accumulator ($194)                  */
};

struct uaesnd_card {
    uint32_t base;             /* guest address of the register window, 0 = unmapped */
    uint32_t alloc_mask;       /* $E0 */
    uint32_t enable_mask;      /* $F0 */
    uint32_t intena_mask;      /* $F4 */
    uint32_t intreq_mask;      /* $F8 */
    struct uaesnd_stream s[UAESND_STREAMS];
};

extern struct uaesnd_card g_uaesnd;

void     uaesnd_card_init (struct uaesnd_card *c);
uint32_t uaesnd_card_read (struct uaesnd_card *c, uint32_t off, unsigned size);
void     uaesnd_fetch_set     (struct uaesnd_set *s, uint32_t addr);
void     uaesnd_mix           (struct uaesnd_card *c, uint8_t *dst, uint32_t frames);
void     uaesnd_stream_advance(struct uaesnd_card *c, unsigned n);
void     uaesnd_stream_start  (struct uaesnd_card *c, unsigned n, uint32_t set_addr);
void     uaesnd_card_write(struct uaesnd_card *c, uint32_t off, uint32_t val, unsigned size);

#endif /* UAESND_CARD_H */
