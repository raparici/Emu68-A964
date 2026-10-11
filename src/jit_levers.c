/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* The JIT's run-time settings: cache mode, cache-control policy, translation depth, live updates from the Main. */

#include <stdint.h>
#include "support.h"
#include "config.h"
#include "M68k.h"
#include "EmuFeatures.h"
#include "amiga_memmap.h"   /* the FEAT_* wire layout */
#include "lists.h"
#include "jit_levers.h"
#include "OMMX.h"

#ifndef __aarch64__

extern struct M68KState *__m68k_state;
extern struct List LRU;
extern int CpuModel;
extern int JitStatsOff;
extern int ChipSlowdown, DbfSlowdown;

const uint16_t feat_jit_depth_tab[16] = { 256, 128, 64, 32, 16, 8, 4, 2, 1,
                                          12,
                                          14,
                                          15,
                                          13,
                                          /* 13-15 unused -> the safe default */
                                          256, 256, 256 };
const uint16_t feat_ipl_poll_tab[8] = { 32, 16, 8, 4, 2, 1, 32, 32 };
const uint8_t feat_ccr_scan_tab[8] = { 20, 0, 1, 2, 4, 8, 12, 16 };

volatile int g_ipl_poll_interval = IPL_POLL_INTERVAL;
int ipl_poll_countdown = 0;
_Static_assert(IPL_POLL_INTERVAL * M68K_LOOP_BUDGET_PER_POLL == M68K_LOOP_BUDGET_RELOAD,
               "the default loop budget must stay M68K_LOOP_BUDGET_RELOAD");

volatile uint32_t g_jit_backedge_off = 0;
volatile uint32_t g_b104_units = 0, g_b104_refused_gate = 0, g_b104_refused_state = 0;
volatile uint32_t g_b104_unrolled = 0, g_b104_unroll_cut = 0;

volatile uint32_t g_jit_mode = JITMODE_SOFT;
volatile uint32_t g_guest_smc_announce = 0;
static   uint32_t g_smc_announce_saved = 0;         /* where the latch waits while a verify mode runs */
volatile uint32_t g_cacr_policy = CACR_POLICY_IGNORE;
volatile uint32_t g_nocache_depth_idx = 6;
volatile uint32_t g_guest_override = 0;
volatile int      g_blitwait = 0;
volatile uint32_t g_jit_levers_dirty = 1;           /* the readback wants (re)publishing              */
volatile uint32_t g_cacr_off_edges = 0, g_cacr_on_edges = 0;
volatile uint32_t g_cntfrq_hz = EMU68_CPU_HZ;
volatile uint32_t g_cpu_hz    = EMU68_CPU_HZ;
volatile uint32_t g_bltpace_code = FEAT2_BLTPACE_TUNED;
volatile uint32_t g_bltpace_ticks = BLTPACE_TICKS_LEGACY;
volatile uint32_t g_bltpace_tuned_ns = 0;
volatile uint32_t g_bltpace_spin_cycles_min = 0, g_bltpace_spin_cycles_max = 0;

/* the BASE values -- the last writer's -- that a CACR temporary returns to */
static uint32_t jl_osd_mode   = JITMODE_SOFT;
static uint32_t jl_base_mode  = JITMODE_SOFT;       /* OSD word, or the guest's SOFT bit             */
static uint32_t jl_base_depth = EMU68_M68K_INSN_DEPTH;
/* the CACR temporary */
static int jl_cache_on   = 0;                       /* the model's cache-enable bit as last evaluated */
static int jl_temp_active = 0;                      /* policy holds a temporary mode/depth            */

static int jl_apply_effective(void);

static inline uint32_t jl_clamp_depth(uint32_t want)
{
    if (want > (uint32_t)EMU68_M68K_INSN_DEPTH) want = (uint32_t)EMU68_M68K_INSN_DEPTH;
    if (want < 1u) want = 1u;
    return want;
}

uint32_t jit_levers_base_depth(void) { return jl_base_depth; }
uint32_t jit_levers_base_mode(void)  { return jl_base_mode; }
int jit_levers_set_base_depth(uint32_t want)
{
    jl_base_depth = jl_clamp_depth(want);
    int changed = jl_apply_effective();
    if (__m68k_state) jit_levers_mirror(__m68k_state);   /* same rule as jit_set_mode */
    return changed;
}
int      jit_levers_temp_active(void) { return jl_temp_active; }
int      jit_levers_cache_on(void)   { return jl_cache_on; }

