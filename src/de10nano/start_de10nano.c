/*
    Copyright © 2019 Michal Schulz <michal.schulz@gmx.de>
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* DE10-Nano start-up and the m68k dispatch loop on core #1 of the Cyclone V HPS. */

#include <stdarg.h>
#include <stdint.h>
#include "config.h"
#include "support.h"
#include "tlsf.h"
#include "devicetree.h"
#include "M68k.h"
#include "HunkLoader.h"
#include "DuffCopy.h"
#include "EmuFeatures.h"
#include "amiga_memmap.h"
#include "jit_levers.h"
#include "OMMX.h"

void _start();

#if EMU68_HOST_BIG_ENDIAN
static const char bootstrapName[] = "Emu68-A9 runtime/ARM v7-a BigEndian (DE10-Nano)";
#else
static const char bootstrapName[] = "Emu68-A9 runtime/ARM v7-a LittleEndian (DE10-Nano)";
#endif

extern int  chipset_alive(void);            /* REG_IPL bit3 fresh read (rung-3) */
extern volatile int g_reset_latched;
extern void M68K_FlushCache(void);                                   /* drop all JIT translations */
extern void arm_dcache_invalidate(uintptr_t addr, uint32_t length);
extern void seam_flush_bridge(void);   /* seam-only srst: re-idle arm_avalon on re-boot (chipset_backend_axi.c) */

static uint32_t g_last_rom_hash = 0;
static inline uint32_t rom_hash16(void)
{
    uint32_t h = 0;
    for (int i = 0; i < 16; i++)
        h += *(volatile uint32_t *)(0x00F80000u + (i * 0x8000u));
    return h;
}

static void bootprog(uint32_t v)
{
    *(volatile uint32_t *)LAUNCH_HEALTH_MIRROR_VA = v;
    arm_flush_cache((intptr_t)LAUNCH_HEALTH_MIRROR_VA, 4);
    asm volatile("dsb" ::: "memory");
}

extern const struct BuildID g_note_build_id;

void printf(const char * restrict format, ...)
{
    va_list v;
    va_start(v, format);
    vkprintf(format, v);
    va_end(v);
}

void start_emu(void *);

uint32_t top_of_ram;
extern int __bootstrap_end;
extern uint32_t mmu_table[4096];
extern void * mmu_table_ptr;
extern void * tmp_stack_ptr;

#define HPS_PERIPH_PHYS   0xFFC00000u
#define HPS_PERIPH_VIRT   0xF2000000u   /* section 0xf20; ARM_PERIIOBASE          */
#define HPS_PERIPH_SIZE   0x00400000u
#define HPS_V(phys)       (HPS_PERIPH_VIRT + ((uint32_t)(phys) - HPS_PERIPH_PHYS))

#define SCU_PHYS          0xFFFEC000u   /* Snoop Control Unit                      */
#define GIC_CPU_PHYS      0xFFFEC100u   /* GIC CPU interface                       */
#define GLOBAL_TIMER_PHYS 0xFFFEC200u   /* MPCore global timer                     */
#define PRIV_TIMER_PHYS   0xFFFEC600u   /* per-CPU private timer                   */
#define GIC_DIST_PHYS     0xFFFED000u   /* GIC distributor                         */
#define L2C310_PHYS       0xFFFEF000u   /* PL310 external L2 cache controller      */
#define RSTMGR_PHYS       0xFFD05000u   /* Reset Manager (mpumodrst @ +0x10?)      */
#define SYSMGR_PHYS       0xFFD08000u   /* System Manager (cpu1 start addr)        */

#ifndef AXI_SEAM_BASE
#define AXI_SEAM_BASE     0xC0000000u
#endif
#define AXI_SEAM_SPAN     0x02000000u

#ifdef EMU68_CHIP_WINDOW
volatile uint32_t g_chip_window = 1;
static void chip_window_remap(int on)
{
    extern uint32_t mmu_table[4096];
    if (on) {
        mmu_table[0] = (AXI_SEAM_BASE + 0x00800000u) | MMU_DEVICE | 0x8000u | 0x10u;  /* RO; XN */
        mmu_table[1] = (AXI_SEAM_BASE + 0x00900000u) | MMU_DEVICE | 0x8000u | 0x10u;
    } else {
        mmu_table[0] = MMU_TRAP;   /* chip via the trap/Via-B slow path (compat) */
        mmu_table[1] = MMU_TRAP;
    }
    arm_flush_cache((uintptr_t)&mmu_table[0], 8);
    asm volatile("dsb; mcr p15,0,%0,c8,c7,0; dsb; isb" :: "r"(0));   /* TLBIALL */
}
#endif

#define CPU_RAM_BASE      0x28000000u   /* carveout base = Emu68.img load + CPU#1 entry */
#if   defined(CHIPSET_AXI)
#define CPU_RAM_TOP       0x2A000000u
#else
#define CPU_RAM_TOP       0x40000000u   /* top of the 1 GB DDR; top_of_ram ceiling      */
#endif

static uint32_t a9_secondary_actlr;    /* stashed for the post-serial banner */

static void de10nano_a9_secondary_init(void)
{
    /* (1) Architected L1 D-cache invalidate-by-set/way (CSSELR=0 -> CCSIDR). */
    uint32_t ccsidr, ways, sets, la, lb;
    asm volatile("mcr p15,2,%0,c0,c0,0" :: "r"(0u));
    asm volatile("isb");
    asm volatile("mrc p15,1,%0,c0,c0,0" : "=r"(ccsidr));  /* CCSIDR                */
    ways = (ccsidr >> 3) & 0x3ff;                          /* Associativity-1      */
    sets = (ccsidr >> 13) & 0x7fff;                        /* NumSets-1            */
    la   = __builtin_clz(ways);                            /* way shift (A[31:32-A])*/
    lb   = ((ccsidr & 7) + 4);                             /* set shift (L+4)      */
    for (uint32_t w = 0; w <= ways; w++)
        for (uint32_t s = 0; s <= sets; s++) {
            uint32_t sw = (w << la) | (s << lb);           /* set/way format       */
            asm volatile("mcr p15,0,%0,c7,c6,2" :: "r"(sw)); /* DCISW              */
        }
    asm volatile("dsb");

    /* (2) I-cache, branch predictor and TLB invalidate. */
    asm volatile("mcr p15,0,%0,c7,c5,0" :: "r"(0u));       /* ICIALLU              */
    asm volatile("mcr p15,0,%0,c7,c5,6" :: "r"(0u));       /* BPIALL               */
    asm volatile("mcr p15,0,%0,c8,c7,0" :: "r"(0u));       /* TLBIALL              */
    asm volatile("dsb; isb");

    uint32_t actlr;
    asm volatile("mrc p15,0,%0,c1,c0,1" : "=r"(actlr));
    actlr |= (1u << 6);
    asm volatile("mcr p15,0,%0,c1,c0,1" :: "r"(actlr));
    asm volatile("dsb; isb");

    asm volatile("mrc p15,0,%0,c1,c0,1" : "=r"(a9_secondary_actlr));
}

static void init_scu(void)
{
    kprintf("[BOOT] init_scu: no-op by design (Linux owns the SCU)\n");
}

static void init_l2c310(void)
{
    kprintf("[BOOT] init_l2c310: no-op by design (assume Linux enabled the shared PL310 @ %08x)\n", L2C310_PHYS);
}

static void init_gic(void)
{
    kprintf("[BOOT] init_gic: STUB (dist @ %08x, cpu @ %08x)\n",
            GIC_DIST_PHYS, GIC_CPU_PHYS);
}

void print_build_id(void)
{
    const uint8_t *build_id_data = &g_note_build_id.bid_Data[g_note_build_id.bid_NameLen];
    kprintf("[BOOT] Build ID: ");
    for (unsigned i = 0; i < g_note_build_id.bid_DescLen; ++i)
        kprintf("%02x", build_id_data[i]);
    kprintf("\n");
}

