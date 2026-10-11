/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: MIT
*/

/* MHI: the decoder card's mailbox, stream and PCM path. */

#include <stdint.h>
#include <string.h>
#include "mhi_mailbox.h"
#include "mhi_fw.h"
#include "amiga_memmap.h"

#if defined(__arm__) && !defined(__APPLE__)
#define MHI_TAIL __attribute__((section(".b157_tail_text"), noinline))
#else
#define MHI_TAIL
#endif

/* ---- firmware seam / cache helpers (provided by the port) ------------------ */
extern unsigned ps_read_16(unsigned addr);      /* chip-RAM read via the seam   */

/* ---- tunables -------------------------------------------------------------- */
#define MHI_OUT_HZ      48000u       /* uaesnd mixes at 48 kHz (uaesnd_mix.c)    */
#define MHI_CHIP_LIMIT  0x00200000u  /* < this = chip RAM (seam); >= = Fast/DDR   */
#define MHI_STAGE_CHUNK 2048u        /* bytes copied from a descriptor per pass   */

/* ---- descriptor ring (guest buffer pointers + lengths) --------------------- */
struct mhi_desc { uint32_t ptr; uint32_t size; uint32_t off; };

/* ---- module state ---------------------------------------------------------- */
static struct {
    uint32_t state;                  /* MHI_FWSTATE_*                            */
    uint32_t arg0, arg1, arg2;       /* mailbox scratch                          */

    struct mhi_desc desc[MHI_DESC_RING];
    uint32_t d_head, d_tail, d_count;
    uint8_t  eof_seen;               /* zero-length QUEUE marker reached          */

    uint32_t src_prod;               /* SRC ring produce cursor, free-running     */
    uint32_t epoch;
    int      dec_hz, dec_ch;         /* decoded rate/channels (telemetry; HOST)   */

    uint32_t consumed;               /* cumulative guest bytes accepted           */
    uint32_t produced;               /* cumulative PCM frames produced (tlm; HOST)*/
    uint32_t underruns, errors;

    uint32_t vol_q;                  /* 0..256 master gain                        */
    uint32_t lg_q, rg_q;             /* 0..256 per-channel gain (pan folded in)   */
} M;

/* Defined in every build so the hook sites branch on a cached global. */
volatile uint32_t g_mhi_active = 0;

volatile uint32_t g_mhi_off = 0;

static volatile uint32_t *const mhi_ctl = (volatile uint32_t *)MHI_CTRL_DDR;
static uint8_t           *const mhi_src = (uint8_t *)MHI_SRC_RING_DDR;

/* ---- pan/volume -> per-channel integer gains ------------------------------- */
static void mhi_recompute_gains(void)
{
    /* pan in arg via MHI_CMD_SETPAN stored to M.arg (0..100, 50 = centre). */
    uint32_t vol = M.vol_q;                       /* 0..256 */
    uint32_t pan = M.arg2 > 100u ? 50u : M.arg2;  /* last SETPAN value, clamped */
    uint32_t lg = 256u, rg = 256u;
    if (pan < 50u)       rg = (pan * 256u) / 50u;         /* fade right down     */
    else if (pan > 50u)  lg = ((100u - pan) * 256u) / 50u;/* fade left down      */
    M.lg_q = (vol * lg) >> 8;
    M.rg_q = (vol * rg) >> 8;
    mhi_ctl[MHI_CTRL_GAIN / 4] = (M.lg_q << 16) | (M.rg_q & 0xFFFFu);
}

/* ---- read compressed bytes DIRECT from guest DDR (chip RAM via the seam) ---- */
static void mhi_read_src(uint8_t *dst, uint32_t gaddr, uint32_t n)
{
    if (gaddr >= MHI_CHIP_LIMIT) {
        memcpy(dst, (const void *)(uintptr_t)gaddr, n);
        return;
    }
    uint32_t i = 0;
    if (gaddr & 1u) {                              /* odd start: one low byte    */
        unsigned w = ps_read_16(gaddr & ~1u);
        dst[i++] = (uint8_t)(w & 0xFFu);
        gaddr++;
    }
    while (i + 2u <= n) {
        unsigned w = ps_read_16(gaddr);
        dst[i++] = (uint8_t)(w >> 8);
        dst[i++] = (uint8_t)(w & 0xFFu);
        gaddr += 2u;
    }
    if (i < n) {                                   /* trailing high byte         */
        unsigned w = ps_read_16(gaddr);
        dst[i++] = (uint8_t)(w >> 8);
    }
}