int jit_backedge_allowed(uint32_t lo_addr)
{
    if (g_jit_backedge_off) return 0;
    if (!g_guest_smc_announce) return 0;
    if (g_jit_mode != JITMODE_CACHED && g_jit_mode != JITMODE_SOFT) return 0;
    if (lo_addr < 0x00200000u) return 0;         /* chip RAM */
    return 1;
}

static int jl_set_effective_mode(uint32_t want)
{
    if (want > JITMODE_MAX) return 0;
    if (want == g_jit_mode) return 0;
    if (jitmode_verifies(want) && !jitmode_verifies(g_jit_mode)) {
        g_smc_announce_saved = g_guest_smc_announce;
        g_guest_smc_announce = 0;
    } else if (!jitmode_verifies(want) && jitmode_verifies(g_jit_mode)) {
        g_guest_smc_announce = g_smc_announce_saved;
    }
    g_jit_mode = want;
    M68K_InvalidateAllUnits();
    return 1;
}

static uint32_t jl_policy_mode(uint32_t base)
{
    if (g_cacr_policy == CACR_POLICY_LITERAL) return JITMODE_NOCACHE;
    if (base == JITMODE_NOCACHE || jitmode_verifies(base)) return base;
    if (base == JITMODE_CHIPSPEED) return JITMODE_VERIFY_CHIPSPEED;
    return JITMODE_VERIFY;
}

/* Re-derive the effective mode and depth from the base and the temporary's state. */
static int jl_apply_effective(void)
{
    int changed = 0;
    if (jl_temp_active) {
        changed |= jl_set_effective_mode(jl_policy_mode(jl_base_mode));
        if (g_cacr_policy >= CACR_POLICY_VERIFY_DEPTH) {
            uint32_t d = jl_clamp_depth(feat_jit_depth_tab[g_nocache_depth_idx & 15u]);
            if (d > jl_base_depth) d = jl_base_depth;
            if (Options.M68K_TRANSLATION_DEPTH != d) { Options.M68K_TRANSLATION_DEPTH = d; changed = 1; }
        } else if (Options.M68K_TRANSLATION_DEPTH != jl_base_depth) {
            Options.M68K_TRANSLATION_DEPTH = jl_base_depth; changed = 1;
        }
    } else {
        changed |= jl_set_effective_mode(jl_base_mode);
        if (Options.M68K_TRANSLATION_DEPTH != jl_base_depth) { Options.M68K_TRANSLATION_DEPTH = jl_base_depth; changed = 1; }
    }
    return changed;
}

int jit_set_mode(uint32_t want)
{
    if (want > JITMODE_MAX) return 0;
    jl_osd_mode = jl_base_mode = want;
    int changed = jl_apply_effective();
    if (__m68k_state) jit_levers_mirror(__m68k_state);
    return changed;
}

static inline int jl_cacr_icache_on(const struct M68KState *m)
{
    uint32_t c = BE32(m->CACR);
    return CPUMODEL_HAS_040_REGS(CpuModel) ? (c & CACR_IE) != 0 : (c & CACR20_EI) != 0;
}

#define CACR_RMW_WINDOW 16u
#define CACR20_ARCH_BIT   CACR20_EI
#define CACR20_ALIAS_BIT  CACR_IE
_Static_assert(__builtin_offsetof(struct M68KState, CACR_RDPC) < 4096, "CACR_RDPC must be reachable by an imm12 str");
static inline int jl_cacr_rmw(const struct M68KState *m)
{
    uint32_t wpc = BE32((uint32_t)m->PC) - 4u;
    return (wpc - BE32(m->CACR_RDPC)) < CACR_RMW_WINDOW;
}

static void jl_cacr_eval(const struct M68KState *m68k)
{
    int on = jl_cacr_icache_on(m68k);
    if (on == jl_cache_on) return;
    jl_cache_on = on;
    if (on) g_cacr_on_edges++; else g_cacr_off_edges++;
    g_jit_levers_dirty = 1;                          /* flag bit 1 (cache off) changed */
    if (g_cacr_policy == CACR_POLICY_IGNORE) return;
    M68K_InvalidateAllUnits();
    jl_temp_active = !on;
    jl_apply_effective();
}