#include "hps_clock.h"
static uint32_t cpu_clock_from_clkmgr;
static void cpu_clock_refresh(void)
{
    const uintptr_t cm = HPS_V(HPS_CLKMGR_PHYS);
    uint32_t r[2][5], hz = 0, pmcr;
    for (int k = 0; k < 2; k++) {
        r[k][0] = rd32le(cm + HPS_CLKMGR_BYPASS);
        r[k][1] = rd32le(cm + HPS_CLKMGR_STAT);
        r[k][2] = rd32le(cm + HPS_CLKMGR_MAINPLL_VCO);
        r[k][3] = rd32le(cm + HPS_CLKMGR_MAINPLL_MPU);
        r[k][4] = rd32le(cm + HPS_CLKMGR_ALTR_MPU);
    }
    if (r[0][0] == r[1][0] && r[0][1] == r[1][1] && r[0][2] == r[1][2] &&
        r[0][3] == r[1][3] && r[0][4] == r[1][4])
        hz = hps_mpu_hz(r[0][0], r[0][1], r[0][2], r[0][3], r[0][4]);
    asm volatile("mrc p15, 0, %0, c9, c12, 0" : "=r"(pmcr));          /* PMCR: the D bit */
    if (hz) {
        const uint32_t old_cntfrq = g_cntfrq_hz;
        g_cpu_hz    = hz;
        g_cntfrq_hz = hps_cntfrq_hz(hz, pmcr);
        cpu_clock_from_clkmgr = 1u;
        if (g_cntfrq_hz != old_cntfrq) jit_bltpace_update();
    }
}

#ifdef EMU68_BLTBUSY_PACE
static uint32_t __attribute__((noinline)) bltpace_old_spin_ticks(void)
{
    uint32_t t0, t1;
    asm volatile("isb\n\tmrc p15, 0, %0, c9, c13, 0" : "=r"(t0) :: "memory");
    for (volatile int _p = 0; _p < (EMU68_BLTBUSY_PACE_N); _p++) ;
    asm volatile("isb\n\tmrc p15, 0, %0, c9, c13, 0" : "=r"(t1) :: "memory");
    return t1 - t0;
}
static void bltpace_boot_measure(void)
{
    uint32_t pmcr, mn = 0xFFFFFFFFu, mx = 0u;
    asm volatile("mrc p15, 0, %0, c9, c12, 0" : "=r"(pmcr));
    for (int k = 0; k < 5; k++) {
        uint32_t t = bltpace_old_spin_ticks();
        if (t < mn) mn = t;
        if (t > mx) mx = t;
    }
    const uint32_t scale = (pmcr & 8u) ? 64u : 1u;                   /* PMCR.D: the counter ticks /64 */
    if (mn != 0u && mn != 0xFFFFFFFFu && mn < 10000000u) {
        g_bltpace_spin_cycles_min = mn * scale;
        g_bltpace_spin_cycles_max = mx * scale;
        g_bltpace_tuned_ns = (mn * scale / 4u) * 5u + ((mn * scale) % 4u) * 5u / 4u;   /* cycles x 1.25 */
    } else {
        g_bltpace_spin_cycles_min = g_bltpace_spin_cycles_max = 0u;
        g_bltpace_tuned_ns = 0u;
    }
    jit_bltpace_update();
    kprintf("[BOOT] Blt Pace: the BBUSY spin = %u cycles (%u..%u, 5 runs) = %u ns at 800 MHz (Tuned); "
            "code %u -> %u ticks at CNTFRQ %u Hz\n", (unsigned)g_bltpace_spin_cycles_min,
            (unsigned)g_bltpace_spin_cycles_min, (unsigned)g_bltpace_spin_cycles_max,
            (unsigned)g_bltpace_tuned_ns, (unsigned)g_bltpace_code, (unsigned)g_bltpace_ticks,
            (unsigned)g_cntfrq_hz);
}
#endif

void boot(uintptr_t dummy, uintptr_t arch, uintptr_t atags, uintptr_t dummy2)
{
    (void)dummy; (void)arch; (void)dummy2;
    uint32_t tmp, initcr;
    uint32_t isar;
    of_node_t *e;

    de10nano_a9_secondary_init();

    asm volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(initcr));
    tmp = initcr;
    tmp |= (1 << 2) | (1 << 12) | (1 << 11);    /* I and D caches, branch prediction */
    tmp = (tmp & ~2) | (1 << 22);               /* Unaligned access enable */
#if EMU68_HOST_BIG_ENDIAN
    tmp |= (1 << 25);                           /* EE bit: BE exceptions + BE page tables */
#endif
    asm volatile ("mcr p15, 0, %0, c1, c0, 0" : : "r"(tmp));
    asm volatile ("mrc p15, 0, %0, c0, c2, 0" : "=r"(isar));

    uint32_t fpsid, MVFR1, MVFR0;
    asm volatile("VMRS %0, FPSID":"=r"(fpsid));
    asm volatile("VMRS %0, MVFR1":"=r"(MVFR1));
    asm volatile("VMRS %0, MVFR0":"=r"(MVFR0));

#if SET_FEATURES_AT_RUNTIME
    if ((isar & 0x0000000f) == 0x00000001) Features.ARM_SUPPORTS_SWP = 1;
    if ((isar & 0x000000f0) == 0x00000010) Features.ARM_SUPPORTS_BITCNT = 1;
    if ((isar & 0x00000f00) == 0x00000100) Features.ARM_SUPPORTS_BITFLD = 1;
    if ((MVFR0 & 0x00f00000) == 0x00100000) Features.ARM_SUPPORTS_SQRT = 1;
    if ((MVFR0 & 0x000f0000) == 0x00010000) Features.ARM_SUPPORTS_VDIV = 1;
    Features.ARM_SUPPORTS_DIV = 0;
#endif

    for (int i = 8; i < 4096 - 8; i++)
        mmu_table[i] = (i << 20) | MMU_DEVICE;

    for (unsigned i = 0; i < (HPS_PERIPH_SIZE >> 20); i++) {
        unsigned sec = (HPS_PERIPH_VIRT >> 20) + i;
        mmu_table[sec] = (HPS_PERIPH_PHYS + (i << 20)) | MMU_DEVICE;
    }

    /* FPGA AXI seam window: identity (virtual == physical), device. */
    for (unsigned i = 0; i < (AXI_SEAM_SPAN >> 20); i++) {
        unsigned sec = (AXI_SEAM_BASE >> 20) + i;
        mmu_table[sec] = ((AXI_SEAM_BASE) + (i << 20)) | MMU_DEVICE;
    }

    arm_flush_cache((intptr_t)mmu_table, sizeof(mmu_table));

#if defined(CHIPSET_AXI)
#else
    setup_serial();
#endif

    kprintf("[BOOT] A9 secondary: L1/I/BP/TLB invalidated; ACTLR readback=%08x (SMP bit6 %s)\n",
            a9_secondary_actlr, (a9_secondary_actlr & (1u << 6)) ? "OK" : "**MISSING**");

    kprintf("[BOOT] Booting %s\n", bootstrapName);
    kprintf("[BOOT] Boot address is %08x\n", _start);
    print_build_id();
    kprintf("[BOOT] ARM stack top at %p\n", tmp_stack_ptr);
    kprintf("[BOOT] Bootstrap ends at %08x\n", &__bootstrap_end);
    kprintf("[BOOT] ISAR=%08x, FPSID=%08x, MVFR0=%08x, MVFR1=%08x\n", isar, fpsid, MVFR0, MVFR1);
    kprintf("[BOOT] Selected features:%s%s%s%s%s%s\n",
        Features.ARM_SUPPORTS_DIV ? " DIV" : " (soft-div)",
        Features.ARM_SUPPORTS_BITFLD ? " BITFLD" : "",
        Features.ARM_SUPPORTS_BITCNT ? " BITCNT" : "",
        Features.ARM_SUPPORTS_SWP ? " SWP":"",
        Features.ARM_SUPPORTS_VDIV ? " VDIV":"",
        Features.ARM_SUPPORTS_SQRT ? " VSQRT":"");
    kprintf("[BOOT] Args=%08x,%08x,%08x,%08x\n", dummy, arch, atags, dummy2);

    memset((void *)&__bootstrap_end, 0, 0xffff0000u - (uintptr_t)&__bootstrap_end);

    /* Local TLSF pool (same window as raspi: bootstrap end .. 0xffff0000). */
    tlsf = tlsf_init();
    tlsf_add_memory(tlsf, &__bootstrap_end, 0xffff0000 - (uintptr_t)&__bootstrap_end);

#if defined(CHIPSET_AXI)
    (void)atags;
#else
    dt_parse((void*)atags);
    e = dt_find_node("/memory");
    if (e) {
        of_property_t *p = dt_find_property(e, "reg");
        if (p) {
            uint32_t *range = p->op_value;
            kprintf("[BOOT] DTB /memory: %p-%p (informational — carveout is pinned)\n",
                    BE32(range[0]), BE32(range[0]) + BE32(range[1]) - 1);
        }
    } else {
        kprintf("[BOOT] No /memory DTB node (expected — the carveout is pinned)\n");
    }
