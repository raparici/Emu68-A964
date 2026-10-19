/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* Core #1's JIT pool and the GL arena in the shared PL310 L2 cache (outer write-back, two ways). */

#include <stdint.h>
#include "tlsf.h"
#include "support.h"
#include "amiga_memmap.h"

#if (defined(DE10NANO) && defined(CHIPSET_AXI))

#define L2C_CTRL       0x100u
#define L2C_SYNC       0x730u
#define L2C_INV_PA     0x770u
#define L2C_INV_WAY    0x77Cu
#define L2C_CLEAN_WAY  0x7BCu
#define L2C_CLINV_WAY  0x7FCu
#define L2C_LOCK_D(m)  (0x900u + 8u * (m))
#define L2C_LOCK_I(m)  (0x904u + 8u * (m))

#define L2S            __attribute__((section(".l2c1_tail_text")))
#define L2S_DATA       __attribute__((section(".l2c1_tail_data")))
#define L2S_RO         __attribute__((section(".l2c1_tail_rodata")))

#define POOL_PA        JITPOOL_DDR_BASE
static const uint32_t s_rng_pa[2] L2S_RO = { JITPOOL_DDR_BASE, A3D_MGLC_ARENA_DDR_BASE };
static const uint32_t s_rng_mb[2] L2S_RO = { JITPOOL_SIZE_MB, A3D_MGLC_ARENA_SECTIONS };
#define SCOPE_LINES    ((((uint32_t)JITPOOL_SIZE_MB + A3D_MGLC_ARENA_SECTIONS) << 20) / 32u)
#define ATTR_NC        0x4c06u          /* MMU_CACHED: inner WB-WA, outer non-cacheable (amiga_memmap.h) */
#define ATTR_WB        0x5c06u          /* TEX 101: outer write-back write-allocate, inner WB-WA */

#define ST_ON          0x01u
#define ST_L2OFF       0x10u
#define ST_BADATTR     0x11u
#define ST_BUSY        0x12u
#define ST_NOBOOTINV   0x16u

#define L2C_VA         (0xF2000000u + (0xFFFEF000u - 0xFFC00000u))   /* start_de10nano.c PERIPH_VA(L2C310_PHYS) */
extern uint32_t mmu_table[4096];
#define L2_MMU         mmu_table
#define L2T            __attribute__((noinline))
static inline L2S uint32_t l2c_rd(uint32_t off) { return rd32le(L2C_VA + off); }
static inline L2S void l2c_wr(uint32_t off, uint32_t v) { wr32le(L2C_VA + off, v); }
static L2S void l1d_clean_inv_all(void)    /* core #1's whole L1 data cache by set / way (a3d_fw.c's routine): local */
{
    uint32_t ccsidr;
    __asm__ volatile("mcr p15, 2, %0, c0, c0, 0\n\tisb" :: "r"(0u) : "memory");
    __asm__ volatile("mrc p15, 1, %0, c0, c0, 0" : "=r"(ccsidr));
    const uint32_t ways = (ccsidr >> 3) & 0x3FFu, sets = (ccsidr >> 13) & 0x7FFFu;
    const uint32_t la = ways ? (uint32_t)__builtin_clz(ways) : 0u, lb = (ccsidr & 7u) + 4u;
    __asm__ volatile("dsb" ::: "memory");
    for (uint32_t w = 0; w <= ways; w++)
        for (uint32_t st = 0; st <= sets; st++)
            __asm__ volatile("mcr p15, 0, %0, c7, c14, 2" :: "r"((w << la) | (st << lb)) : "memory");
    __asm__ volatile("dsb" ::: "memory");
}
static L2S void tlb_bp_ic_inv(void)
{
    __asm__ volatile("dsb" ::: "memory");
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 0" :: "r"(0u) : "memory");    /* TLBIALL */
    __asm__ volatile("mcr p15, 0, %0, c7, c5, 6" :: "r"(0u) : "memory");    /* BPIALL  */
    __asm__ volatile("mcr p15, 0, %0, c7, c5, 0" :: "r"(0u) : "memory");    /* ICIALLU */
    __asm__ volatile("dsb\n\tisb" ::: "memory");
}

static uint32_t s_done L2S_DATA;
static uint32_t s_retry L2S_DATA;
static uint32_t s_boot_inv_ok L2S_DATA;     /* 1: this life invalidated the scope by PA before its first use */
static uint32_t s_tick L2S_DATA;