#define JIT_DBG_BUDGET 256u
volatile uint32_t g_jit_dbg_dumps = 0;
extern void disasm_init(void);
extern void disasm_open_b(void);
extern void disasm_print_b(uint16_t *m68k_addr, uint16_t m68k_count, uint32_t *arm_addr, size_t arm_size, uint32_t *arm_start);
void jit_dbg_unit(struct M68KTranslationUnit *u)
{
    static uint32_t seen = 0, budget = 0, opened = 0;
    uint32_t ctrl = BE32(__m68k_state->DBG_CTRL);
    if ((ctrl & 3u) == 0u) return;
    if (ctrl != seen) { seen = ctrl; budget = JIT_DBG_BUDGET; }
    uint32_t lo = BE32(__m68k_state->DBG_ADDRLO), hi = BE32(__m68k_state->DBG_ADDRHI);
    if ((uintptr_t)u->mt_M68kHigh < lo || (uintptr_t)u->mt_M68kLow > hi) return;
    if (budget == 0u) return;
    budget--;
    g_jit_dbg_dumps++;
    if (!opened) { opened = 1; disasm_init(); disasm_open_b(); }   /* capstone's tlsf hooks, then the handles */
    kprintf("[JIT:DBG] unit %08x-%08x: %u m68k insns, %u ARM words (level %u%s)\n",
            (uint32_t)(uintptr_t)u->mt_M68kLow, (uint32_t)(uintptr_t)u->mt_M68kHigh,
            (unsigned)u->mt_M68kInsnCnt, (unsigned)u->mt_ARMInsnCnt, ctrl & 3u, (ctrl & 4u) ? ", +ARM" : "");
    disasm_print_b((uint16_t *)((uintptr_t)u->mt_M68kAddress & ~(uintptr_t)1u), (uint16_t)u->mt_M68kInsnCnt,
                 (ctrl & 4u) ? (uint32_t *)u->mt_ARMEntryPoint : (uint32_t *)0,
                 (ctrl & 4u) ? (size_t)u->mt_ARMInsnCnt * 4u : 0u, (uint32_t *)u->mt_ARMEntryPoint);
    kprintf("\n");
}

#if defined(__arm__) && !defined(__aarch64__) && !defined(__APPLE__)
#define R10_TEXT   __attribute__((section(".r10_tail_text"), noinline, optimize("Os")))
#else
#define R10_TEXT   __attribute__((noinline))
#endif
extern uintptr_t g_r10_span_addr, g_r10_span_bytes;
R10_TEXT void jit_dbg_unit_w(struct M68KTranslationUnit *u)
{
    uintptr_t a = (uintptr_t)u->mt_M68kAddress & ~(uintptr_t)1u;
    g_r10_span_addr = a;
    g_r10_span_bytes = (uintptr_t)u->mt_M68kHigh > a ? (uintptr_t)u->mt_M68kHigh - a : 0u;
    jit_dbg_unit(u);
    g_r10_span_addr = 0;
}

#define BLITWAIT_MAX_READS 20000u
volatile uint32_t g_blitwait_hits = 0, g_blitwait_spins = 0, g_blitwait_timeouts = 0;
extern unsigned int ps_read_16(unsigned int address);
void jit_blitwait_spin(void)
{
    uint32_t n = 0;
    g_blitwait_hits++;
    while (ps_read_16(0x00DFF002u) & 0x4000u) {          /* DMACONR bit 14 = BBUSY */
        if (++n >= BLITWAIT_MAX_READS) { g_blitwait_timeouts++; break; }
    }
    g_blitwait_spins += n;
}

int jit_soft_flush_active(void)
{
    return g_jit_mode == JITMODE_SOFT;
}

static inline uint32_t jl_pmc(void)
{
#ifndef __aarch64__
    uint32_t v; asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(v)); return v;
#else
    return 0;
#endif
}