static void mhi_session_reset(void)
{
    M.d_head = M.d_tail = M.d_count = 0;
    M.eof_seen = 0;
    M.consumed = M.produced = 0;
    M.dec_hz = 0; M.dec_ch = 0;
    M.epoch++;
    if (g_mhi_off)
        return;             /* disarmed it, and a guest reset must not arm it again             */
    mhi_ctl[MHI_CTRL_MAGIC   / 4] = MHI_CTRL_MAGIC_VAL;
    mhi_ctl[MHI_CTRL_EOF     / 4] = 0u;
    mhi_ctl[MHI_CTRL_SRC_PROD / 4] = M.src_prod;
    mhi_ctl[MHI_CTRL_PAUSED    / 4] = 0u;
    mhi_ctl[MHI_CTRL_EPOCH_SRC / 4] = M.src_prod;
    mhi_ctl[MHI_CTRL_FW_REV    / 4] = MHI_CTRL_FW_REV_VAL;
    asm volatile("dmb" ::: "memory");
    mhi_ctl[MHI_CTRL_EPOCH   / 4] = M.epoch;
}

static void mhi_set_paused(uint32_t on)
{
    if (!g_mhi_off)
        mhi_ctl[MHI_CTRL_PAUSED / 4] = on;
}

static uint32_t mhi_played_view(void)
{
    uint32_t e = M.epoch, played;
    if (mhi_ctl[MHI_CTRL_PLAYED_EPOCH / 4] != e) return 0u;
    asm volatile("dmb" ::: "memory");
    played = mhi_ctl[MHI_CTRL_PLAYED / 4];
    asm volatile("dmb" ::: "memory");
    if (mhi_ctl[MHI_CTRL_PLAYED_EPOCH / 4] != e) return 0u;
    return played;
}

static void mhi_set_state(uint32_t s)
{
    M.state = s;
    g_mhi_active = (s == MHI_FWSTATE_PLAYING) ? 1u : 0u;
}

static void mhi_queue(uint32_t ptr, uint32_t size)
{
    if (size == 0u) { M.eof_seen = 1; return; }
    if (M.d_count >= MHI_DESC_RING) return;        /* full: guest saw QFREE=0     */
    M.desc[M.d_tail].ptr  = ptr;
    M.desc[M.d_tail].size = size;
    M.desc[M.d_tail].off  = 0;
    M.d_tail = (M.d_tail + 1u) & (MHI_DESC_RING - 1u);
    M.d_count++;
    if (M.state == MHI_FWSTATE_OUT_OF_DATA)        /* new data auto-resumes       */
        mhi_set_state(MHI_FWSTATE_PLAYING);
}

static void mhi_stage_src(void)
{
    uint32_t src_cons = mhi_ctl[MHI_CTRL_SRC_CONS / 4];   /* Main's cursor        */
    uint32_t inflight = M.src_prod - src_cons;            /* free-running counters */
    uint32_t room = (inflight <= MHI_SRC_RING_BYTES)
                    ? (MHI_SRC_RING_BYTES - inflight) : 0u;

    if (M.d_count != 0u && room >= 2u) {
        struct mhi_desc *d = &M.desc[M.d_head];
        uint32_t avail = d->size - d->off;
        uint32_t n = avail < room ? avail : room;
        if (n > MHI_STAGE_CHUNK) n = MHI_STAGE_CHUNK;
        if (n != 0u) {
            uint32_t off   = M.src_prod & (MHI_SRC_RING_BYTES - 1u);
            uint32_t first = MHI_SRC_RING_BYTES - off;    /* wrap in two segments  */
            if (first > n) first = n;
            mhi_read_src(mhi_src + off, d->ptr + d->off, first);
            if (n > first)
                mhi_read_src(mhi_src, d->ptr + d->off + first, n - first);
            d->off     += n;
            M.src_prod += n;
            M.consumed += n;                              /* guest may free these  */
            if (d->off >= d->size) {                      /* descriptor exhausted  */
                M.d_head = (M.d_head + 1u) & (MHI_DESC_RING - 1u);
                M.d_count--;
            }
            mhi_ctl[MHI_CTRL_SRC_PROD / 4] = M.src_prod;  /* publish */
        }
    }

    uint32_t eof = (M.eof_seen && M.d_count == 0u) ? 1u : 0u;
    mhi_ctl[MHI_CTRL_EOF / 4] = eof;

    if (eof) {
        uint32_t src_c = mhi_ctl[MHI_CTRL_SRC_CONS / 4];
        uint32_t pcm_p = mhi_ctl[MHI_CTRL_PCM_PROD / 4];
        uint32_t pcm_c = mhi_ctl[MHI_CTRL_PCM_CONS / 4];
        if (src_c == M.src_prod && pcm_p == pcm_c)
            mhi_set_state(MHI_FWSTATE_OUT_OF_DATA);
    }
}

