/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* OMMX: the vector register file kept per exec task. */

#include <stdint.h>
#include "support.h"
#include "M68k.h"
#include "OMMX.h"
#include "amiga_memmap.h"   /* LAUNCH_HEALTH_MIRROR_VA, OMMXBANK_STATS_* */

volatile uint32_t g_ommx_banks_armed = 0;
volatile uint32_t g_ommx_banks_off = 0;

#ifndef EMU68_OMMX

void ommx_banks_arm(void) { }
void ommx_banks_switch_point(struct M68KState *m68k, int via_sr) { (void)m68k; (void)via_sr; }
void ommx_banks_reset(struct M68KState *m68k) { (void)m68k; }
void ommx_banks_set_off(uint32_t off) { g_ommx_banks_off = off ? 1u : 0u; }
uint32_t ommx_banks_slots_used(void) { return 0; }
int ommx_banks_slot_of(uint32_t task) { (void)task; return -1; }
uint32_t ommx_banks_owner(void) { return 0; }

#else /* EMU68_OMMX */

#define OB_EB_RETRY   64u            /* switch points to skip after $4 failed validation */
#define OB_EB_RECHECK 256u           /* switch points between re-reads of $4 while the cached base validates */
#define EB_CHKBASE    0x26u          /* NDK exec/execbase.i: ChkBase, ULONG ~ExecBase    */
#define EB_THISTASK   0x114u         /* NDK exec/execbase.i: ThisTask, APTR              */

struct ob_slot {
    uint32_t task;                   /* the TCB this bank belongs to; 0 = free           */
    uint32_t stamp;                  /* ob_clock at its last save or load (LRU)          */
    uint32_t D_hi[8];                /* M68KState.D_hi[]                                 */
    uint64_t E[24];                  /* M68KState.E[]                                    */
} __attribute__((aligned(8)));

static struct ob_slot ob_slot[OMMX_BANK_SLOTS];
static int      ob_used;
static uint32_t ob_owner;            /* the task whose bank is in M68KState; 0 = unknown  */
static int      ob_owner_slot = -1;  /* its slot when it has one (a hint, re-checked)     */
static uint32_t ob_eb;               /* cached ExecBase, re-validated on every use; 0 = none */
static uint32_t ob_eb_retry;         /* switch points left before $4 is read again        */
static uint32_t ob_eb_age;           /* switch points since $4 was last read              */
static uint32_t ob_clock;            /* LRU clock: one tick per save / load               */

__attribute__((weak)) int ommx_guest_read32(uint32_t addr, uint32_t *val)
{
    (void)addr; (void)val;
    return 0;
}

static int ob_eb_valid(uint32_t eb)
{
    uint32_t chk;
    if (eb == 0u || (eb & 1u)) return 0;                          /* exec: even ...          */
    if (!ommx_guest_read32(eb + EB_CHKBASE, &chk)) return 0;
    return chk == ~eb;                                            /* ... and ChkBase = ~base */
}

static uint32_t ob_execbase(void)
{
    uint32_t eb = ob_eb;
    if (eb && ++ob_eb_age < OB_EB_RECHECK && ob_eb_valid(eb)) return eb;   /* $4 read at age 0: one in 256 */
    ob_eb = 0u;
    if (ob_eb_retry) { ob_eb_retry--; return 0u; }
    ob_eb_age = 0u;
    if (!ommx_guest_read32(4u, &eb) || !ob_eb_valid(eb)) {
        ob_eb_retry = OB_EB_RETRY;
        return 0u;
    }
    ob_eb = eb;
    return eb;
}

static uint32_t ob_this_task(void)
{
    uint32_t eb = ob_execbase(), tt;
    if (!eb) return 0u;
    if (!ommx_guest_read32(eb + EB_THISTASK, &tt)) return 0u;
    if (tt & 1u) return 0u;                                       /* a TCB is never odd */
    return tt;
}

static int ob_find(uint32_t task)
{
    for (int i = 0; i < ob_used; i++)
        if (ob_slot[i].task == task) return i;
    return -1;
}

static int ob_alloc(int keep)
{
    if (ob_used < OMMX_BANK_SLOTS) return ob_used++;
    int victim = -1;
    uint32_t oldest = 0u;
    for (int i = 0; i < OMMX_BANK_SLOTS; i++) {
        if (i == keep) continue;
        uint32_t age = ob_clock - ob_slot[i].stamp;               /* wrap-safe */
        if (victim < 0 || age > oldest) { victim = i; oldest = age; }
    }
    return victim;
}

static void ob_forget_all(void)
{
    for (int i = 0; i < OMMX_BANK_SLOTS; i++) ob_slot[i].task = 0u;
    ob_used = 0;
    ob_owner = 0u;
    ob_owner_slot = -1;
}

void ommx_banks_arm(void)
{
    if (g_ommx_banks_armed) return;
    g_ommx_banks_armed = 1u;
    if (!g_ommx_banks_off && ob_owner == 0u) {
        ob_owner = ob_this_task();
        ob_owner_slot = -1;
    }
}

void ommx_banks_switch_point(struct M68KState *m68k, int via_sr)
{
    if (!g_ommx_banks_armed || g_ommx_banks_off) return;
    (void)via_sr;
    uint32_t tt = ob_this_task();
    if (tt == 0u) return;       /* no trustworthy OS: one bank */
    if (tt == ob_owner) return;
    if (ob_owner == 0u) {
        ob_owner = tt;                                            /* ADOPT: keep the flag, move nothing */
        ob_owner_slot = ob_find(tt);
        return;
    }
    int in = ob_find(tt);
    if (m68k->OMMX_TOUCHED) {
        int out = ob_owner_slot;
        if (out < 0 || ob_slot[out].task != ob_owner) out = ob_find(ob_owner);
        if (out < 0) out = ob_alloc(in);
        struct ob_slot *s = &ob_slot[out];
        s->task = ob_owner;
        s->stamp = ++ob_clock;
        for (int i = 0; i < 8; i++)  s->D_hi[i] = m68k->D_hi[i];
        for (int i = 0; i < 24; i++) s->E[i] = m68k->E[i];
    }
    if (in >= 0) {
        struct ob_slot *s = &ob_slot[in];
        s->stamp = ++ob_clock;
        for (int i = 0; i < 8; i++)  m68k->D_hi[i] = s->D_hi[i];
        for (int i = 0; i < 24; i++) m68k->E[i] = s->E[i];
    }
    m68k->OMMX_TOUCHED = 0u;
    ob_owner = tt;
    ob_owner_slot = in;
}

void ommx_banks_reset(struct M68KState *m68k)
{
    ob_forget_all();
    ob_eb = 0u;
    ob_eb_retry = 0u;
    ob_eb_age = 0u;
    if (m68k) m68k->OMMX_TOUCHED = 0u;
}

void ommx_banks_set_off(uint32_t off)
{
    uint32_t want = off ? 1u : 0u;
    if (want == g_ommx_banks_off) return;
    g_ommx_banks_off = want;
    if (!want) ob_forget_all();
}

uint32_t ommx_banks_slots_used(void) { return (uint32_t)ob_used; }
int ommx_banks_slot_of(uint32_t task) { return task ? ob_find(task) : -1; }
uint32_t ommx_banks_owner(void) { return ob_owner; }

#endif /* EMU68_OMMX */