void jit_flush_all_guest(void)
{
    if (jit_soft_flush_active() && __m68k_state) {
        uint32_t thr = BE32(__m68k_state->JIT_SOFTFLUSH_THRESH);
        if (thr == 0u) thr = EMU68_WEAK_CFLUSH_LIMIT;
        if (__m68k_state->JIT_UNIT_COUNT < thr) {
            struct Node *n;
            ForeachNode(&LRU, n) {
                struct M68KTranslationUnit *u =
                    (struct M68KTranslationUnit *)((intptr_t)n - __builtin_offsetof(struct M68KTranslationUnit, mt_LRUNode));
                jit_unit_mark(u);
            }
            return;
        }
    }
    M68K_InvalidateAllUnits();
}

void m68k_icache_announce(void)
{
    if (jitmode_verifies(g_jit_mode))
    {
        g_smc_announce_saved = 1;
    }
    else if (!g_guest_smc_announce)
    {
        g_guest_smc_announce = 1;
        kprintf("[JIT:DR6] guest announced a cache clear (CINV/CPUSH icache) — "
                "per-dispatch staleness CRC is now OFF\n");
    }
}

void m68k_do_cacr(struct M68KState *m68k)
{
    uint32_t val = BE32(m68k->CACR_VAL);

    if (CPUMODEL_HAS_040_REGS(CpuModel)) {
        m68k->CACR = BE32(val & (CPUMODEL_IS_060(CpuModel) ? 0xf880e000u : 0x80008000u));
    } else {
        uint32_t prev = BE32(m68k->CACR);
        uint32_t ei   = (prev & CACR20_EI) ? 1u : 0u;
        if ((val ^ prev) & CACR20_ARCH_BIT)
            ei = (val & CACR20_ARCH_BIT) ? 1u : 0u;                  /* rule 1: the architectural bit */
        else if ((val ^ prev) == CACR20_ALIAS_BIT && jl_cacr_rmw(m68k))
            ei = (val & CACR20_ALIAS_BIT) ? 1u : 0u;                 /* rule 2: EmuControl's RMW */
        m68k->CACR = BE32((val & CACR20_FI) | (ei ? (CACR20_EI | CACR_IE) : 0u));
        m68k->CACR_RDPC = 0;                                         /* the read is consumed by this write */
        if (val & CACR_CLEAR_ANY)
        {
            jit_flush_all_guest();
            if (jitmode_verifies(g_jit_mode))
            {
                g_smc_announce_saved = 1;
            }
            else if (!g_guest_smc_announce)
            {
                g_guest_smc_announce = 1;
                kprintf("[JIT:DR6] guest announced a cache clear (CACR=%08x) — "
                        "per-dispatch staleness CRC is now OFF\n", val);
            }
        }
    }
    jl_cacr_eval(m68k);
}

static uint32_t jl_soft_thresh = 0;
int m68k_do_jitctl(struct M68KState *m68k)
{
    if (g_cacr_policy == CACR_POLICY_IGNORE) {
        m68k->JIT_SOFTFLUSH_THRESH = BE32(jl_soft_thresh ? jl_soft_thresh : (uint32_t)EMU68_WEAK_CFLUSH_LIMIT);
        jit_levers_mirror(m68k);
        return 0;
    }
    int changed = 0;
    uint32_t c2 = BE32(m68k->JIT_CONTROL2);
    int      cs  = (c2 & 1u) ? (int)(1u + ((c2 >> 8) & 7u)) : 0;
    int      dbf = (int)((c2 >> 1) & 1u);
    uint32_t ccr = (c2 >> 3) & 0x1Fu;
    int      bw  = (int)((c2 >> 11) & 1u);
    uint32_t nbe = (c2 & JC2F_NO_BACKEDGE) ? 1u : 0u;
    uint32_t nob = (c2 & JC2F_NO_OMMX_BANKS) ? 1u : 0u;
    if (ChipSlowdown != cs)  { ChipSlowdown = cs;  changed = 1; }
    if (DbfSlowdown  != dbf) { DbfSlowdown  = dbf; changed = 1; }
    if (Options.M68K_CCR_SCAN_DEPTH != ccr) { Options.M68K_CCR_SCAN_DEPTH = ccr; changed = 1; }
    if (g_blitwait != bw)    { g_blitwait = bw; g_guest_override = 1; }
    /* dispatch-time, like blitwait: nothing emitted depends on it, so no flush */
    if (g_ommx_banks_off != nob) { ommx_banks_set_off(nob); g_guest_override = 1; }
    if (g_jit_backedge_off != nbe) { g_jit_backedge_off = nbe; changed = 1; }
    {
        uint32_t c = BE32(m68k->JIT_CONTROL);
        uint32_t depth = (c >> 24) & 0xFFu; if (depth == 0u) depth = 256u;
        uint32_t irng  = (c >> 8) & 0xFFFFu;
        uint32_t lcnt  = (c >> 4) & 0xFu;   if (lcnt == 0u) lcnt = (uint32_t)EMU68_MAX_LOOP_COUNT;
        depth = jl_clamp_depth(depth);
        if (jl_base_depth != depth) { g_guest_override = 1; if (jit_levers_set_base_depth(depth)) changed = 1; }
        if (Options.M68K_INLINE_RANGE != irng) { Options.M68K_INLINE_RANGE = irng; g_guest_override = 1; changed = 1; }
        if (Options.M68K_LOOP_COUNT != lcnt)   { Options.M68K_LOOP_COUNT = lcnt;   g_guest_override = 1; changed = 1; }
        if (c & JCCF_SOFT) {
            if (jl_base_mode != JITMODE_SOFT && (jl_osd_mode == JITMODE_CACHED || jl_osd_mode == JITMODE_SOFT)) {
                jl_base_mode = JITMODE_SOFT; g_guest_override = 1;
                if (jl_apply_effective()) changed = 1;
            }
        } else if (jl_base_mode == JITMODE_SOFT) {
            jl_base_mode = (jl_osd_mode == JITMODE_SOFT) ? JITMODE_CACHED : jl_osd_mode; g_guest_override = 1;
            if (jl_apply_effective()) changed = 1;
        }
    }
    jl_soft_thresh = BE32(m68k->JIT_SOFTFLUSH_THRESH);
    if (changed) { g_guest_override = 1; M68K_InvalidateAllUnits(); }
    jit_levers_mirror(m68k);
    g_jit_levers_dirty = 1;
    return changed;
}