#endif

    for (uint32_t s = CPU_RAM_BASE >> 20; s < CPU_RAM_TOP >> 20; s++)
        mmu_table[s] = (s << 20) | ((s < (FASTRAM_DDR_BASE >> 20)) ? MMU_CACHED : MMU_FASTRAM);
    top_of_ram = CPU_RAM_TOP;

#if defined(EMU68_ZZNET) || defined(EMU68_UAESND)
    mmu_table[0x286] = 0x28600000u | 0x1c02u;
#endif

    for (unsigned i = 0; i < JITPOOL_SIZE_MB; i++)
        mmu_table[(JITPOOL_DDR_BASE >> 20) + i] = (JITPOOL_DDR_BASE + (i << 20)) |
            MMU_CACHED;

#if !defined(CHIPSET_AXI)
    {
        intptr_t kernel_new_loc = top_of_ram - 0x00800000;   /* 0x3F800000 */
        top_of_ram = kernel_new_loc - 0x1000;

        for (int i = 0; i < 8; i++)
            mmu_table[0xff8 + i] = ((kernel_new_loc & 0xfff00000) + (i << 20)) | MMU_CACHED;

        kprintf("[BOOT] Moving kernel %08x -> %p (top of the %08x carveout)\n",
                CPU_RAM_BASE, (void*)kernel_new_loc, CPU_RAM_BASE);
        DuffCopy((void*)(kernel_new_loc + 4), (void*)(CPU_RAM_BASE + 4), 0x00800000 / 4 - 1);
        arm_flush_cache(kernel_new_loc, 0x00800000);
        arm_flush_cache((intptr_t)mmu_table, sizeof(mmu_table));

        asm volatile("dsb; mcr p15,0,%0,c2,c0,0; dsb; isb"
                     :: "r"(((uint32_t)mmu_table_ptr & 0x007fffff) | (kernel_new_loc & 0xfff00000)));
        asm volatile("dsb; mcr p15,0,%0,c8,c7,0; dsb; isb" :: "r"(0));   /* flush TLB */
    }
#else
    (void)mmu_table_ptr;
#endif

    init_scu();
    init_l2c310();
    init_gic();

    {
        extern void init_vectors(void);
        extern void ps_setup_protocol(void);
        extern void ps_pulse_reset(void);

        for (unsigned s = 0; s <= SEC_ROM; s++)
            mmu_table[s] = MMU_TRAP;

        for (unsigned s = 0x010; s <= 0x27F; s++)
            mmu_table[s] = MMU_TRAP;

#ifdef EMU68_CHIP_WINDOW
        chip_window_remap(g_chip_window);
#endif

        for (unsigned s = SEC_CPURAM_BASE; s < SEC_FAST_TOP; s++)
            mmu_table[s] = (s == SEC_CPURAM_BASE)
                         ? MMU_TRAP
                         : ((FASTRAM_DDR_BASE + ((s - SEC_CPURAM_BASE) << 20)) | MMU_FASTRAM);

        for (unsigned i = 0; i < Z3FAST_SIZE_MB; i++)
            mmu_table[(Z3FAST_M68K_BASE >> 20) + i] =
                (Z3FAST_DDR_BASE + (i << 20)) | MMU_FASTRAM;
        kprintf("[BOOT] Z3 CPU RAM: m68k %08x (backing phys %08x, %u MB)\n",
                Z3FAST_M68K_BASE, Z3FAST_DDR_BASE, Z3FAST_SIZE_MB);

        mmu_table[SHARE_FW_VA >> 20] = (SHARE_DDR_BASE & 0xFFF00000u) | 0x1c02u;
        kprintf("[BOOT] shared-folder mailbox: m68k %08x -> phys %08x (VA %08x, NC)\n",
                SHARE_M68K_BASE, SHARE_DDR_BASE, SHARE_FW_VA);

#ifdef EMU68_SHARE_L2
        {
            static uint32_t share_l2[256] __attribute__((aligned(1024)));
            uint32_t ttbr0;
            asm volatile("mrc p15,0,%0,c2,c0,0" : "=r"(ttbr0));
            uint32_t delta = (ttbr0 & 0xFFFFC000u) - (uint32_t)(uintptr_t)mmu_table;

            uint32_t sec  = SHARE_M68K_BASE >> 20;               /* 0x0D */
            uint32_t page = (SHARE_M68K_BASE >> 12) & 0xFFu;     /* flag page 0xD4    */
            uint32_t dpage = page + 1u;                          /* data page  0xD5   */
            uint32_t dphys = (SHARE_DDR_BASE & 0xFFFFF000u) + 0x1000u;  /* phys 0x27FF5000 */
            for (unsigned i = 0; i < 256; i++)
                share_l2[i] = (i == dpage) ? ((dphys & 0xFFFFF000u) | 0x72u) : 0u;

            uint32_t l2_pa = (uint32_t)(uintptr_t)share_l2 + delta;
            if ((l2_pa & 0x3FFu) == 0) {
                arm_flush_cache((uintptr_t)share_l2, sizeof(share_l2));
                asm volatile("dsb" ::: "memory");
                /* coarse L2 pointer: bits[31:10]=table, [1:0]=01, domain 0 (seam domain). */
                mmu_table[sec] = (l2_pa & 0xFFFFFC00u) | 0x1u;
                arm_flush_cache((uintptr_t)&mmu_table[sec], 4);
                asm volatile("mcr p15,0,%0,c8,c7,0" :: "r"(0) : "memory");   /* TLBIALL */
                asm volatile("dsb; isb" ::: "memory");
                kprintf("[BOOT] share: data page %08x -> phys %08x direct-NC (flag/request page still traps)\n",
                        SHARE_M68K_BASE + 0x1000u, dphys);
            }
        }
#endif

#if defined(CHIPSET_AXI)
        mmu_table[SEC_ROM] = ROM_DDR_BASE | MMU_CACHED_RO;
#endif

        arm_flush_cache((intptr_t)mmu_table, sizeof(mmu_table));
        asm volatile("dsb; mcr p15,0,%0,c8,c7,0; dsb; isb" :: "r"(0));   /* flush TLB */
        {
            volatile uint32_t *mb = (volatile uint32_t *)SHARE_MBOX_FW_PTR;
#ifdef EMU68_SHARE_DOORBELL
            mb[SHARE_DB_FW_CAP_WORD] = SHARE_DB_FW_CAP;
#else
            mb[SHARE_DB_FW_CAP_WORD] = 0u;
#endif
            asm volatile("dsb" ::: "memory");
        }

#if defined(CHIPSET_AXI) && defined(EMU68_EXTROM_1MB)
        {
            static uint32_t ext_l2[256] __attribute__((aligned(1024)));
            volatile uint32_t *dsc =
                (volatile uint32_t *)(LAUNCH_HEALTH_MIRROR_VA + EXTROM_DESC_OFFSET);

            arm_flush_cache((uintptr_t)dsc, 16);
            asm volatile("dsb" ::: "memory");
            /* core#0 writes these LE, same protocol as the JIT-cache control word at +0xB0C */
            uint32_t xmagic = __builtin_bswap32(dsc[0]);
            uint32_t xbase  = __builtin_bswap32(dsc[1]);
            uint32_t xsize  = __builtin_bswap32(dsc[2]);
            uint32_t xm68k  = __builtin_bswap32(dsc[3]);

            if (xmagic == EXTROM_DESC_MAGIC
                && xm68k == (SEC_AUTOCONF << 20)
                && xsize == EXTROM_SIZE
                && xbase == EXTROM_DDR_BASE
                && (xbase & 0xFFFFFu) == 0)
            {
                uint32_t ttbr0;
                asm volatile("mrc p15,0,%0,c2,c0,0" : "=r"(ttbr0));
                uint32_t delta  = (ttbr0 & 0xFFFFC000u) - (uint32_t)(uintptr_t)mmu_table;
                uint32_t l2_pa  = (uint32_t)(uintptr_t)ext_l2 + delta;
                uint32_t rodesc = MMU_CACHED_RO;
                uint32_t small  = 0x2u
                                | ((rodesc >> 4) & 1u)              /* XN      4 -> 0    */
                                | (rodesc & 0x0Cu)                  /* C,B  stay 3:2     */
                                | (((rodesc >> 10) & 3u) << 4)      /* AP  11:10 -> 5:4  */
                                | (((rodesc >> 12) & 7u) << 6)      /* TEX 14:12 -> 8:6  */
                                | (((rodesc >> 15) & 1u) << 9)      /* APX    15 -> 9    */
                                | (((rodesc >> 16) & 1u) << 10)     /* S      16 -> 10   */
                                | (((rodesc >> 17) & 1u) << 11);    /* nG     17 -> 11   */

                if ((l2_pa & 0x3FFu) == 0) {
                    for (unsigned i = 0; i < 256; i++)
                        ext_l2[i] = (i < (EXTROM_SIZE >> 12))
                                  ? ((xbase + (i << 12)) | small)
                                  : 0u;      /* $E80000+ : FAULT, autoconfig as today */

                    uint32_t saved = mmu_table[SEC_AUTOCONF];
                    arm_flush_cache((uintptr_t)ext_l2, sizeof(ext_l2));
                    asm volatile("dsb" ::: "memory");
                    mmu_table[SEC_AUTOCONF] = (l2_pa & 0xFFFFFC00u)
                                            | (mmu_table[SEC_ROM] & 0x1E0u)  /* same domain */
                                            | 0x1u;                          /* coarse table */
                    arm_flush_cache((uintptr_t)&mmu_table[SEC_AUTOCONF], 4);
                    asm volatile("dsb; mcr p15,0,%0,c8,c7,0; dsb; isb" :: "r"(0) : "memory");

                    uint32_t hdr = *(volatile uint32_t *)((uintptr_t)(SEC_AUTOCONF << 20));
                    if (hdr == 0x11144EF9u) {
                        kprintf("[BOOT] ext ROM: m68k $E00000-$E7FFFF <- phys %08x "
                                "(L2 %08x, page attr %03x), header %08x OK\n",
                                xbase, l2_pa, small, hdr);
                    } else {
                        mmu_table[SEC_AUTOCONF] = saved;
                        arm_flush_cache((uintptr_t)&mmu_table[SEC_AUTOCONF], 4);
                        asm volatile("dsb; mcr p15,0,%0,c8,c7,0; dsb; isb" :: "r"(0) : "memory");
                        kprintf("[BOOT] ext ROM REFUSED: $E00000 reads %08x, expected "
                                "11144EF9 — window withdrawn, section $E left trapped\n", hdr);
                    }
                } else {
                    kprintf("[BOOT] ext ROM REFUSED: L2 table at %08x is not 1 KB aligned\n",
                            l2_pa);
                }
            } else if (xmagic != 0u) {
                kprintf("[BOOT] ext ROM descriptor REJECTED: magic %08x base %08x size %08x "
                        "m68k %08x — section $E left trapped\n", xmagic, xbase, xsize, xm68k);
            }
        }
#endif

#if defined(CHIPSET_AXI)
        extern void *jit_tlsf;
        jit_tlsf = tlsf_init_with_memory((void *)(uintptr_t)JITPOOL_DDR_BASE,
                                         (uintptr_t)JITPOOL_SIZE_MB << 20);
        kprintf("[BOOT] JIT pool: %u MB @ phys %08x (dedicated jit_tlsf=%p)\n",
                JITPOOL_SIZE_MB, JITPOOL_DDR_BASE, jit_tlsf);
#endif

        init_vectors();         /* ARMv7 vector table + VBAR + abort/undef/irq stacks */
        ps_setup_protocol();    /* arm the chipset transport (AXI seam / doorbell) */
        ps_pulse_reset();       /* assert chipset reset (FPGA strobe / doorbell)   */
        kprintf("[BOOT] CHIPSET_AXI seam armed: chip RAM $0-$1FFFFF + $BFxxxx + $DFxxxx -> FPGA;"
                " Fast RAM $200000+ -> carveout %08x; ROM $F80000 cached-RO\n", CPU_RAM_BASE);
    }

    {
        uint32_t rom_ssp = BE32(*(volatile uint32_t *)0x00F80000u);
        uint32_t rom_pc  = BE32(*(volatile uint32_t *)0x00F80004u);
        kprintf("[BOOT] DE10: boot from reset — ROM@$F80000 SSP=%08x PC=%08x\n",
                rom_ssp, rom_pc);

        bootprog(0xB0070001);
        {
            int spins = 0;
            while (!chipset_alive()) {
                for (volatile int i = 0; i < 100000; i++) ;      /* ~1 ms */
                if ((++spins % 2000) == 0)
                    kprintf("[BOOT] still waiting for the chipset to leave reset (bit3=0)...\n");
                if (spins > 15000) {   /* ~15 s: start anyway, loudly */
                    kprintf("[BOOT] chipset STILL in reset after ~15 s — starting the m68k anyway\n");
                    break;
                }
            }
        }
        bootprog(0xB0070002);
        kprintf("[BOOT] chipset alive (bit3=1) — starting the m68k\n");
#ifdef EMU68_UAESND
        { extern void uaesnd_ring_arm(void); uaesnd_ring_arm(); }
        kprintf("[uaesnd] ring armed at %08x (%u KB), ctrl %08x\n",
                (uint32_t)UAESND_RING_DDR, (uint32_t)(UAESND_RING_BYTES >> 10),
                (uint32_t)UAESND_CTRL_DDR);
#endif
        cpu_clock_refresh();
        kprintf("[BOOT] CPU clock %u Hz (%s), CNTFRQ %u Hz\n", (unsigned)g_cpu_hz,
                cpu_clock_from_clkmgr ? "clock manager" : "build default -- clock manager unreadable",
                (unsigned)g_cntfrq_hz);
#ifdef EMU68_BLTBUSY_PACE
        bltpace_boot_measure();
#endif
#ifdef EMU68_DEVICETREE
        { extern void dtree_board_init(void); dtree_board_init(); }
#endif
        g_last_rom_hash = rom_hash16();   /* baseline so a same-ROM config-reload is NOT flushed */
        start_emu((void *)(intptr_t)rom_pc);
        while (1);
    }
}

