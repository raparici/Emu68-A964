/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: the engine's firmware glue (heap, ONCALL dispatch, status block for the Main). */

#include <stdint.h>
#include <stdarg.h>
#include "support.h"
#include "M68k.h"
#include "OMMX.h"
#include "amiga_memmap.h"
#include "a3d_agent.h"

#ifndef EMU68_A3D_BACKEND
#define EMU68_A3D_BACKEND A3D_BE_FABRIC
#endif
extern volatile uint32_t g_cntfrq_hz;       /* the rate of PMCCNTR (jit_levers.h), set from the clock manager */

extern uint32_t mmu_table[4096];
extern int EnableOMMX;

extern int zz9000_vram_span(uint32_t *guest, uint32_t *bytes, uint32_t *phys, uint8_t **host) __attribute__((weak));

#include "a3d_fw_prot.h"
_Static_assert(A3D_MAX_CTX * A3D_PROT_SLOTS <= A3D_FW_PROT_KEYS,
               "the protection must hold a key for every context's every slot");

static uint32_t g_fw_backend = EMU68_A3D_BACKEND;
static uint32_t g_fw_up;

static void a3d_fw_dcache_flush_all(void)
{
    uint32_t ccsidr;
    __asm__ volatile("mcr p15, 2, %0, c0, c0, 0\n\tisb" :: "r"(0u) : "memory");
    __asm__ volatile("mrc p15, 1, %0, c0, c0, 0" : "=r"(ccsidr));                   /* CCSIDR                    */
    const uint32_t ways = (ccsidr >> 3) & 0x3FFu, sets = (ccsidr >> 13) & 0x7FFFu;
    const uint32_t la = ways ? (uint32_t)__builtin_clz(ways) : 0u, lb = (ccsidr & 7u) + 4u;
    __asm__ volatile("dsb" ::: "memory");
    for (uint32_t w = 0; w <= ways; w++)
        for (uint32_t st = 0; st <= sets; st++)
            __asm__ volatile("mcr p15, 0, %0, c7, c14, 2" :: "r"((w << la) | (st << lb)) : "memory");   /* DCCISW */
    __asm__ volatile("dsb" ::: "memory");
}

#define A3D_FW_FLUSH_ALL_BYTES  (256u << 10)

void a3d_plat_clean(const void *p, uint32_t len)
{
    if (len >= A3D_FW_FLUSH_ALL_BYTES)
        a3d_fw_dcache_flush_all();
    else if (len)
        arm_flush_cache((uintptr_t)p, len);
}

static uint32_t g_fw_stage_nc A3D_R8_BSS;        /* 1: staging is mapped 0x1c02 (a3d_fw_stage_nc)          */
static uint32_t g_fw_stage_nc_ask A3D_R8_BSS;

A3D_TAIL_CALL void a3d_plat_clean_nc(const void *p, uint32_t len)
{
    const uint32_t off = (uint32_t)((uintptr_t)p - A3D_HEAP_FW_VA);
    const uint32_t nc0 = g_fw_stage_nc ? 0u : (A3D_HEAP_STAGE_MB << 20);   /* the heap's non-cacheable part */
    if (off >= nc0 && off < A3D_HEAP_BYTES && len <= A3D_HEAP_BYTES - off)
    {
        __asm__ volatile("dsb" ::: "memory");   /* nothing to clean; the stores complete before a doorbell */
        return;
    }
    a3d_plat_clean(p, len);
}

void a3d_plat_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vkprintf(fmt, ap);
    va_end(ap);
}

uint32_t a3d_plat_cycles(void)
{
    uint32_t c;
    __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(c));     /* PMCCNTR */
    return c;
}

uint32_t a3d_plat_prof(void)
{
    return a3d_plat_cycles();               /* the core #1 split counts PMCCNTR ticks too */
}

uint32_t a3d_plat_cycles_per_ms(void)
{
    return g_cntfrq_hz / 1000u;             /* PMCCNTR's rate (x 1/64 under PMCR.D, hps_cntfrq_hz) */
}

#define A3D_GPU_PHYS    0xC2000000u
static uint32_t g_gpu_mapped;