int m68k_service_ctl_req(struct M68KState *m68k)
{
    uint32_t rq = m68k->CACR_REQ;
    int dropped = 0;
    if (rq & 1u) { m68k_do_cacr(m68k); dropped = 1; }
    if (rq & 2u) { if (m68k_do_jitctl(m68k)) dropped = 1; }
    m68k->CACR_REQ = 0;
    return dropped;
}

int jit_levers_apply_feats(uint32_t feats, int ok)
{
    return jit_levers_apply_feats2(feats, 0u, ok);
}
int jit_levers_apply_feats2(uint32_t feats, uint32_t feats2, int ok)
{
    int changed = 0;
    /* the operator acted: whatever the guest had overridden, the OSD's word is the base again */
    g_guest_override = 0;
    {
        uint32_t want_off = (ok && (feats2 & FEAT2_NO_BACKEDGE)) ? 1u : 0u;
        if (g_jit_backedge_off != want_off) { g_jit_backedge_off = want_off; changed = 1; }
    }
    {
        ommx_banks_set_off((ok && (feats2 & FEAT2_NO_OMMX_BANKS)) ? 1u : 0u);
    }
#ifdef EMU68_A3D
    {
        extern void a3d_fw_set_wait_mode(uint32_t exact);
        a3d_fw_set_wait_mode((ok && (feats2 & FEAT2_A3D_WAIT_EXACT)) ? 1u : 0u);
    }
#endif
    {
        uint32_t lv = ok ? ((feats2 & FEAT2_LCNT_MASK) >> FEAT2_LCNT_SHIFT) : 0u;
        uint32_t lwant = lv ? lv : (uint32_t)EMU68_MAX_LOOP_COUNT;
        if (Options.M68K_LOOP_COUNT != lwant) { Options.M68K_LOOP_COUNT = lwant; changed = 1; }
    }
    {
        uint32_t bp = ok ? ((feats2 & FEAT2_BLTPACE_MASK) >> FEAT2_BLTPACE_SHIFT) : FEAT2_BLTPACE_TUNED;
        g_bltpace_code = bp;
        jit_bltpace_update();
    }
    {
        uint32_t idx  = ok ? ((feats & FEAT_JIT_DEPTH_MASK) >> FEAT_JIT_DEPTH_SHIFT) : 0u;
        uint32_t want = jl_clamp_depth(feat_jit_depth_tab[idx & 15u]);
        if (jl_base_depth != want) { jl_base_depth = want; changed = 1; }
    }
    {
        uint32_t pidx = ok ? ((feats & FEAT_IPL_POLL_MASK) >> FEAT_IPL_POLL_SHIFT) : 0u;
        int pwant = (int)feat_ipl_poll_tab[pidx & 7u];
        if (pwant < 1) pwant = 1;
        g_ipl_poll_interval = pwant;
    }
    {
        int want_off = (ok && (feats & FEAT_JITSTATS_OFF)) ? 1 : 0;
        if (JitStatsOff != want_off) { JitStatsOff = want_off; changed = 1; }
    }
    {
        int dwant = (ok && (feats & FEAT_DBF_SLOWDOWN)) ? 1 : 0;
        uint32_t cs = ok ? ((feats & FEAT_CHIP_SLOW_MASK) >> FEAT_CHIP_SLOW_SHIFT) : 0u;
        if (cs > 8u) cs = 8u;
        if (DbfSlowdown  != dwant)   { DbfSlowdown  = dwant;    changed = 1; }
        if (ChipSlowdown != (int)cs) { ChipSlowdown = (int)cs;  changed = 1; }
    }
    {
        uint32_t cidx = ok ? ((feats & FEAT_CCR_SCAN_MASK) >> FEAT_CCR_SCAN_SHIFT) : 0u;
        uint32_t cwant = (uint32_t)feat_ccr_scan_tab[cidx & 7u];
        if (Options.M68K_CCR_SCAN_DEPTH != cwant) { Options.M68K_CCR_SCAN_DEPTH = cwant; changed = 1; }
    }
    {
        g_blitwait = (ok && (feats & FEAT_BLITWAIT)) ? 1 : 0;
    }
    {
        uint32_t pol = ok ? ((feats & FEAT_CACR_POLICY_MASK) >> FEAT_CACR_POLICY_SHIFT) : CACR_POLICY_IGNORE;
        uint32_t nidx = ok ? ((feats & FEAT_NOCACHE_DEPTH_MASK) >> FEAT_NOCACHE_DEPTH_SHIFT) : 6u;
        int pchanged = (g_cacr_policy != pol) || (g_nocache_depth_idx != nidx);
        g_cacr_policy = pol;
        g_nocache_depth_idx = nidx;
        if (pchanged && jl_temp_active && pol == CACR_POLICY_IGNORE) { jl_temp_active = 0; changed = 1; }
    }
    /* the guest's SOFT bit is gone with the override; the OSD's mode is the base again */
    jl_base_mode = jl_osd_mode;
    changed |= jl_apply_effective();
    if (__m68k_state) jit_levers_mirror(__m68k_state);
    g_jit_levers_dirty = 1;
    return changed;
}