void print_context(struct M68KState *m68k)
{
    printf("\nM68K Context:\n");
    for (int i=0; i < 8; i++) {
        if (i==4) printf("\n");
        printf("    D%d = 0x%08x", i, BE32(m68k->D[i].u32));
    }
    printf("\n");
    for (int i=0; i < 8; i++) {
        if (i==4) printf("\n");
        printf("    A%d = 0x%08x", i, BE32(m68k->A[i].u32));
    }
    printf("\n");
    printf("    PC = 0x%08x    SR = ", BE32((int)m68k->PC));
    uint16_t sr = BE16(m68k->SR);
    printf("%c%c%c%c%c\n",
           (sr & SR_X) ? 'X' : '.', (sr & SR_N) ? 'N' : '.',
           (sr & SR_Z) ? 'Z' : '.', (sr & SR_V) ? 'V' : '.',
           (sr & SR_C) ? 'C' : '.');
    printf("    USP= 0x%08x    MSP= 0x%08x    ISP= 0x%08x\n",
           BE32(m68k->USP.u32), BE32(m68k->MSP.u32), BE32(m68k->ISP.u32));
}

uint32_t last_PC = 0xffffffff;

/* armhf-port: glue.c defines __m68k_state and M68K_SetStatePtr. */
#ifndef __aarch64__
extern void M68K_SetStatePtr(struct M68KState *s);
#endif

#ifndef EMU68_BLOCKSPIN
#define EMU68_BLOCKSPIN 0
#endif
volatile uint32_t g_block_spin = (EMU68_BLOCKSPIN);

int g_blitter_busy = 0;
/* (the JITMODE_* codes, the parked latch and jitmode_verifies(): jit_levers.h / jit_levers.c) */

static inline int jit_nocache(struct M68KState *m68k)
{
    uint32_t m = g_jit_mode;
#ifdef EMU68_BLITFAIR
    if (__builtin_expect(JITMODE_IS_CACHED(m), 1))
        return g_blitter_busy && (BE32((uint32_t)m68k->PC) < 0x00200000u);
    if (m == JITMODE_VERIFY) return 0;
#else
    if (__builtin_expect(JITMODE_IS_CACHED(m) || m == JITMODE_VERIFY, 1)) return 0;
#endif
    if (m == JITMODE_NOCACHE) return 1;
    return BE32((uint32_t)m68k->PC) < 0x00200000u;
}

#ifdef CHIPSET_BACKEND
extern int  chipset_pending_ipl(void);
extern void chipset_ack_ipl(int level);