int a3d_plat_gpu_map(void)
{
    if (!g_gpu_mapped)
    {
        mmu_table[A3D_GPU_PHYS >> 20] = A3D_GPU_PHYS | MMU_DEVICE;
        arm_flush_cache((uintptr_t)&mmu_table[A3D_GPU_PHYS >> 20], 4u);
        __asm__ volatile("dsb" ::: "memory");
        __asm__ volatile("mcr p15,0,%0,c8,c7,0" :: "r"(0u));       /* TLBIALL, as the heap's mapping does */
        __asm__ volatile("mcr p15,0,%0,c7,c5,6" :: "r"(0u));       /* BPIALL  */
        __asm__ volatile("dsb; isb" ::: "memory");
        g_gpu_mapped = 1;
    }
    return 1;
}

uint32_t a3d_plat_gpu_rd(uint32_t off)
{
    return rd32le(A3D_GPU_PHYS + (off & 0xFFCu));
}

void a3d_plat_gpu_wr(uint32_t off, uint32_t v)
{
    wr32le(A3D_GPU_PHYS + (off & 0xFFCu), v);
}

void a3d_plat_gpu_relax(void)
{
}

static int a3d_fw_map_heap(void)
{
    const uint32_t va = A3D_HEAP_FW_VA, phys = A3D_HEAP_DDR_BASE;
    for (uint32_t i = 0; i < A3D_HEAP_SIZE_MB; i++)
        mmu_table[(va >> 20) + i] = (phys + (i << 20)) | (i < A3D_HEAP_STAGE_MB ? MMU_CACHED : 0x1c02u);
    arm_flush_cache((uintptr_t)&mmu_table[va >> 20], A3D_HEAP_SIZE_MB * 4u);
    __asm__ volatile("dsb" ::: "memory");
    __asm__ volatile("mcr p15,0,%0,c8,c7,0" :: "r"(0u));       /* TLBIALL */
    __asm__ volatile("mcr p15,0,%0,c7,c5,6" :: "r"(0u));       /* BPIALL  */
    __asm__ volatile("dsb; isb" ::: "memory");
    return a3d_heap_init((uint8_t *)(uintptr_t)va, phys, A3D_HEAP_SIZE_MB << 20);
}

A3D_TAIL_CALL static void a3d_fw_stage_nc(void)
{
    const uint32_t va = A3D_HEAP_FW_VA, phys = A3D_HEAP_DDR_BASE;
    a3d_fw_dcache_flush_all();
    for (uint32_t i = 0; i < A3D_HEAP_STAGE_MB; i++)
        mmu_table[(va >> 20) + i] = (phys + (i << 20)) | 0x1c02u;
    arm_flush_cache((uintptr_t)&mmu_table[va >> 20], A3D_HEAP_STAGE_MB * 4u);
    __asm__ volatile("dsb" ::: "memory");
    __asm__ volatile("mcr p15,0,%0,c8,c7,0" :: "r"(0u));       /* TLBIALL */
    __asm__ volatile("mcr p15,0,%0,c7,c5,6" :: "r"(0u));       /* BPIALL  */
    __asm__ volatile("dsb; isb" ::: "memory");
    a3d_fw_dcache_flush_all();
    g_fw_stage_nc = 1;
    a3d_r8cnt.stage_nc = 1;
}

#define A3D_GR_ALL (A3D_GR_READ | A3D_GR_WRITE | A3D_GR_TARGET)

/* a section backed by guest Fast RAM: what cpuram_boards.c maps, and nothing else */
static int a3d_fw_fast_backing(uint32_t phys)
{
    return (phys >= FASTRAM_DDR_BASE && phys - FASTRAM_DDR_BASE < (Z2_AUTOCONF_MB << 20))
        || (phys >= Z3FAST_DDR_BASE && phys - Z3FAST_DDR_BASE < (Z3FAST_SIZE_MB << 20));
}

/* sections [s0, s1): runs of Fast RAM sections (MMU_FASTRAM, physically consecutive) become ranges */
static void a3d_fw_scan(uint32_t s0, uint32_t s1)
{
    uint32_t run_lo = 0, run_phys = 0, run_mb = 0;
    for (uint32_t s = s0; s <= s1; s++)
    {
        uint32_t ok = 0, ph = 0;
        if (s < s1)
        {
            const uint32_t e = mmu_table[s];
            ph = e & 0xFFF00000u;
            ok = (e & 3u) == 2u && (e & 0x000FFFEFu) == MMU_FASTRAM && a3d_fw_fast_backing(ph);
        }
        if (ok && run_mb && ph == run_phys + (run_mb << 20))
        {
            run_mb++;
            continue;
        }
        if (run_mb)
            a3d_guest_add(run_lo, run_mb << 20, (uint8_t *)(uintptr_t)run_lo, run_phys, A3D_GR_ALL);
        run_mb = 0;
        if (ok)
        {
            run_lo = s << 20;
            run_phys = ph;
            run_mb = 1;
        }
    }
}