void jit_levers_reset(struct M68KState *m68k)
{
    g_guest_override = 0;
    m68k->CACR = 0;
    m68k->CACR_VAL = 0;
    m68k->CACR_REQ = 0;
    jl_cache_on = 0;
    jl_base_mode = jl_osd_mode;
    jl_temp_active = 0;                                       /* the base runs at cold boot / m68k reset; a real disable edge arms the temporary */
    jl_apply_effective();
    ommx_banks_reset(m68k);
    jit_levers_mirror(m68k);
    g_jit_levers_dirty = 1;
}

static uint32_t jl_idx_of_depth(uint32_t d)
{
    uint32_t best = 8u, bestv = 0u;                          /* index 8 = 1, the floor */
    for (uint32_t i = 0; i < 13u; i++) {
        if (feat_jit_depth_tab[i] == d) return i;
        if (feat_jit_depth_tab[i] < d && feat_jit_depth_tab[i] > bestv) { bestv = feat_jit_depth_tab[i]; best = i; }
    }
    return best;
}
static uint32_t jl_idx_of_ccr(uint32_t c)
{
    uint32_t best = 1u, bestv = 0u;                          /* index 1 = 0, the floor */
    for (uint32_t i = 0; i < 8u; i++) {
        if (feat_ccr_scan_tab[i] == c) return i;
        if (feat_ccr_scan_tab[i] < c && feat_ccr_scan_tab[i] >= bestv) { bestv = feat_ccr_scan_tab[i]; best = i; }
    }
    return best;
}
static uint32_t jl_idx_of_poll(int p)
{
    for (uint32_t i = 0; i < 6u; i++) if ((int)feat_ipl_poll_tab[i] == p) return i;
    return 0u;
}

