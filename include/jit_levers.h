/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* The JIT's run-time settings: cache mode, cache-control policy, translation depth, live updates from the Main. */

#ifndef _JIT_LEVERS_H
#define _JIT_LEVERS_H

#include <stdint.h>

struct M68KState;
struct M68KTranslationUnit;

#define JITMODE_CACHED            0u   /* cached; believe the guest's cache-clear announcements  */
#define JITMODE_NOCACHE           1u
#define JITMODE_VERIFY            2u   /* cached, CRC every unit ourselves (announce latch parked)*/
#define JITMODE_CHIPSPEED         3u   /* cached, except chip-RAM code (re-translated)            */
#define JITMODE_VERIFY_CHIPSPEED  4u   /* 2 and 3 at once                                          */
#define JITMODE_SOFT              5u
#define JITMODE_MAX               5u

#define JITMODE_CACHED_SET   ((1u << JITMODE_CACHED) | (1u << JITMODE_SOFT))   /* 0x21 */
#define JITMODE_IS_CACHED(m) ((JITMODE_CACHED_SET >> (m)) & 1u)

static inline int jitmode_verifies(uint32_t m)
{
    return m == JITMODE_VERIFY || m == JITMODE_VERIFY_CHIPSPEED;
}

#define CACR_POLICY_IGNORE        0u
#define CACR_POLICY_VERIFY        1u   /* mode 2 while the cache bit is clear                     */
#define CACR_POLICY_VERIFY_DEPTH  2u
#define CACR_POLICY_LITERAL       3u   /* mode 1 (translate every dispatch) at min(OSD, NOCACHE) */

#define JITCACHE_CTRL_OFFSET  0xB0Cu
#define JITCACHE_CTRL_MAGIC   0x4A4D0000u
#define JITCACHE_CTRL_MAGIC3  0x4A4E0000u

#define IPL_POLL_INTERVAL 32
extern volatile int g_ipl_poll_interval;
extern int ipl_poll_countdown;

#define JC2B_NO_BACKEDGE  2
#define JC2F_NO_BACKEDGE  (1u << JC2B_NO_BACKEDGE)
extern volatile uint32_t g_jit_backedge_off;

#define JC2B_NO_OMMX_BANKS 16
#define JC2F_NO_OMMX_BANKS (1u << JC2B_NO_OMMX_BANKS)
#define M68K_LOOP_BUDGET_PER_POLL 8u
int jit_backedge_allowed(uint32_t lo_addr);
extern volatile uint32_t g_b104_units, g_b104_refused_gate, g_b104_refused_state;
extern volatile uint32_t g_b104_unrolled, g_b104_unroll_cut;

extern const uint16_t feat_jit_depth_tab[16];
extern const uint16_t feat_ipl_poll_tab[8];
extern const uint8_t  feat_ccr_scan_tab[8];

/* ---- state visible to the dispatchers ------------------------------------------------------ */
extern volatile uint32_t g_jit_mode;            /* the EFFECTIVE cache mode (hot-path read)      */
extern volatile uint32_t g_guest_smc_announce;
extern volatile uint32_t g_cacr_policy;         /* CACR_POLICY_*                                 */
extern volatile uint32_t g_nocache_depth_idx;   /* index into feat_jit_depth_tab[]               */
extern volatile uint32_t g_guest_override;      /* 1 = a guest MOVEC write is in force           */
extern volatile int      g_blitwait;
extern volatile uint32_t g_cacr_off_edges, g_cacr_on_edges;
extern volatile uint32_t g_cntfrq_hz;
extern volatile uint32_t g_cpu_hz;

static inline int jit_dispatch_verifies(uint32_t pc)
{
    if (pc < 0x00200000u) return 1;                        /* chip RAM: always                         */
    if (g_guest_smc_announce) return 0;                    /* fast RAM: the guest announces its clears */
    return !(pc >= 0x00F80000u && pc < 0x01000000u);       /* non-ROM                                  */
}