static void a3d_fw_ranges(void)
{
    a3d_guest_reset();
    a3d_fw_scan(SEC_CPURAM_BASE, SEC_FAST_TOP);         /* the Z2 window, $200000 up            */
    a3d_fw_scan(0x100u, 0x800u);                        /* Z3 space, $10000000-$7FFFFFFF         */
    uint32_t g, n, p;
    uint8_t *h;
    if (zz9000_vram_span && zz9000_vram_span(&g, &n, &p, &h))
        a3d_guest_add(g, n, h, p, A3D_GR_ALL);
}

static uint32_t a3d_fw_fpscr_enter(void)
{
    uint32_t f;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(f));
    __asm__ volatile("vmsr fpscr, %0" :: "r"(0u));          /* round to nearest, IEEE, no traps */
    return f;
}

static void a3d_fw_fpscr_leave(uint32_t f)
{
    __asm__ volatile("vmsr fpscr, %0" :: "r"(f));
}

int a3d_plat_prot_set(uint32_t key, uint32_t base, uint32_t lo, uint32_t bytes)
{
    return a3d_fw_prot_set(key, base, lo, bytes);
}

int a3d_plat_prot_pending(uint32_t key, uint32_t on)
{
    return a3d_fw_prot_pending(key, on);
}

static uint32_t g_ap_base[12];

static void a3d_fw_counters_zero(void)
{
    g_ap_base[8] = g_ap_qcovers;
    g_ap_base[9] = g_ap_qfaults;
    g_ap_base[10] = g_ap_qdma;
    g_ap_base[11] = g_ap_qrefused;
    g_ap_base[0] = g_ap_faults;
    g_ap_base[1] = g_ap_shared;
    g_ap_base[2] = g_ap_self + g_ap_nested;
    g_ap_base[3] = g_ap_stale;
    g_ap_base[4] = g_ap_idle;
    g_ap_base[5] = g_ap_applies;
    g_ap_base[6] = g_ap_tlball;
    g_ap_base[7] = g_ap_tlbpages;
}

static void a3d_fw_sync_counters(void)
{
    a3d_cnt.prot_faults = g_ap_faults - g_ap_base[0];
    a3d_cnt.prot_shared = g_ap_shared - g_ap_base[1];
    a3d_cnt.prot_self = g_ap_self + g_ap_nested - g_ap_base[2];
    a3d_cnt.prot_stale = g_ap_stale - g_ap_base[3];
    a3d_cnt.prot_idle = g_ap_idle - g_ap_base[4];
    a3d_cnt.mmu_applies = g_ap_applies - g_ap_base[5];
    a3d_cnt.tlb_all = g_ap_tlball - g_ap_base[6];
    a3d_cnt.tlb_pages = g_ap_tlbpages - g_ap_base[7];
    a3d_cnt.q_covers = g_ap_qcovers - g_ap_base[8];
    a3d_cnt.q_faults = g_ap_qfaults - g_ap_base[9];
    a3d_cnt.q_dma = g_ap_qdma - g_ap_base[10];
    a3d_cnt.q_refused = g_ap_qrefused - g_ap_base[11];
#ifdef EMU68_ZZ9000
    {
        extern void zz_blitq_gate_stats(uint32_t out[6]);
        uint32_t g[6];
        zz_blitq_gate_stats(g);             /* cumulative since the card's init (a mode change or a guest reset) */
        a3d_cnt.q_gated = g[0];
        a3d_cnt.q_gate_waits = g[1];
        a3d_cnt.q_gate_blocks = g[2];
        a3d_cnt.q_tmpl = g[5];
    }
#endif
}

static uint32_t a3d_fw_touch_c(uint32_t lo, uint32_t hi, uint32_t why)
{
    const uint32_t n = a3d_touch(lo, hi, why);
    a3d_fw_sync_counters();
    return n;
}

static uint32_t a3d_fw_fence_try_c(uint32_t fence, uint32_t a, uint32_t b)
{
    (void)a;
    (void)b;
    const uint32_t r = a3d_fence_try(fence);
    a3d_fw_sync_counters();
    return r;
}