void jit_levers_publish(void)
{
    volatile uint32_t *rb = (volatile uint32_t *)(LAUNCH_HEALTH_MIRROR_VA + LIVE_RB_OFFSET);
    uint32_t eff_depth = Options.M68K_TRANSLATION_DEPTH;
    uint32_t w = 0;
    if (JitStatsOff) w |= FEAT_JITSTATS_OFF;
    w |= (jl_idx_of_depth(eff_depth) & 15u) << FEAT_JIT_DEPTH_SHIFT;     /* IN FORCE: the NC index while the temporary holds */
    w |= (jl_idx_of_poll(g_ipl_poll_interval) & 7u) << FEAT_IPL_POLL_SHIFT;
    if (DbfSlowdown) w |= FEAT_DBF_SLOWDOWN;
    w |= ((uint32_t)ChipSlowdown & 15u) << FEAT_CHIP_SLOW_SHIFT;
    if (g_blitwait) w |= FEAT_BLITWAIT;
    w |= (jl_idx_of_ccr(Options.M68K_CCR_SCAN_DEPTH) & 7u) << FEAT_CCR_SCAN_SHIFT;
    w |= (g_cacr_policy & 3u) << FEAT_CACR_POLICY_SHIFT;
    w |= (g_nocache_depth_idx & 15u) << FEAT_NOCACHE_DEPTH_SHIFT;
    uint32_t flags = (g_guest_override ? 1u : 0u)
                   | (jl_cache_on ? 0u : 2u)
                   | ((jl_temp_active && g_cacr_policy >= CACR_POLICY_VERIFY_DEPTH) ? 4u : 0u);
    rb[1] = w;
    rb[2] = flags;
    rb[3] = (eff_depth & 0x1FFu) | ((Options.M68K_CCR_SCAN_DEPTH & 0x1Fu) << 16);
    rb[4] = g_blitwait_timeouts;
    rb[5] = Options.M68K_LOOP_COUNT;
    asm volatile("dsb" ::: "memory");
    rb[0] = LIVE_RB_MAGIC;
    arm_flush_cache((uintptr_t)rb, 32);
}

uint32_t bltpace_ticks_for(uint32_t code, uint32_t cntfrq_hz, uint32_t tuned_ns)
{
    uint32_t ns;
    if (code == FEAT2_BLTPACE_OFF) return 0u;
    if (code == FEAT2_BLTPACE_TUNED) {
        if (tuned_ns == 0u) return BLTPACE_TICKS_LEGACY;
        ns = tuned_ns;
    } else {
        ns = code * FEAT2_BLTPACE_UNIT_NS;
    }
    if (ns > 60000u) ns = 60000u;
    uint32_t f100k = cntfrq_hz / 100000u;
    if (f100k == 0u) return BLTPACE_TICKS_LEGACY;           /* no plausible counter rate */
    uint32_t t = (ns * f100k + 9999u) / 10000u;
    return t ? t : 1u;
}
void jit_bltpace_update(void)
{
    uint32_t t = bltpace_ticks_for(g_bltpace_code, g_cntfrq_hz, g_bltpace_tuned_ns);
    if (t != g_bltpace_ticks) { g_bltpace_ticks = t; }
    g_jit_levers_dirty = 1;                                 /* republish BLP1 with the LVR1 readback */
}
int bltpace_wait_ticks(uint32_t n)
{
    uint32_t t0 = jl_pmc();
    uint32_t cap = (n > (0xFFFFFFFFu - 1024u) / 64u) ? 0xFFFFFFFFu : n * 64u + 1024u;
    while ((uint32_t)(jl_pmc() - t0) < n) {
        if (--cap == 0u) return 1;
    }
    return 0;
}