extern unsigned int ps_read_8(unsigned int address);
extern unsigned int ps_read_16(unsigned int address);
extern unsigned int ps_read_32(unsigned int address);
extern void ps_write_8(unsigned int address, unsigned int data);
extern void ps_write_16(unsigned int address, unsigned int data);
extern void ps_write_32(unsigned int address, unsigned int data);
static inline uint32_t guest_read_32(uint32_t a) {
    return (a < 0x200000u) ? ps_read_32(a) : BE32(*(volatile uint32_t *)(uintptr_t)a);
}
static inline uint16_t guest_read_16(uint32_t a) {
    return (a < 0x200000u) ? (uint16_t)ps_read_16(a) : BE16(*(volatile uint16_t *)(uintptr_t)a);
}
static inline void guest_write_32(uint32_t a, uint32_t v) {
    if (a < 0x200000u) ps_write_32(a, v); else *(volatile uint32_t *)(uintptr_t)a = BE32(v);
}
static inline void guest_write_16(uint32_t a, uint16_t v) {
    if (a < 0x200000u) ps_write_16(a, v); else *(volatile uint16_t *)(uintptr_t)a = BE16(v);
}

int ommx_guest_read32(uint32_t a, uint32_t *v)
{
    if (a < 0x200000u) {
        if (a > 0x200000u - 4u) return 0;
        *v = guest_read_32(a);
        return 1;
    }
    if ((a >> 20) != ((a + 3u) >> 20)) return 0;
    uint32_t d = mmu_table[a >> 20];
    if ((d & 0x77FFFu) != (MMU_FASTRAM & 0x77FFFu)) return 0;     /* a section, TEX/C/B/AP as Fast RAM (AP[2] ignored) */
    *v = BE32(*(volatile uint32_t *)(uintptr_t)a);
    return 1;
}

static void take_m68k_interrupt(struct M68KState *m68k, int level)
{
    uint32_t vbr = BE32(m68k->VBR);
    uint32_t handler = guest_read_32(vbr + 0x60 + level * 4);
    if (handler == 0) {
        static int nullvec_warns = 4;
        if (nullvec_warns > 0)
            kprintf("[JIT:INT] *** level %d autovector is 0 (VBR=%08x) — interrupt NOT taken ***%s\n",
                    level, vbr, (--nullvec_warns == 0) ? " (further warnings silenced)" : "");
        return;
    }

    uint16_t sr = BE16(m68k->SR);
    uint32_t pc = BE32(m68k->PC);

    if (!(sr & SR_S)) {
        m68k->USP.u32  = m68k->A[7].u32;
        m68k->A[7].u32 = m68k->ISP.u32;
    } else if (sr & SR_M) {
        m68k->MSP.u32  = m68k->A[7].u32;
        m68k->A[7].u32 = m68k->ISP.u32;
    }

    uint32_t sp = BE32(m68k->A[7].u32);

    sp -= 2; guest_write_16(sp, (uint16_t)(0x60 + level * 4));   /* format 0 | autovector offset */
    sp -= 4; guest_write_32(sp, pc);
    sp -= 2; guest_write_16(sp, sr);
    m68k->A[7].u32 = BE32(sp);

    uint16_t newsr = (uint16_t)((((sr & ~SR_IPL) | (level << 8)) | SR_S) & ~(SR_T0 | SR_T1 | SR_M));
    m68k->SR = BE16(newsr);
    m68k->PC = BE32(handler);

    m68k->STOP_REQ = 0;

    static int int_trace = 4;
    if (int_trace > 0)
        kprintf("[JIT:INT] IPL=%d: frame@%08x savedPC=%08x savedSR=%04x -> handler=%08x newSR=%04x%s\n",
                level, sp, pc, sr, handler, newsr,
                (--int_trace == 0) ? " (further IPL traces silenced; -DEMU68_INT_TRACE for all)" : "");
}

#ifdef EMU68_CPU040
extern int CpuModel;
#endif
void m68k_do_rte(struct M68KState *m68k)
{
    uint32_t sp = BE32(m68k->A[7].u32);
    uint16_t sr = guest_read_16(sp);
    uint32_t pc = guest_read_32(sp + 2);

    {
        uint16_t fmtword = guest_read_16(sp + 6);
        switch (fmtword >> 12)
        {
            case 0:  sp += 8;  break;                  /* short frame                      */
            case 1:  sp += 8;  break;
            case 2:
            case 3:  sp += 12; break;                  /* +instruction/effective address   */
            case 4:  sp += 16; break;
#ifdef EMU68_CPU040
            case 7:
                if (CPUMODEL_HAS_040_REGS(CpuModel)) { sp += 60; break; }
                /* fallthrough to the loud refuse when presenting an 020 */
#endif
            default:
                kprintf("[JIT:RTE] *** UNKNOWN exception frame format %d (word %04x) at SSP %08x "
                        "— refusing to pop ***\n", fmtword >> 12, fmtword, sp);
                m68k->PC = 0;                          /* stops start_emu, beacon-visible  */
                return;
        }
    }

    m68k->SR = BE16(sr);
    if (!(sr & SR_S)) {
        m68k->ISP.u32  = BE32(sp);
        m68k->A[7].u32 = m68k->USP.u32;
    } else if (sr & SR_M) {
        m68k->ISP.u32  = BE32(sp);
        m68k->A[7].u32 = m68k->MSP.u32;
    } else {
        m68k->A[7].u32 = BE32(sp);
    }
    m68k->PC = BE32(pc);
    ommx_banks_switch_point(m68k, 0);
}

void m68k_do_move_to_sr(struct M68KState *m68k)
{
    uint16_t oldsr = BE16(m68k->SR);
    uint16_t newsr = (uint16_t)(m68k->MOVE2SR_VAL & 0xf71f);
    int old_s = (oldsr & SR_S) != 0, old_m = (oldsr & SR_M) != 0;
    int new_s = (newsr & SR_S) != 0, new_m = (newsr & SR_M) != 0;
    if (old_s != new_s || old_m != new_m) {
        if      (!old_s) m68k->USP.u32 = m68k->A[7].u32;
        else if ( old_m) m68k->MSP.u32 = m68k->A[7].u32;
        else             m68k->ISP.u32 = m68k->A[7].u32;
        if      (!new_s) m68k->A[7].u32 = m68k->USP.u32;
        else if ( new_m) m68k->A[7].u32 = m68k->MSP.u32;
        else             m68k->A[7].u32 = m68k->ISP.u32;
    }
    m68k->SR = BE16(newsr);
    m68k->MOVE2SR_REQ = 0;
    if (old_s && !new_s)
        ommx_banks_switch_point(m68k, 1);
}

#endif /* CHIPSET_BACKEND */

extern int DisableFPU;
#ifdef EMU68_OMMX
extern int EnableOMMX;
#endif
#ifdef EMU68_CPU040
extern int CpuModel;
#endif