static uint32_t a3d_fw_fence_wait_c(uint32_t fence, uint32_t a, uint32_t b)
{
    (void)a;
    (void)b;
    a3d_fence_wait(fence);
    a3d_fw_sync_counters();
    return 1;
}

void a3d_plat_present_begin(void)
{
#ifdef EMU68_ZZ9000
    extern void zz_blitq_run_ready(void);
    zz_blitq_run_ready();
#endif
}

uint32_t a3d_plat_display_guard(uint32_t lo, uint32_t hi)
{
#ifdef EMU68_ZZ9000
    extern uint32_t zz9000_display_guard(uint32_t glo, uint32_t ghi);
    const uint32_t f = zz9000_display_guard(lo, hi);
    return f;
#else
    (void)lo;
    (void)hi;
    return 0;
#endif
}

static uint32_t g_pub_t;
static void a3d_fw_publish(void)
{
    volatile uint32_t *b = (volatile uint32_t *)(LAUNCH_HEALTH_MIRROR_VA + A3D_STATS_OFFSET);
    extern int EnableOMMX;
    uint32_t caps = 0;
    if (g_fw_up && a3d_ready())
        caps |= A3D_HCAP_UP;
    if (g_fw_up && g_oncall_on && EnableOMMX)
        caps |= A3D_HCAP_ONCALL;
    caps |= (a3d_backend_id() & 0xFu) << 4;
    caps |= (A3D_HEAP_SIZE_MB & 0xFFu) << 8;
    caps |= (A3D_AGENT_VERSION & 0xFFu) << 16;
    b[1] = caps;
    b[2] = (a3d_wait_mode & 0xFFu) | (a3d_prot_enabled ? 0x100u : 0u);
    a3d_fw_sync_counters();
    b[3] = a3d_cnt.lazy_waits;
    b[4] = a3d_cnt.prot_faults;
    b[5] = a3d_cnt.passes - a3d_cnt.sub_frame;
    b[6] = a3d_cnt.fault_flushes;
    b[7] = a3d_cnt.fence_flushes;
    b[8] = a3d_cnt.waits;
    b[9] = a3d_cnt.wait_cuts;
    b[10] = a3d_cnt.passes;
    b[11] = a3d_cnt.frames;
    b[12] = a3d_cnt.prot_idle;
    b[13] = a3d_cnt.prot_shared;
    b[14] = a3d_cnt.prot_self + a3d_cnt.prot_stale;
    b[15] = g_ap_pages;
    asm volatile("dsb" ::: "memory");
    b[0] = A3D_STATS_MAGIC;
    arm_flush_cache((uintptr_t)b, 64);
    g_pub_t = a3d_plat_cycles();
}

void a3d_fw_set_wait_mode(uint32_t exact)
{
    const uint32_t m = exact ? A3D_WAIT_EXACT : A3D_WAIT_AUTO;
    if (m != a3d_wait_mode)
        kprintf("[A3D] wait mode %s\n", m == A3D_WAIT_EXACT ? "EXACT (a cut at every wait)" : "AUTO (lazy waits + protection)");
    a3d_wait_mode = m;
    a3d_cnt.wait_mode = m;
    a3d_fw_publish();
}

static void a3d_fw_call(uint32_t id, uint32_t regs[4], uint32_t via_trap)
{
    if (id == A3D_HELLO || id == A3D_CTX_CREATE || id == A3D_TARGET)
        a3d_fw_ranges();
    const uint32_t f = a3d_fw_fpscr_enter();
    g_a3d_agent_busy = 1;
    a3d_fw_sync_counters();
    a3d_oncall(id, regs, via_trap);
    g_a3d_agent_busy = 0;
    if (a3d_fw_prot_held())
        a3d_fw_prot_hold(0);
    a3d_fw_fpscr_leave(f);
    if ((uint32_t)(a3d_plat_cycles() - g_pub_t) > (1u << 24))
        a3d_fw_publish();
}

/* the JIT path: called from the emitted code with r0 = the regs block {d0, d1, a0, a1}, r1 = the id */
uint32_t a3d_oncall_jit(uint32_t *regs, uint32_t id)
{
    a3d_fw_call(id, regs, 0);
    return regs[0];
}

