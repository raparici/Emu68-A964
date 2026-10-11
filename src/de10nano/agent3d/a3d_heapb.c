/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: the GPU heap's second physical region. */

#include <stdint.h>
#include "a3d_heap.h"
#include "support.h"
#include "amiga_memmap.h"

#if defined(EMU68_A3D) && !defined(__aarch64__)
extern uint32_t mmu_table[4096];
int __real_a3d_heap_init(uint8_t *va, uint32_t phys, uint32_t bytes);
uint32_t __real_a3d_heap_phys(const void *va);
uint8_t *__real_a3d_heap_va(uint32_t phys);
struct a3d_listset;            /* (a3d_agent.h: its float prototypes cannot be included in a GPR-only file) */
const uint8_t *__real_a3d_ls_va(const struct a3d_listset *ls, uint32_t phys, uint32_t bytes);

#define A3D_SPLIT      ((uint32_t)A3D_HEAP_SIZE_MB << 20)          /* the first region's bytes: the seam's VA offset */
#define A3D_B_BYTES    ((uint32_t)A3D_HEAP2_SIZE_MB << 20)
_Static_assert(A3D_SPLIT + A3D_B_BYTES == A3D_HEAP_BYTES, "the two regions are the heap's VA");
_Static_assert(A3D_HEAP_SPAN0_BYTES == A3D_SPLIT, "a list set's region 0 (a3d_agent.c) is the first physical region");
_Static_assert(A3D_POOL_OFF < A3D_SPLIT && A3D_POOL_OFF + A3D_POOL_BYTES == A3D_HEAP_BYTES, "the pool spans the seam");

static uint8_t *s_va;          /* the heap's VA (0: not up) */
static uint32_t s_phys;        /* the first region's phys */
volatile uint32_t g_a3d_heapb_seam;   /* the seam block's VA offset when made (diagnostic; 0 = none) */

int __wrap_a3d_heap_init(uint8_t *va, uint32_t phys, uint32_t bytes)
{
    (void)bytes;               /* (the caller still says A3D_HEAP_SIZE_MB: the first region) */
    s_va = 0;
    g_a3d_heapb_seam = 0;
    const uint32_t vb = (uint32_t)(uintptr_t)va + A3D_SPLIT;
    for (uint32_t i = 0; i < A3D_HEAP2_SIZE_MB; i++)
    {
        mmu_table[(vb >> 20) + i] = (A3D_HEAP2_DDR_BASE + (i << 20)) | 0x1c02u;
        mmu_table[(A3D_HEAP2_DDR_BASE >> 20) + i] = (A3D_HEAP2_DDR_BASE + (i << 20)) | 0x1c02u;
    }
    arm_flush_cache((uintptr_t)&mmu_table[vb >> 20], A3D_HEAP2_SIZE_MB * 4u);
    arm_flush_cache((uintptr_t)&mmu_table[A3D_HEAP2_DDR_BASE >> 20], A3D_HEAP2_SIZE_MB * 4u);
    __asm__ volatile("dsb" ::: "memory");
    __asm__ volatile("mcr p15,0,%0,c8,c7,0" :: "r"(0u));       /* TLBIALL */
    __asm__ volatile("mcr p15,0,%0,c7,c5,6" :: "r"(0u));       /* BPIALL  */
    __asm__ volatile("dsb; isb" ::: "memory");
    if (!__real_a3d_heap_init(va, phys, A3D_HEAP_BYTES))
        return 0;
    uint8_t *a = (uint8_t *)a3d_pool_alloc(A3D_SPLIT - A3D_POOL_OFF - A3D_POOL_ALIGN);
    uint8_t *seam = (uint8_t *)a3d_pool_alloc(A3D_POOL_ALIGN);
    if (a != va + A3D_POOL_OFF || seam != va + A3D_SPLIT - A3D_POOL_ALIGN)
        return 0;              /* (not a fresh pool: refuse rather than hand out a block across the seam) */
    a3d_pool_free(a);
    g_a3d_heapb_seam = A3D_SPLIT - A3D_POOL_ALIGN;
    s_phys = phys;
    s_va = va;
    return 1;
}

uint32_t __wrap_a3d_heap_phys(const void *p)
{
    const uint32_t off = (uint32_t)((const uint8_t *)p - s_va);
    if (s_va == 0 || (const uint8_t *)p < s_va || off >= A3D_HEAP_BYTES)
        return 0;
    return off < A3D_SPLIT ? s_phys + off : A3D_HEAP2_DDR_BASE + (off - A3D_SPLIT);
}

uint8_t *__wrap_a3d_heap_va(uint32_t phys)
{
    if (s_va == 0)
        return 0;
    if (phys >= s_phys && phys - s_phys < A3D_SPLIT)
        return s_va + (phys - s_phys);
    if (phys >= A3D_HEAP2_DDR_BASE && phys - A3D_HEAP2_DDR_BASE < A3D_B_BYTES)
        return s_va + A3D_SPLIT + (phys - A3D_HEAP2_DDR_BASE);
    return 0;
}

const uint8_t *__wrap_a3d_ls_va(const struct a3d_listset *ls, uint32_t phys, uint32_t bytes)
{
    const uint8_t *v = __real_a3d_ls_va(ls, phys, bytes);
    if (v || s_va == 0)
        return v;
    if (phys >= A3D_HEAP2_DDR_BASE && bytes <= A3D_B_BYTES && phys - A3D_HEAP2_DDR_BASE <= A3D_B_BYTES - bytes)
        return s_va + A3D_SPLIT + (phys - A3D_HEAP2_DDR_BASE);
    return 0;
}
#endif