static int feat_desc_apply(int at_boot)
{
    volatile uint32_t *dsc = (volatile uint32_t *)(LAUNCH_HEALTH_MIRROR_VA + FEAT_DESC_OFFSET);
    arm_flush_cache((uintptr_t)dsc, 16);
    asm volatile("dsb" ::: "memory");
    uint32_t fmagic = __builtin_bswap32(dsc[0]);
    uint32_t feats  = __builtin_bswap32(dsc[1]);
    uint32_t feats2 = __builtin_bswap32(dsc[2]);
    uint32_t ok     = (fmagic == FEAT_DESC_MAGIC);
    int changed = 0;
#ifdef EMU68_FPU
    {
        int want_off = (ok && (feats & FEAT_FPU_OFF)) ? 1 : 0;
        if (DisableFPU != want_off) { DisableFPU = want_off; changed = 1; }
    }
#endif
#ifdef EMU68_OMMX
    {
        int want_on = (ok && (feats & FEAT_OMMX_DECODE)) ? 1 : 0;
        if (EnableOMMX != want_on) { EnableOMMX = want_on; changed = 1; }
    }
#endif
#ifdef EMU68_CPU040
    {
        int want_model = ok ? ((feats & FEAT_CPU_68060) ? CPUMODEL_68060
                                : ((feats & FEAT_CPU_68040) ? CPUMODEL_68040 : CPUMODEL_68EC020))
                            : EMU68_CPU040_DEFAULT;
        if (CpuModel != want_model) { CpuModel = want_model; changed = 1; }
#ifdef EMU68_FP_RESIDENT
        { extern void fpresident_board_enable(int on);
          fpresident_board_enable(CPUMODEL_HAS_040_REGS(CpuModel)
                                  && !(ok && (feats & FEAT_FP_RESIDENT_OFF))); }
#endif
    }
#endif
#ifdef EMU68_ZZ9000
    {
        extern void zz9000_board_enable(int on);
        extern volatile uint32_t g_zz9000_off;
        int want_off = (ok && (feats & FEAT_RTG_OFF)) ? 1 : 0;
        if ((int)g_zz9000_off != want_off) { zz9000_board_enable(!want_off); changed = 1; }
    }
#endif
#ifdef EMU68_UAESND
    {
        extern void uaesnd_board_enable(int on);
        extern void uaesnd_ring_arm(void);
        extern void uaesnd_ring_disarm(void);
        extern volatile uint32_t g_uaesnd_off;
        int want_off = (ok && (feats & FEAT_AHI_OFF)) ? 1 : 0;
        if ((int)g_uaesnd_off != want_off) { uaesnd_board_enable(!want_off); changed = 1; }
        if (want_off) uaesnd_ring_disarm(); else uaesnd_ring_arm();
    }
#endif
#ifdef EMU68_MHI
    {
        extern void mhi_fw_enable(int on);
        extern volatile uint32_t g_zz9000_off, g_uaesnd_off;
        mhi_fw_enable(g_zz9000_off == 0u && g_uaesnd_off == 0u);
    }
#endif
#ifdef EMU68_DEVICETREE
    {
        extern int dtree_board_enable(int on);
        if (dtree_board_enable((int)feat2_dtree_offered(feats2, ok))) changed = 1;
    }
#endif
#ifdef EMU68_CHIP_WINDOW
    {
        int want_win = (ok && (feats & FEAT_CHIP_COMPAT)) ? 0 : 1;
        if ((int)g_chip_window != want_win) { g_chip_window = want_win; chip_window_remap(want_win); }
    }
#endif
    if (jit_levers_apply_feats2(feats, feats2, ok)) changed = 1;
    kprintf("[%s] FEAT_DESC magic %08x feats %08x -> 68882 %s, OMMX %s%s\n",
            at_boot ? "BOOT" : "REBOOT", fmagic, feats,
            DisableFPU ? "OFF" : "on",
#ifdef EMU68_OMMX
            EnableOMMX ? "ON (decode $FC00-$FFFF)" : "off",
#else
            "not built",
#endif
            (!at_boot && changed) ? " - CHANGED: flushing the JIT" : "");
#ifdef EMU68_CPU040
    kprintf("[%s] CPU personality: %s (model %d)\n", at_boot ? "BOOT" : "REBOOT",
            CPUMODEL_NAME(CpuModel), CpuModel);
#endif
    {
        extern int JitStatsOff;
        kprintf("[%s] boards: RTG %s, bigmigAHI %s, JIT counters %s\n",
                at_boot ? "BOOT" : "REBOOT",
#ifdef EMU68_ZZ9000
                ({ extern volatile uint32_t g_zz9000_off; g_zz9000_off ? "ABSENT" : "on"; }),
#else
                "not built",
#endif
#ifdef EMU68_UAESND
                ({ extern volatile uint32_t g_uaesnd_off; g_uaesnd_off ? "ABSENT" : "on"; }),
#else
                "not built",
#endif
                JitStatsOff ? "OFF" : "on");
    }
#ifdef EMU68_MHI
    kprintf("[%s] MHI decoder: %s\n", at_boot ? "BOOT" : "REBOOT",
            ({ extern volatile uint32_t g_mhi_off; g_mhi_off ? "ABSENT (needs ZZ9000 RTG and BigMig AHI/MHI ON)"
                                                             : "on (inside the ZZ9000 window)"; }));
#endif
#ifdef EMU68_DEVICETREE
    kprintf("[%s] devicetree.resource board: %s\n", at_boot ? "BOOT" : "REBOOT",
            ({ extern volatile uint32_t g_dtree_off; g_dtree_off ? "ABSENT (OSD Emu68 DT OFF, the default)"
                                                                 : "offered (OSD Emu68 DT ON)"; }));
#endif
    kprintf("[%s] JIT unit depth: %u m68k insns/unit%s, IPL poll every %d unit exits"
            " -> worst-case IPL latency ~%u insns\n", at_boot ? "BOOT" : "REBOOT",
            (unsigned)Options.M68K_TRANSLATION_DEPTH,
            Options.M68K_TRANSLATION_DEPTH == (uint32_t)EMU68_M68K_INSN_DEPTH ? " (default)" : " (COMPAT)",
            g_ipl_poll_interval,
            (unsigned)Options.M68K_TRANSLATION_DEPTH * (unsigned)g_ipl_poll_interval);
#ifdef EMU68_INNER_LOOP
    kprintf("[%s] in-unit loop back-edge %s: an inner loop yields to the dispatcher every %u passes\n",
            at_boot ? "BOOT" : "REBOOT", g_jit_backedge_off ? "OFF (JITCTRL2 bit 2 / OSD InnerLoop)" : "on",
            (unsigned)g_ipl_poll_interval * M68K_LOOP_BUDGET_PER_POLL);
#endif
    kprintf("[%s] CCR optimiser: scan depth %u%s, unit-end clamp %s\n",
            at_boot ? "BOOT" : "REBOOT", (unsigned)Options.M68K_CCR_SCAN_DEPTH,
            Options.M68K_CCR_SCAN_DEPTH == (uint32_t)EMU68_CCR_SCAN_DEPTH ? " (default)"
            : (Options.M68K_CCR_SCAN_DEPTH == 0u ? " - DISABLED (CCRD=0)" : " (CCRD)"),
            EMU68_CCR_BREAK_AT_UNIT_END ? "on" : "OFF");
    return changed;
}

#if (EMU68_BLOCKSPIN != 0) || !defined(EMU68_FAST_DISPATCH)
#define FASTDISPATCH_ALLOWED 0
#else
#define FASTDISPATCH_ALLOWED 1
#endif
#ifdef EMU68_ZZNET
extern volatile uint32_t g_zznet_active;
extern int zz_idle_pending_ipl(void);
#endif
#if FASTDISPATCH_ALLOWED
_Static_assert(__builtin_offsetof(struct M68KState, PC) == 76,          "trampoline: PC offset");
_Static_assert(__builtin_offsetof(struct M68KState, MOVE2SR_REQ) == 240,"trampoline: MOVE2SR_REQ offset");
_Static_assert(__builtin_offsetof(struct M68KState, STOP_REQ) == 244,   "trampoline: STOP_REQ offset");
_Static_assert(__builtin_offsetof(struct M68KState, CACR_REQ) == 252,   "trampoline: CACR_REQ offset");
_Static_assert(__builtin_offsetof(struct M68KTranslationUnit, mt_M68kAddress) == 16,  "trampoline: mt_M68kAddress");
_Static_assert(__builtin_offsetof(struct M68KTranslationUnit, mt_ARMEntryPoint) == 64, "trampoline: mt_ARMEntryPoint");
_Static_assert(sizeof(struct List) == 12, "trampoline: struct List stride");
extern struct List *ICache;
unsigned fast_dispatch_run(void *entry);
asm(
"   .text                                   \n"
"   .align 4                                \n"
"   .global fast_dispatch_run               \n"
"   .type   fast_dispatch_run, %function    \n"
"fast_dispatch_run:                         \n"
"   push    {r4, lr}                        \n"
"   mov     r4, #0                          \n"   /* units run */
".Lfd_run:                                  \n"
"   blx     r0                              \n"   /* the unit; returns here with ctx->PC written */
"   add     r4, r4, #1                      \n"
"   ldr     ip, [fp, #76]                   \n"   /* next PC (native) */
"   cmp     ip, #0                          \n"   /* PC 0: C's reboot logic */
"   beq     .Lfd_ret                        \n"
"   cmn     ip, #2                          \n"   /* 0xFFFFFFFE: RTE is done in C */
"   beq     .Lfd_ret                        \n"
"   ldr     r0, [fp, #240]                  \n"   /* MOVE2SR_REQ */
"   ldr     r1, [fp, #244]                  \n"   /* STOP_REQ    */
"   ldr     r2, [fp, #252]                  \n"   /* CACR_REQ    */
"   orr     r0, r0, r1                      \n"
"   orrs    r0, r0, r2                      \n"
"   bne     .Lfd_ret                        \n"
"   ldr     r2, =ipl_poll_countdown         \n"
"   ldr     r1, [r2]                        \n"
"   subs    r1, r1, #1                      \n"
"   str     r1, [r2]                        \n"
"   ble     .Lfd_ret                        \n"   /* C's turn: IPL poll & the rest */
"   eor     r1, ip, ip, lsr #16             \n"   /* ICache hash, as M68K_FindTranslationUnit */
"   uxth    r1, r1                          \n"
"   ldr     r2, =ICache                     \n"
"   ldr     r2, [r2]                        \n"
"   add     r2, r2, r1, lsl #3              \n"
"   add     r2, r2, r1, lsl #2              \n"   /* &ICache[hash]  (12-byte List) */
"   ldr     r3, [r2]                        \n"   /* lh_Head */
"   ldr     r0, [r3]                        \n"   /* ln_Succ: 0 => list empty (head is the tail sentinel) */
"   cmp     r0, #0                          \n"
"   beq     .Lfd_ret                        \n"
"   ldr     r0, [r3, #16]                   \n"   /* mt_M68kAddress */
"   cmp     r0, ip                          \n"
"   bne     fast_dispatch_nothead_e         \n"
"   ldr     r0, [r3, #64]                   \n"   /* mt_ARMEntryPoint */
"   b       .Lfd_run                        \n"
".Lfd_ret:                                  \n"
"   mov     r0, r4                          \n"
"   pop     {r4, pc}                        \n"
"   .ltorg                                  \n"
"   .size   fast_dispatch_run, .-fast_dispatch_run \n"
);
asm(
"   .section .r10e_tail_text,\"ax\",%progbits     \n"
"   .align  2                               \n"
"   .global fast_dispatch_nothead_e         \n"
"   .type   fast_dispatch_nothead_e, %function \n"
"fast_dispatch_nothead_e:                   \n"
"   mov     r0, ip                          \n"   /* the PC */
"   bl      r10e_promote                    \n"
"   mov     r0, r4                          \n"   /* as .Lfd_ret: the units run */
"   pop     {r4, pc}                        \n"
"   .ltorg                                  \n"
"   .size   fast_dispatch_nothead_e, .-fast_dispatch_nothead_e \n"
"   .text                                   \n"
);
#endif /* FASTDISPATCH_ALLOWED */