/* ============================ public entry points =========================== */

void mhi_fw_tick(void)
{
    if (!g_mhi_active) return;                      /* idle: one predicted branch  */
    if (M.state != MHI_FWSTATE_PLAYING) return;
    mhi_stage_src();     /* cheap copy guest DDR -> SRC ring; core#0 decodes it */
}

void mhi_fw_mix_frame(int32_t *l, int32_t *r)
{
    (void)l; (void)r;
}

MHI_TAIL uint32_t mhi_fw_mmio(uint32_t off, int is_load, unsigned size, uint32_t wval)
{
    (void)size;                                     /* guest uses move.l           */
    if (g_mhi_off)
        return 0;                                   /* load 0 (MAGIC too), every store gone  */
    if (is_load) {
        switch (off) {
        case MHI_REG_MAGIC:     return MHI_MAGIC;
        case MHI_REG_VERSION:   return MHI_PROTO_VERSION;
        case MHI_REG_ARG0:      return M.arg0;
        case MHI_REG_ARG1:      return M.arg1;
        case MHI_REG_ARG2:      return M.arg2;
        case MHI_REG_STATE:     return M.state;
        case MHI_REG_CONSUMED:  return M.consumed;   /* bytes accepted from guest  */
        case MHI_REG_PRODUCED:  return mhi_ctl[MHI_CTRL_PCM_PROD / 4];
        case MHI_REG_HZ:        return mhi_ctl[MHI_CTRL_DEC_HZ / 4];
        case MHI_REG_CHANNELS:  return 2u;           /* core#0 normalises to stereo */
        case MHI_REG_QFREE:     return MHI_DESC_RING - M.d_count;
        case MHI_REG_UNDERRUNS: return M.underruns;
        case MHI_REG_ERROR:     return M.errors;
        case MHI_REG_PLAYED:    return mhi_played_view();
        case MHI_REG_CAPS:      return mhi_ctl[MHI_CTRL_DEC_CAPS  / 4];
        case MHI_REG_SINK_LEAD: return mhi_ctl[MHI_CTRL_SINK_LEAD / 4];
        case MHI_REG_MAIN_REV:  return mhi_ctl[MHI_CTRL_MAIN_REV  / 4];
        default:                return 0;
        }
    }

    switch (off) {
    case MHI_REG_ARG0: M.arg0 = wval; return 0;
    case MHI_REG_ARG1: M.arg1 = wval; return 0;
    case MHI_REG_ARG2: M.arg2 = wval; return 0;
    case MHI_REG_CMD:
        switch (wval) {
        case MHI_CMD_ALLOC:
            mhi_session_reset();
            M.vol_q = 256u; M.arg2 = 50u; mhi_recompute_gains();
            mhi_set_state(MHI_FWSTATE_STOPPED);
            break;
        case MHI_CMD_FREE:
            mhi_session_reset();
            mhi_set_state(MHI_FWSTATE_IDLE);
            break;
        case MHI_CMD_QUEUE:
            mhi_queue(M.arg0, M.arg1);
            break;
        case MHI_CMD_PLAY:
            /* Play may legally arrive with an empty queue (seek pattern). */
            mhi_set_paused(0u);
            mhi_set_state(MHI_FWSTATE_PLAYING);
            break;
        case MHI_CMD_STOP:
            mhi_session_reset();
            mhi_set_state(MHI_FWSTATE_STOPPED);
            break;
        case MHI_CMD_PAUSE:
            mhi_set_paused(1u);
            mhi_set_state(MHI_FWSTATE_PAUSED);
            break;
        case MHI_CMD_SETVOL:
            M.vol_q = (M.arg1 > 100u ? 100u : M.arg1) * 256u / 100u;
            mhi_recompute_gains();
            break;
        case MHI_CMD_SETPAN:
            M.arg2 = (M.arg1 > 100u ? 100u : M.arg1);
            mhi_recompute_gains();
            break;
        default: break;
        }
        return 0;
    default:
        return 0;
    }
}

MHI_TAIL void mhi_fw_reset(void)
{
    mhi_session_reset();
    mhi_set_state(MHI_FWSTATE_IDLE);
    M.underruns = M.errors = 0;
}

MHI_TAIL void mhi_fw_enable(int on)
{
    uint32_t off = on ? 0u : 1u;
    if (off == g_mhi_off)
        return;
    g_mhi_off = off;
    mhi_fw_reset();
    if (off)
        mhi_ctl[MHI_CTRL_MAGIC / 4] = 0u;          /* disarm: the Main's decode thread parks */
}