static uint16_t jl_live_gen_seen = 0;
int jit_levers_host_tick(void)
{
    int dropped = 0;
    {
        volatile uint32_t *jc = (volatile uint32_t *)(LAUNCH_HEALTH_MIRROR_VA + JITCACHE_CTRL_OFFSET);
        arm_flush_cache((uintptr_t)jc, 4);
        uint32_t v = __builtin_bswap32(*jc);                 /* core#0 writes LE */
        uint32_t magic = v & 0xFFFF0000u;
        if (magic == JITCACHE_CTRL_MAGIC || magic == JITCACHE_CTRL_MAGIC3) {
            uint32_t want = (magic == JITCACHE_CTRL_MAGIC3) ? (v & 7u) : (v & 3u);
            if (want <= JITMODE_MAX && want != jl_osd_mode) {
                if (jit_set_mode(want)) dropped = 1;
                g_jit_levers_dirty = 1;
            }
        }
    }
    {
        volatile uint32_t *rq = (volatile uint32_t *)(LAUNCH_HEALTH_MIRROR_VA + LIVE_REQ_OFFSET);
        arm_flush_cache((uintptr_t)rq, 4);
        uint32_t v = __builtin_bswap32(*rq);
        uint16_t gen = (uint16_t)(v & 0xFFFFu);
        if ((v >> 16) == LIVE_REQ_MAGIC && gen != 0u && gen != jl_live_gen_seen) {
            jl_live_gen_seen = gen;
            volatile uint32_t *dsc = (volatile uint32_t *)(LAUNCH_HEALTH_MIRROR_VA + FEAT_DESC_OFFSET);
            arm_flush_cache((uintptr_t)dsc, 16);
            asm volatile("dsb" ::: "memory");
            uint32_t fmagic = __builtin_bswap32(dsc[0]), feats = __builtin_bswap32(dsc[1]), feats2 = __builtin_bswap32(dsc[2]);
            if (jit_levers_apply_feats2(feats, feats2, fmagic == FEAT_DESC_MAGIC)) { M68K_InvalidateAllUnits(); dropped = 1; }
            *(volatile uint32_t *)(LAUNCH_HEALTH_MIRROR_VA + LIVE_ACK_OFFSET) = ((uint32_t)LIVE_ACK_MAGIC << 16) | gen;
            arm_flush_cache(LAUNCH_HEALTH_MIRROR_VA + LIVE_ACK_OFFSET, 4);
            kprintf("[JIT:LIVE] gen %u: feats %08x feats2 %08x applied (live subset)%s\n", gen, feats, feats2, dropped ? " - units dropped" : "");
            g_jit_levers_dirty = 1;
        }
    }
    /* (c) the readback, whenever something changed */
    if (g_jit_levers_dirty) { g_jit_levers_dirty = 0; jit_levers_publish(); }
    return dropped;
}

void jit_levers_mirror(struct M68KState *m68k)
{
    uint32_t c2 = BE32(m68k->JIT_CONTROL2);
    c2 &= ~(uint32_t)((1u << 0) | (1u << 1) | JC2F_NO_BACKEDGE | (0x1Fu << 3) | (0x07u << 8) | (1u << 11) | JC2F_NO_OMMX_BANKS);
    c2 &= ~(uint32_t)((1u << 29) | (1u << 30));
    if (ChipSlowdown) c2 |= (1u << 0) | ((((uint32_t)ChipSlowdown - 1u) & 7u) << 8);   /* upstream stores cs_dist-1 */
    if (DbfSlowdown)  c2 |= (1u << 1);
    if (g_jit_backedge_off) c2 |= JC2F_NO_BACKEDGE;
    c2 |= (Options.M68K_CCR_SCAN_DEPTH & 0x1Fu) << 3;
    if (g_blitwait)   c2 |= (1u << 11);
    if (g_ommx_banks_off) c2 |= JC2F_NO_OMMX_BANKS;
    m68k->JIT_CONTROL2 = BE32(c2);
    {
        uint32_t c = BE32(m68k->JIT_CONTROL) & 0x0000000Eu;          /* keep bits 1-3 (undefined, stored) */
        c |= ((jl_base_depth & 0xFFu) << 24) | ((Options.M68K_INLINE_RANGE & 0xFFFFu) << 8)
           | ((Options.M68K_LOOP_COUNT & 0xFu) << 4);
        if (jl_base_mode == JITMODE_SOFT) c |= JCCF_SOFT;
        m68k->JIT_CONTROL = BE32(c);
    }
    if (m68k->JIT_SOFTFLUSH_THRESH == 0u) m68k->JIT_SOFTFLUSH_THRESH = BE32(EMU68_WEAK_CFLUSH_LIMIT);
}

#endif /* !__aarch64__ */