#define BLTPACE_TICKS_LEGACY 0xFFFFFFFFu
#ifndef EMU68_BLTBUSY_PACE_N
#define EMU68_BLTBUSY_PACE_N 2048
#endif
extern volatile uint32_t g_bltpace_code;            /* feats2 bits 8-15 in force                          */
extern volatile uint32_t g_bltpace_ticks;           /* PMCCNTR ticks per BBUSY read; 0 none; LEGACY above   */
extern volatile uint32_t g_bltpace_tuned_ns;        /* the tuned time (boot measurement); 0 = none         */
extern volatile uint32_t g_bltpace_spin_cycles_min, g_bltpace_spin_cycles_max;   /* the measurement      */
extern volatile uint32_t g_bltpace_capped;          /* waits the iteration cap ended (counter stopped)     */
/* ticks to wait for `code` at a counter rate of `cntfrq_hz`, given the tuned time; pure, 32-bit only */
uint32_t bltpace_ticks_for(uint32_t code, uint32_t cntfrq_hz, uint32_t tuned_ns);
void     jit_bltpace_update(void);                  /* g_bltpace_ticks from the code, g_cntfrq_hz, tuned  */
/* wait n PMCCNTR ticks; returns 1 when the iteration cap ended the wait (the counter did not advance) */
int      bltpace_wait_ticks(uint32_t n);

void m68k_do_cacr(struct M68KState *m68k);
int  m68k_service_ctl_req(struct M68KState *m68k);
int  m68k_do_jitctl(struct M68KState *m68k);
/* The 040's CINV/CPUSH icache announcement (M68k_LINEF.c's weak hook, strong here). */
void m68k_icache_announce(void);
int  jit_set_mode(uint32_t want);
int  jit_levers_apply_feats2(uint32_t feats, uint32_t feats2, int ok);
int  jit_levers_apply_feats(uint32_t feats, int ok);
void jit_levers_reset(struct M68KState *m68k);
void jit_levers_mirror(struct M68KState *m68k);
/* The base depth / mode as the last writer left them (what a CACR temporary returns to). */
uint32_t jit_levers_base_depth(void);
uint32_t jit_levers_base_mode(void);
/* The last writer sets the base depth (clamped); returns 1 when the effective depth changed. */
int      jit_levers_set_base_depth(uint32_t want);
/* 1 while the CACR policy holds a temporary mode/depth (the guest has its cache off). */
int  jit_levers_temp_active(void);
/* 1 while the guest's instruction-cache enable bit (per model) is SET. */
int  jit_levers_cache_on(void);

int  jit_levers_host_tick(void);
#define JIT_JF_LEV_LAZYFPSR  0x01u
#define JIT_JF_LEV_FPMEM     0x02u
#define JIT_JF_LEV_FPSTORE   0x04u
#define JIT_JF_LEV_AS3392    0x08u
#define JIT_JF_LEV_FNOP      0x10u
#define JIT_JF_LEV_DEFAULT   0x11u
#define JIT_JU_UNROLL_MASK   0x0Fu
#define JIT_JU_LEV_DEFAULT   0x04u
#if JIT_JU_LEV_DEFAULT
uint32_t ju_unroll_cap(uint32_t n, uintptr_t first_copy_bytes);   /* jit_r10c.c b104_unroll_want_r10c's last word on n */
#endif
void      a1_tlsf_free(void *handle, void *ptr);
void      a1_FlushFPSR(uint32_t **arm_stream);
uintptr_t a1_translate_r10c(uint16_t *m68kcodeptr);
extern volatile uint32_t g_a1_gen;
void jit_levers_publish(void);
extern volatile uint32_t g_jit_levers_dirty;
void jit_dbg_unit(struct M68KTranslationUnit *u);
extern volatile uint32_t g_jit_dbg_dumps;
void jit_blitwait_spin(void);
extern volatile uint32_t g_blitwait_hits, g_blitwait_spins, g_blitwait_timeouts;

/* provided by M68k_Translator.c */
void M68K_InvalidateAllUnits(void);

#endif /* _JIT_LEVERS_H */
