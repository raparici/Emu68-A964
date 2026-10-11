/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: MIT
*/

/* The Zorro III board that delivers devicetree.resource. */

#include <stdint.h>
#include <boards.h>
#include <support.h>
#include "amiga_memmap.h"

#include "../boards/devicetree.h"     /* devicetree_bin[4096] - the compiled resource   */

extern uint32_t mmu_table[4096];
extern void arm_flush_cache(uintptr_t addr, uint32_t length);

static uint8_t dtree_rom[4096] __attribute__((aligned(4)));
static uint8_t dtree_fdt[1024] __attribute__((aligned(8)));
static uint32_t dtree_fdt_size = 0;
extern volatile uint32_t g_cpu_hz;
static uint32_t *dtree_clk[2];

uint32_t g_dtree_rom_base = 0;

static uint32_t dtree_rom_ok = 0;
volatile uint32_t g_dtree_off = 1;
extern struct ExpansionBoard dtree_board;   /* defined at the end of this file */

#define FDT_BEGIN_NODE 1u
#define FDT_END_NODE   2u
#define FDT_PROP       3u
#define FDT_END        9u

static uint32_t *fw;            /* struct-area cursor      */
static uint8_t  *fs;            /* strings-area cursor     */
static uint8_t  *fs_base;

static uint32_t addstr(const char *s)
{
    /* tiny table, few strings: linear dedupe keeps the blob honest */
    uint8_t *p = fs_base;
    while (p < fs) {
        const char *a = (const char *)p, *b = s;
        while (*a && *a == *b) { a++; b++; }
        if (*a == *b) return (uint32_t)(p - fs_base);
        while (*p) p++;
        p++;
    }
    uint32_t off = (uint32_t)(fs - fs_base);
    while ((*fs++ = (uint8_t)*s++)) ;
    return off;
}

static void node(const char *name)
{
    *fw++ = FDT_BEGIN_NODE;
    uint8_t *d = (uint8_t *)fw;
    while ((*d++ = (uint8_t)*name++)) ;
    while (((uintptr_t)d) & 3) *d++ = 0;
    fw = (uint32_t *)d;
}

static void prop_str(const char *name, const char *val)
{
    uint32_t len = 0; while (val[len]) len++; len++;          /* include the NUL */
    *fw++ = FDT_PROP; *fw++ = len; *fw++ = addstr(name);
    uint8_t *d = (uint8_t *)fw;
    for (uint32_t i = 0; i < len; i++) d[i] = (uint8_t)val[i];
    d += len; while (((uintptr_t)d) & 3) *d++ = 0;
    fw = (uint32_t *)d;
}

static void prop_u32(const char *name, uint32_t v)
{
    *fw++ = FDT_PROP; *fw++ = 4; *fw++ = addstr(name); *fw++ = v;
}

static void build_fdt(void)
{
    uint32_t *hdr = (uint32_t *)dtree_fdt;
    uint32_t *rsv = (uint32_t *)(dtree_fdt + 0x28);
    fw      = (uint32_t *)(dtree_fdt + 0x38);
    fs_base = fs = dtree_fdt + sizeof(dtree_fdt) - 256;       /* strings grow in the tail */

    rsv[0] = rsv[1] = rsv[2] = rsv[3] = 0;                    /* one terminator entry     */

    node("");
    prop_u32("#address-cells", 1);
    prop_u32("#size-cells", 1);
    prop_str("model", "BigMig (DE10-Nano, Emu68-A9)");
    prop_str("compatible", "raparici,bigmig");
      node("chosen");
      prop_str("bootargs", "bigmig de10nano maxcpus=1 sidecar=1");
      *fw++ = FDT_END_NODE;
      node("cpus");
        node("cpu@0");
        prop_str("compatible", "arm,cortex-a9");
        prop_u32("clock-frequency", g_cpu_hz);     /* Emu68Info CPU prints MHz from this */
        dtree_clk[0] = fw - 1;
        *fw++ = FDT_END_NODE;
        node("cpu@1");
        prop_str("compatible", "arm,cortex-a9");
        prop_u32("clock-frequency", g_cpu_hz);     /* one MPU clock drives both A9s      */
        dtree_clk[1] = fw - 1;
        *fw++ = FDT_END_NODE;
      *fw++ = FDT_END_NODE;
      node("emu68");
      prop_str("idstring", VERSION_STRING);
      {
          static char githash[48];
          const char *v = VERSION_STRING; const char *g = 0;
          for (; *v; v++) if (v[0]=='g' && v[1]=='i' && v[2]=='t' && v[3]==':') { g = v + 4; break; }
          if (g) {
              unsigned n = 0;
              while (g[n] && g[n] != ' ' && g[n] != ',' && n < sizeof(githash)-1) { githash[n] = g[n]; n++; }
              githash[n] = 0;
              prop_str("git-hash", githash);
          }
      }
      prop_str("variant", "bigmig");
      *fw++ = FDT_END_NODE;
    *fw++ = FDT_END_NODE;
    *fw++ = FDT_END;

    /* compact: move strings up against the struct area */
    uint32_t struct_size = (uint32_t)((uint8_t *)fw - (dtree_fdt + 0x38));
    uint32_t str_size    = (uint32_t)(fs - fs_base);
    uint8_t *dst = (uint8_t *)fw;
    for (uint32_t i = 0; i < str_size; i++) dst[i] = fs_base[i];

    hdr[0] = 0xd00dfeedu;                     /* magic            */
    hdr[1] = 0x38u + struct_size + str_size;  /* totalsize        */
    hdr[2] = 0x38u;                           /* off_dt_struct    */
    hdr[3] = 0x38u + struct_size;             /* off_dt_strings   */
    hdr[4] = 0x28u;                           /* off_mem_rsvmap   */
    hdr[5] = 17u;                             /* version          */
    hdr[6] = 16u;                             /* last_comp        */
    hdr[7] = 0u;                              /* boot_cpuid       */
    hdr[8] = str_size;                        /* size_dt_strings  */
    hdr[9] = struct_size;                     /* size_dt_struct   */
    dtree_fdt_size = hdr[1];
}