static L2S int l2c_wait_idle(void)          /* a background (way) operation of Linux's: wait it out, bounded */
{
    for (uint32_t k = 0; k < 2000000u; k++)
        if (((l2c_rd(L2C_INV_WAY) | l2c_rd(L2C_CLEAN_WAY) | l2c_rd(L2C_CLINV_WAY)) & 0xFFFFu) == 0u)
            return 1;
    return 0;
}
static L2S void l2c_sync(void)
{
    l2c_wr(L2C_SYNC, 0u);
    for (uint32_t k = 0; k < 1000000u && (l2c_rd(L2C_SYNC) & 1u); k++)
        ;
}
/* one operation a 32-byte line over [pa, pa + bytes); 0 = refused (busy) */
static L2T L2S uint32_t pa_op(uint32_t reg, uint32_t pa, uint32_t bytes)
{
    const uint32_t end = pa + bytes;
    uint32_t n = 0;
    pa &= ~31u;
    while (pa < end)
    {
        if ((n & 0x3FFFu) == 0u && !l2c_wait_idle())     /* every 512 KB: no way operation of Linux's under ours */
            return 0;
        l2c_wr(reg, pa);
        pa += 32u;
        n++;
    }
    l2c_sync();
    return n;
}

static L2S int pool_attrs_are(uint32_t attr)
{
    for (uint32_t r = 0; r < 2u; r++)
        for (uint32_t i = 0; i < s_rng_mb[r]; i++)
            if (L2_MMU[(s_rng_pa[r] >> 20) + i] != ((s_rng_pa[r] + (i << 20)) | attr))
                return 0;
    return 1;
}
static L2S void pool_remap(uint32_t attr)
{
    for (uint32_t r = 0; r < 2u; r++)
    {
        for (uint32_t i = 0; i < s_rng_mb[r]; i++)
            L2_MMU[(s_rng_pa[r] >> 20) + i] = (s_rng_pa[r] + (i << 20)) | attr;
        arm_flush_cache((uintptr_t)&L2_MMU[s_rng_pa[r] >> 20], s_rng_mb[r] * 4u);
    }
    tlb_bp_ic_inv();
}
/* one PA operation over the whole scope; 0 = refused (busy) */
static L2S uint32_t scope_op(uint32_t reg)
{
    const uint32_t a = pa_op(reg, s_rng_pa[0], s_rng_mb[0] << 20);
    const uint32_t b = a ? pa_op(reg, s_rng_pa[1], s_rng_mb[1] << 20) : 0u;
    return b ? a + b : 0u;
}

#define L2_MASTER      1u
#define L2_LOCK        0x3Fu                /* master 1 allocates into ways 6-7 only; Linux keeps all 8 */

static L2T L2S uint32_t l2_on(void)
{
    if (!s_boot_inv_ok)
        return ST_NOBOOTINV;
    if (!(l2c_rd(L2C_CTRL) & 1u))
        return ST_L2OFF;
    if (!pool_attrs_are(ATTR_NC))
        return ST_BADATTR;
    l1d_clean_inv_all();                    /* L1's dirty scope lines to DDR under the non-cacheable attributes */
    if (!scope_op(L2C_INV_PA))              /* a previous life's stale lines: gone */
        return ST_BUSY;
    pool_remap(ATTR_WB);
    l2c_wr(L2C_LOCK_D(L2_MASTER), L2_LOCK);
    l2c_wr(L2C_LOCK_I(L2_MASTER), L2_LOCK);
    l2c_sync();
    return ST_ON;
}

L2T L2S void l2c1_poll(void)
{
    if (s_done)
        return;
    if (s_retry && --s_retry != 0u)
        return;
    const uint32_t st = l2_on();
    s_retry = (st == ST_BUSY || st == ST_NOBOOTINV) ? 256u : 0u;
    s_done = (s_retry == 0u);
}

/* every boot: the scope invalidated by PA before its first use */
L2T L2S void l2c1_boot_invalidate(void)
{
    s_boot_inv_ok = (scope_op(L2C_INV_PA) == SCOPE_LINES);
}

void *__real_tlsf_init_with_memory(void *memory, uintptr_t size);
L2S void *__wrap_tlsf_init_with_memory(void *memory, uintptr_t size)
{
    if ((uintptr_t)memory == POOL_PA)       /* the JIT pool's allocator (start_de10nano.c): the pool's first use */
        l2c1_boot_invalidate();
    return __real_tlsf_init_with_memory(memory, size);
}
int __real_jit_levers_host_tick(void);
L2S int __wrap_jit_levers_host_tick(void)
{
    int r = __real_jit_levers_host_tick();
    if ((++s_tick & 1023u) == 0u)
        l2c1_poll();
    return r;
}
#endif