/* the trap path: the dispatcher saw ONCALL_REQ after the unit that executed the ONCALL */
void a3d_oncall_trap(struct M68KState *m)
{
    uint32_t regs[4] = { BE32(m->D[0].u32), BE32(m->D[1].u32), BE32(m->A[0].u32), BE32(m->A[1].u32) };
    const uint32_t id = m->ONCALL_ID & 0xffffu;
    m->ONCALL_REQ = 0;
    a3d_fw_call(id, regs, 1);
    m->D[0].u32 = BE32(regs[0]);
    m->D[1].u32 = BE32(regs[1]);
}

int a3d_fw_init(void)
{
    if (!a3d_fw_map_heap())
    {
        kprintf("[A3D] heap init FAILED (VA %08x phys %08x): the 3D agent stays down\n",
                A3D_HEAP_FW_VA, A3D_HEAP_DDR_BASE);
        return 0;
    }
    a3d_guest_reset();
    if (!a3d_init(g_fw_backend))
    {
        kprintf("[A3D] agent init FAILED: the 3D agent stays down\n");
        return 0;
    }
    g_fw_up = 1;
    g_ap_touch = a3d_fw_touch_c;
    g_ap_fence_try = a3d_fw_fence_try_c;
    g_ap_fence_wait = a3d_fw_fence_wait_c;
    a3d_fw_counters_zero();
    g_oncall_on = 1;
    const uint32_t be = a3d_backend_id();
    kprintf("[A3D] 3D agent up: backend %s%s, heap %u MB phys %08x at VA %08x, ONCALL %s path, OMMX decode %s\n",
            be == A3D_BE_FABRIC ? "FABRIC" : (be == A3D_BE_SOFT ? "SOFT" : "NULL"),
            (a3d_cnt.backend & 0x100u) ? " (FABRIC asked: no GPU answered at 0xC2000000)" : "",
            A3D_HEAP_SIZE_MB, A3D_HEAP_DDR_BASE, A3D_HEAP_FW_VA,
            g_oncall_trap ? "trap" : "JIT", EnableOMMX ? "ON" : "OFF (ONCALL traps)");
    kprintf("[A3D] wait mode %s, protection %s\n", a3d_wait_mode == A3D_WAIT_EXACT ? "EXACT" : "AUTO",
            a3d_prot_enabled ? "on" : "OFF (lazy waits unprotected)");
    a3d_fw_publish();
    return 1;
}

uint32_t g_a3d_gate_off;
uint32_t g_a3d_wb_off;

uint32_t a3d_fw_wb_early(void)
{
    return a3d_wait_mode == A3D_WAIT_AUTO && a3d_prot_enabled && !g_a3d_wb_off && g_fw_up;
}
static int a3d_fw_sep(char c)
{
    return c == ' ' || c == ',';
}

static int a3d_fw_word(const char *s, const char *w)
{
    for (; *w; s++, w++)
        if (*s != *w)
            return 0;
    return *s == 0 || a3d_fw_sep(*s);
}

void a3d_fw_boot0(const char *args);
__attribute__((noinline)) void a3d_fw_boot0(const char *args)
{
    int off = 0;
    for (const char *s = args; s && *s; )
    {
        while (a3d_fw_sep(*s))
            s++;
        if (a3d_fw_word(s, "ommx"))
            EnableOMMX = 1;
        else if (a3d_fw_word(s, "a3d=soft"))
            g_fw_backend = A3D_BE_SOFT;
        else if (a3d_fw_word(s, "a3d=null"))
            g_fw_backend = A3D_BE_NULL;
        else if (a3d_fw_word(s, "a3d=fabric"))
            g_fw_backend = A3D_BE_FABRIC;
        else if (a3d_fw_word(s, "a3d=off"))
            off = 1;
        else if (a3d_fw_word(s, "oncall=trap"))
            g_oncall_trap = 1;
        else if (a3d_fw_word(s, "oncall=jit"))
            g_oncall_trap = 0;
        else if (a3d_fw_word(s, "a3dwait=exact"))
            a3d_wait_mode = A3D_WAIT_EXACT;
        else if (a3d_fw_word(s, "a3dwait=auto"))
            a3d_wait_mode = A3D_WAIT_AUTO;
        else if (a3d_fw_word(s, "a3dprot=off"))
            a3d_prot_enabled = 0;
        else if (a3d_fw_word(s, "a3dvp=off"))
        {
            a3d_vp_off = 1;
            kprintf("[A3D] the vertex pass off (a3dvp=off): every draw takes the reference loop\n");
        }
        else if (a3d_fw_word(s, "a3dwb=sync"))
        {
            g_a3d_wb_off = 1;
            kprintf("[A3D] WaitBlitter waits for every queued op (a3dwb=sync)\n");
        }
        else if (a3d_fw_word(s, "a3dgate=off"))
        {
            g_a3d_gate_off = 1;
            kprintf("[A3D] RTG blit queue gate OFF (a3dgate=off): 3D fences at the blit's trigger\n");
        }
        while (*s && !a3d_fw_sep(*s))
            s++;
    }
    if (off)
    {
        kprintf("[A3D] 3D agent left down (a3d=off)\n");
        return;
    }
    a3d_fw_init();
}