void dtree_board_init(void)
{
    for (uint32_t i = 0; i < sizeof(dtree_rom); i++)
        dtree_rom[i] = devicetree_bin[i];

    if (dtree_rom[0] != 0x90u || dtree_rom[2] != 0x10u) {
        kprintf("[DTREE] upstream ROM header unexpected (%02x %02x) — board left DISABLED\n",
                dtree_rom[0], dtree_rom[2]);
        return;                               /* dtree_rom_ok stays 0: never offered      */
    }
    dtree_rom[2] = 0x60u;                     /* er_Type size code 1 (64 KB) -> 6 (2 MB)  */

    build_fdt();

    dtree_rom_ok = 1;
    kprintf("[DTREE] resource ROM patched to 2 MB, FDT %u bytes (offered only if the OSD asks)\n",
            dtree_fdt_size);
}

int dtree_board_enable(int on)
{
    uint32_t want = (on && dtree_rom_ok) ? 1u : 0u;
    int changed = (dtree_board.enabled != want);
    dtree_board.enabled = want;
    g_dtree_off = want ? 0u : 1u;
    return changed;
}

static void map(struct ExpansionBoard *board)
{
    uint32_t base = board->map_base;

    if (base & 0x000FFFFFu) {
        kprintf("[DTREE] map_base $%08x is not 1MB-aligned — board NOT mapped\n", base);
        return;
    }

    mmu_table[DTREE_ROM_DDR_BASE >> 20] = DTREE_ROM_DDR_BASE | MMU_DEVICE;
    arm_flush_cache((uintptr_t)&mmu_table[DTREE_ROM_DDR_BASE >> 20], 4);
    asm volatile("mcr p15,0,%0,c8,c7,0" :: "r"(0) : "memory");
    asm volatile("dsb; isb" ::: "memory");

    if (dtree_clk[0]) *dtree_clk[0] = g_cpu_hz;
    if (dtree_clk[1]) *dtree_clk[1] = g_cpu_hz;
    {
        uint8_t *dst = (uint8_t *)DTREE_ROM_DDR_BASE;
        uint32_t bad = 0;
        for (uint32_t i = 0; i < sizeof(dtree_rom); i++) dst[i] = dtree_rom[i];
        for (uint32_t i = 0; i < dtree_fdt_size; i++)    dst[0x1000 + i] = dtree_fdt[i];
        arm_flush_cache((uintptr_t)dst, 0x1000 + dtree_fdt_size);
        for (uint32_t i = 0; i < sizeof(dtree_rom); i++) if (dst[i] != dtree_rom[i]) bad++;
        for (uint32_t i = 0; i < dtree_fdt_size; i++)    if (dst[0x1000+i] != dtree_fdt[i]) bad++;
        if (bad) {
            kprintf("[DTREE] staging VERIFY FAILED (%u bytes) — board NOT mapped\n", bad);
            return;
        }
    }

    mmu_table[DTREE_ROM_DDR_BASE >> 20] = DTREE_ROM_DDR_BASE | MMU_DEVICE | 0x8000u;

    mmu_table[base >> 20]       = DTREE_ROM_DDR_BASE | MMU_CACHED_RO;
    mmu_table[(base >> 20) + 1] = DTREE_ROM_DDR_BASE | MMU_CACHED_RO;
    arm_flush_cache((uintptr_t)&mmu_table[base >> 20], 8);
    asm volatile("mcr p15, 0, %0, c8, c7, 0" :: "r"(0) : "memory");
    asm volatile("dsb ish; isb" ::: "memory");

    g_dtree_rom_base = base;
    kprintf("[DTREE] resource at $%08x (ROM 4K + FDT %u), 2 MB window aliased\n",
            base, dtree_fdt_size);
}

void dtree_board_unmap(void)
{
    if (!g_dtree_rom_base) return;
    mmu_table[g_dtree_rom_base >> 20]       = 0;
    mmu_table[(g_dtree_rom_base >> 20) + 1] = 0;
    arm_flush_cache((uintptr_t)&mmu_table[g_dtree_rom_base >> 20], 8);
    asm volatile("dsb" ::: "memory");
    asm volatile("mcr p15, 0, %0, c8, c7, 0" :: "r"(0u));
    asm volatile("dsb; isb" ::: "memory");
    kprintf("[DTREE] reset: UNMAPPED the board window at $%08x\n", g_dtree_rom_base);
    g_dtree_rom_base = 0;
}

struct ExpansionBoard dtree_board = {
    dtree_rom,                  /* the PATCHED serve-copy, never the upstream original    */
    sizeof(dtree_rom),
    0,
    1,                          /* is_z3                                                  */
    0,
    map
};

static void * __attribute__((used, section(".boards.z3"))) _board = &dtree_board;