void start_emu(void *addr)
{
#ifndef __aarch64__
    register struct M68KState * m68k asm("fp");
    struct M68KState __m68k;
    M68K_SetStatePtr(&__m68k);          /* must be before M68K_InitializeCache() */
#ifndef EMU68_FPU
    { extern int DisableFPU; DisableFPU = 1; }
#endif
    feat_desc_apply(1);
#ifdef EMU68_A3D
    { extern void a3d_fw_boot(const char *args); a3d_fw_boot(0); }
#endif
#if defined(CHIPSET_AXI)
    bootprog(0xB0070010);
#endif
    M68K_InitializeCache();
#if defined(CHIPSET_AXI)
    bootprog(0xB0070011);
#endif

    void (*arm_code)();
    int      trace_step = 0;
    uint32_t trace_pc   = 0;
    struct M68KTranslationUnit * unit = (void*)0;

    asm volatile ("mov %0, %1":"=r"(m68k):"r"(&__m68k));

    bzero(&__m68k, sizeof(__m68k));
    jit_levers_reset(m68k);
#if defined(CHIPSET_AXI)
    m68k->A[7].u32 = BE32((uint32_t)M68K_BOOT_SSP);
#else
    m68k->A[7].u32 = BE32((uint32_t)top_of_ram);
#endif
    m68k->PC = BE32((uint32_t)addr);

    print_context(m68k);

    m68k->A[7].u32 = BE32(BE32(m68k->A[7].u32) - 4);
    *(uint32_t*)(BE32(m68k->A[7].u32)) = 0;

#ifdef CHIPSET_BACKEND
    m68k->SR = BE16(SR_S);
    m68k->ISP.u32 = m68k->A[7].u32;
#endif

    printf("[JIT] Let it go...\n");

#if defined(CHIPSET_AXI)
    __asm__ __volatile__("mcr p15, 0, %0, c7, c5, 0" :: "r"(0));   /* ICIALLU */
    __asm__ __volatile__("mcr p15, 0, %0, c7, c5, 6" :: "r"(0));   /* BPIALL  */
    __asm__ __volatile__("mcr p15, 0, %0, c7, c10, 4" :: "r"(0));  /* DSB     */
    __asm__ __volatile__("isb");
    bootprog(0xB0070012);
#endif
#if defined(CHIPSET_AXI)
    int first_dispatch = 1;
#endif

#ifdef CHIPSET_BACKEND
    ipl_poll_countdown = 0;             /* 0 -> poll on the first iteration (global: the fast dispatcher shares it) */
#endif
#if defined(CHIPSET_AXI)
    uint32_t jit_health = 0;
#endif
#define DIAGDBG_HANG_PC 0x00FC2168u
    int dbg_wedge_done = 0;
    do {
#if defined(CHIPSET_AXI)
        ++jit_health;
        { static uint32_t hb_pub = 0xffffffffu, hb_early = 0;
          if ((!hb_early && jit_health >= 0x400u) || (jit_health >> 20) != hb_pub) {
              hb_early = 1; hb_pub = jit_health >> 20;
              bootprog(0xB0080000u | (hb_pub & 0x0000FFFFu));
          } }
#endif
#if defined(CHIPSET_AXI)
        if (__builtin_expect(first_dispatch, 0))
            bootprog(0xB0070013);
#endif
#ifdef CHIPSET_BACKEND
        if (__builtin_expect((uint32_t)m68k->PC == 0, 0)) {
            bootprog(0xB00E0002u);
            uint32_t rpc0 = BE32(*(volatile uint32_t *)0x00F80004u);
            m68k->SR  = BE16((uint16_t)0x2700);
            m68k->VBR = 0;
            m68k->A[7].u32 = BE32((uint32_t)M68K_BOOT_SSP);
            m68k->PC  = BE32(rpc0);
            last_PC = 0xffffffff;
        }
        if (__builtin_expect(m68k->STOP_REQ != 0, 0)) {
            ipl_poll_countdown = 0;      /* poll the chipset IPL every pass */
            last_PC = 0xffffffff;        /* fresh dispatch on wake           */
            goto stop_idle_poll;
        }
#endif
        trace_step = (BE16(m68k->SR) & SR_T1) != 0;
        trace_pc   = BE32((uint32_t)m68k->PC);
        if (!trace_step && !jit_nocache(m68k) && last_PC != (uint32_t)m68k->PC) {
#if defined(CHIPSET_AXI)
            if (__builtin_expect((uint32_t)m68k->PC == 0xFFFFFFFFu && !dbg_wedge_done, 0)) {
                dbg_wedge_done = 1;
                bootprog(0xB00DEAD0u);
                for (;;) asm volatile("wfe");
            }
#endif
            {
                uint16_t *pc_ptr = (uint16_t *)(BE32((uint32_t)m68k->PC));
                unit = M68K_GetTranslationUnit(pc_ptr);
                if (jit_dispatch_verifies((uint32_t)pc_ptr) && unit) {
                    if (M68K_VerifyUnit(unit) == NULL) {
                        unit = M68K_GetTranslationUnit(pc_ptr);   /* stale -> re-translate fresh */
                    }
                }
            }
            last_PC = (uint32_t)m68k->PC;
#if defined(EMU68_BLOCKSPIN)
            for (volatile uint32_t _spin = 0; _spin < g_block_spin; _spin++) { asm volatile("" ::: "memory"); }
#endif
        }
        if (__builtin_expect(trace_step, 0)) {
            uint32_t saved_depth = Options.M68K_TRANSLATION_DEPTH;
            Options.M68K_TRANSLATION_DEPTH = 1;
            g_trace_step = 1;
            *(void**)(&arm_code) = M68K_TranslateNoCache((uint16_t *)(uintptr_t)BE32((uint32_t)m68k->PC));
            g_trace_step = 0;
            Options.M68K_TRANSLATION_DEPTH = saved_depth;
            m68k->EXC_TAKEN = 0;
            last_PC = 0xffffffff;
        } else if (__builtin_expect(jit_nocache(m68k), 0)) {
            *(void**)(&arm_code) = M68K_TranslateNoCache((uint16_t *)(uintptr_t)BE32((uint32_t)m68k->PC));
            last_PC = 0xffffffff;      /* nothing is cached, so never claim a hit */
        } else {
            *(void**)(&arm_code) = unit->mt_ARMEntryPoint;
        }
#if defined(CHIPSET_AXI)
        if (__builtin_expect(first_dispatch, 0))
            bootprog(0xB0070014);
#endif /* CHIPSET_AXI */
#ifdef EMU68_ZZNET
        if (__builtin_expect(g_zznet_active != 0u, 0)) {
            extern void zz_eth_pump_global(void);
            zz_eth_pump_global();
        }
#endif
        m68k->LOOP_BUDGET = (uint32_t)g_ipl_poll_interval * M68K_LOOP_BUDGET_PER_POLL;
#if FASTDISPATCH_ALLOWED
        if (__builtin_expect(!trace_step && g_guest_smc_announce && JITMODE_IS_CACHED(g_jit_mode) && g_block_spin == 0, 1)) {
            unsigned ran = fast_dispatch_run(*(void **)(&arm_code));
            jit_health += ran - 1u;
        } else
#endif
        arm_code(m68k);
#ifdef CHIPSET_BACKEND
stop_idle_poll: ;                       /* STOP: dispatch skipped, polling continues */
#endif
#if defined(CHIPSET_AXI)
        if (__builtin_expect(first_dispatch, 0)) {
            first_dispatch = 0;
            bootprog(0xB0070015);
        }
#endif

#ifdef CHIPSET_BACKEND
        if (__builtin_expect((uint32_t)m68k->PC == 0, 0)) {
            bootprog(0xB00E0001u);
            uint32_t rpc0 = BE32(*(volatile uint32_t *)0x00F80004u);
            m68k->SR  = BE16((uint16_t)0x2700);
            m68k->VBR = 0;
            m68k->A[7].u32 = BE32((uint32_t)M68K_BOOT_SSP);
            m68k->PC  = BE32(rpc0);
            last_PC = 0xffffffff;
            continue;
        }
        if (BE32((uint32_t)m68k->PC) == 0xFFFFFFFE) {
            m68k_do_rte(m68k);
            last_PC = 0xffffffff;
            ipl_poll_countdown = 0;
        }
        if (m68k->MOVE2SR_REQ) {
            m68k_do_move_to_sr(m68k);
            last_PC = 0xffffffff;
            ipl_poll_countdown = 0;
        }
        if (m68k->CACR_REQ) {
            if (m68k_service_ctl_req(m68k))
                last_PC = 0xffffffff;
        }
#ifdef EMU68_A3D
        if (m68k->ONCALL_REQ) {
            extern void a3d_oncall_trap(struct M68KState *m);
            a3d_oncall_trap(m68k);
        }
#endif
        if (__builtin_expect(trace_step, 0)) {
            if (!m68k->EXC_TAKEN) {
                if (m68k->STOP_REQ) m68k->STOP_REQ = 0;
                m68k_do_trace(m68k, trace_pc);
            }
            m68k->EXC_TAKEN = 0;
            trace_step = 0;
            last_PC = 0xffffffff;
        }
        if (m68k->PC != 0 && --ipl_poll_countdown <= 0) {
            ipl_poll_countdown = g_ipl_poll_interval;

            {
                static int peek_throttle = 0;
                if (++peek_throttle >= 32) {
                    peek_throttle = 0;
                    {
                        extern volatile uint32_t g_guest_reset_pulsed;
                        if (g_guest_reset_pulsed) {
                            extern void cpuram_boards_reset(void);
                            g_guest_reset_pulsed = 0;
                            cpuram_boards_reset();
                        }
                    }
#ifdef EMU68_ZZ9000
                    {
                        extern volatile uint32_t g_zz9000_off;
                        extern void zz9000_tick(void);
                        if (__builtin_expect(g_zz9000_off == 0u, 1)) zz9000_tick();
                    }
#endif
#ifdef EMU68_UAESND
                    {
                        extern volatile uint32_t g_uaesnd_active;
                        extern volatile uint32_t g_uaesnd_off;
                        extern void uaesnd_tick(void);
                        static uint32_t uas_poll;
                        if (__builtin_expect(g_uaesnd_off != 0u, 0)) { /* absent */ }
                        else if (__builtin_expect(g_uaesnd_active != 0u, 0))
                            uaesnd_tick();
                        else if ((++uas_poll & 0x3FFu) == 0u) {
                            if (*(volatile uint32_t *)(UAESND_CTRL_DDR + UAESND_CTRL_MODE))
                                g_uaesnd_active = 1u;
                        }
                    }
#endif
                    if (jit_levers_host_tick())
                        last_PC = 0xffffffffu;
                    {
                        static uint32_t clk_tick;
                        if (((++clk_tick) & 0xFFFFu) == 0u)
                            cpu_clock_refresh();
                    }
                }
            }

#ifdef EMU68_ZZNET
            int level = zz_idle_pending_ipl();
#else
            int level = chipset_pending_ipl();
#endif
            {
                static int ipl7_held;
                if (level < 7) ipl7_held = 0;
                else if (ipl7_held) level = 0;
                else ipl7_held = 1;
            }
            { extern int chipset_seam_in_reset(void);
            if ((level <= 0 && !chipset_alive()) || g_reset_latched || chipset_seam_in_reset()) {
                bootprog(0xB0070003);
                kprintf("[JIT] external chipset reset observed — waiting for release...\n");
                {
                    uint32_t rel_guard = 200000u;
                    while (!chipset_alive() && --rel_guard)
                        for (volatile int i = 0; i < 10000; i++) ;
                    if (!rel_guard)
                        kprintf("[JIT] reset-release wait timed out; proceeding anyway\n");
                }
                g_reset_latched = 0;   /* consumed: chipset released - clear before re-boot */

                seam_flush_bridge();

                arm_dcache_invalidate(0x00F80000u, 0x00080000u);   /* 512 KB ROM window */

                {
                    static uint32_t reboot_count = 0;
                    uint32_t rom_hash = rom_hash16();   /* fresh read post-invalidate */
                    int feats_changed = feat_desc_apply(0);
                    if (rom_hash != g_last_rom_hash || feats_changed) {
                        kprintf("[JIT] re-boot #%u: flushing JIT (hash %08x, prev %08x%s)\n",
                                reboot_count, rom_hash, g_last_rom_hash,
                                feats_changed ? ", features changed" : "");
                        M68K_FlushCache();
                        g_last_rom_hash = rom_hash;
                    }
                    reboot_count++;
                }
                jit_levers_reset(m68k);
                { extern void cpuram_boards_reset(void); cpuram_boards_reset(); }
                arm_dcache_invalidate(0x00F80000u, 64u);
                asm volatile("dsb; isb" ::: "memory");
                uint32_t rpc = BE32(*(volatile uint32_t *)(ROM_DDR_BASE + 0x00080004u));
                if (rpc < 0x00F80000u || rpc > 0x00FFFFFFu) {
                    uint32_t rpc_win = BE32(*(volatile uint32_t *)0x00F80004u);
                    kprintf("[JIT] reset: DDR vector %08x implausible, window says %08x\n", rpc, rpc_win);
                    if (rpc_win >= 0x00F80000u && rpc_win <= 0x00FFFFFFu) rpc = rpc_win;
                }
                m68k->SR  = BE16((uint16_t)0x2700);
                ps_write_16(0x00000004u, 0);
                ps_write_16(0x00000006u, 0);
                ps_write_16(0x00000000u, 0);
                ps_write_16(0x00000002u, 0);
                m68k->VBR = 0;
                m68k->A[7].u32 = BE32((uint32_t)M68K_BOOT_SSP);
                m68k->PC  = BE32(rpc);
                last_PC = 0xffffffff;
                bootprog(0xB0070004);
                kprintf("[JIT] chipset released — m68k RE-BOOT from vector PC=%08x\n", rpc);
                continue;
            } }
            if (level > 0) {
                int mask = (BE16(m68k->SR) >> 8) & 7;
                if (level == 7 || level > mask) {
                    take_m68k_interrupt(m68k, level);
                    chipset_ack_ipl(level);
                    last_PC = 0xffffffff;
                }
            }
        }
#endif
    } while(m68k->PC != 0);

    bootprog(0xB00E0000u);

    printf("[JIT] Back from translated code\n");
    print_context(m68k);
    M68K_DumpStats();
#else
    (void)addr;
#endif
}