static const char s_r8_w_softle[] A3D_TAIL_RODATA = "a3d=softle";
static const char s_r8_w_fill[] A3D_TAIL_RODATA = "a3dfill=scalar";
static const char s_r8_w_pbuf[] A3D_TAIL_RODATA = "a3dpbuf=shared";
static const char s_r8_w_state[] A3D_TAIL_RODATA = "a3dstate=full";
static const char s_r8_w_stage[] A3D_TAIL_RODATA = "a3dstage=nc";
static const char s_r8_m_softle[] A3D_TAIL_RODATA = "[A3D] SOFT in RasterIX's little-endian colour (a3d=softle): R5G6B5 "
    "targets take the private buffer, its fill and the present\n";
static const char s_r8_m_fill[] A3D_TAIL_RODATA =
    "[A3D] the private buffer's fill takes the per-pixel loop (a3dfill=scalar)\n";
static const char s_r8_m_pbuf[] A3D_TAIL_RODATA =
    "[A3D] one private buffer a context (a3dpbuf=shared): flips share it and wait\n";
static const char s_r8_m_state[] A3D_TAIL_RODATA =
    "[A3D] every draw's state translated in full, every poll in full (a3dstate=full)\n";
static const char s_r8_m_stage[] A3D_TAIL_RODATA =
    "[A3D] band-list staging non-cacheable under SOFT too (a3dstage=nc): the cleans it skips\n";
static const char s_r8_m_staging[] A3D_TAIL_RODATA = "[A3D] band-list staging %s\n";
static const char s_r8_nc[] A3D_TAIL_RODATA = "non-cacheable";
static const char s_r8_c[] A3D_TAIL_RODATA = "cacheable";

A3D_TAIL_CALL static void a3d_fw_boot_r8(const char *s)
{
    if (a3d_fw_word(s, s_r8_w_softle))
    {
        g_fw_backend = A3D_BE_SOFTLE;
        kprintf("%s", s_r8_m_softle);
    }
    else if (a3d_fw_word(s, s_r8_w_fill))
    {
        a3d_fill_ref_on = 1;
        kprintf("%s", s_r8_m_fill);
    }
    else if (a3d_fw_word(s, s_r8_w_pbuf))
    {
        a3d_pbuf_shared_on = 1;
        kprintf("%s", s_r8_m_pbuf);
    }
    else if (a3d_fw_word(s, s_r8_w_state))
    {
        a3d_state_full_on = 1;
        kprintf("%s", s_r8_m_state);
    }
    else if (a3d_fw_word(s, s_r8_w_stage))
    {
        g_fw_stage_nc_ask = 1;
        kprintf("%s", s_r8_m_stage);
    }
}

A3D_TAIL_CALL void a3d_fw_boot(const char *args)
{
    for (const char *s = args; s && *s; )
    {
        while (a3d_fw_sep(*s))
            s++;
        a3d_fw_boot_r8(s);
        while (*s && !a3d_fw_sep(*s))
            s++;
    }
    a3d_fw_boot0(args);
    if (!g_fw_up)
        return;
    if (a3d_backend_id() == A3D_BE_FABRIC || g_fw_stage_nc_ask)
        a3d_fw_stage_nc();
    kprintf(s_r8_m_staging, g_fw_stage_nc ? s_r8_nc : s_r8_c);
}

void a3d_fw_guest_reset(void)
{
    if (!g_fw_up)
        return;
    a3d_reset_all();
    a3d_fw_prot_reset();
    a3d_guest_reset();
    a3d_fw_publish();
}
