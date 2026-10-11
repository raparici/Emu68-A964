/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: GPL-3.0-or-later
*/

/* ZZ9000: the Zorro III board, its register page, the scan-out push and the Ethernet rings. */

#include <stdint.h>
#include <boards.h>
#include <support.h>
#include "amiga_memmap.h"
#include "zz9000/zz_card.h"
#ifdef EMU68_MHI
#include "mhi/mhi_mailbox.h"
#include "mhi/mhi_fw.h"
#endif

#define ZZ9000_VRAM_PHYS     0x23800000u   /* D1: the re-validated gap       */
#define ZZ9000_BACKING_BYTES 0x04000000u   /* 64 MB, and every byte of it is real   */
#define ZZ9000_WINDOW_MB     64u
#define ZZ9000_BACKED_MB     (ZZ9000_BACKING_BYTES >> 20)

extern uint32_t mmu_table[4096];
extern unsigned ps_read_16(unsigned addr);

#define ZZ_L2_FASTRAM 0x00000136u
static uint32_t zz_kva_to_pa(const void *va)
{
    uint32_t ttbr0;
    asm volatile("mrc p15,0,%0,c2,c0,0" : "=r"(ttbr0));
    return (uint32_t)va + ((ttbr0 & 0xFFFFC000u) - (uint32_t)mmu_table);
}
#define ZZ_L1_COARSE(pa) (((pa) & 0xFFFFFC00u) | 0x1u)

static uint32_t zz_l2[256] __attribute__((aligned(1024)));

struct zz_card g_zz_card;
uint32_t g_zz9000_regbase;          /* 0 until map(); the DABT branch guard  */

static const uint8_t zz9000_acrom[256] = {
    [0x00] = 0x80,                  /* er_Type hi: Zorro III, no memlist     */
    [0x02] = 0x20,                  /* er_Type lo: size code 010 = 64 MB ext  */
    [0x04] = 0xF0, [0x06] = 0xB0,   /* product 0x04 (inverted 0/4)           */
    [0x08] = 0xD0, [0x0A] = 0xF0,   /* er_Flags 0x20: extended sizes         */
    [0x0C] = 0xF0, [0x0E] = 0xF0,   /* reserved                              */
    [0x10] = 0x90, [0x12] = 0x20,   /* manufacturer 0x6d6e                   */
    [0x14] = 0x90, [0x16] = 0x10,   /*   (inverted 6,d,6,e)                  */
    [0x18] = 0xF0, [0x1A] = 0xF0,   /* serial 0                              */
    [0x1C] = 0xF0, [0x1E] = 0xF0,
    [0x20] = 0xF0, [0x22] = 0xF0,
    [0x24] = 0xF0, [0x26] = 0xF0,
    [0x28] = 0xF0, [0x2A] = 0xF0,   /* no init-diag ROM                      */
    [0x2C] = 0xF0, [0x2E] = 0xF0,
    /* the rest reads as inverted-0 (0xF0), the autoconfig default */
    [0x30] = 0xF0, [0x32] = 0xF0, [0x34] = 0xF0, [0x36] = 0xF0,
    [0x38] = 0xF0, [0x3A] = 0xF0, [0x3C] = 0xF0, [0x3E] = 0xF0,
};

static uint32_t zz9000_vblank_read(void *ud)
{
    (void)ud;
    unsigned vpos = ((ps_read_16(0x00DFF004u) & 7u) << 8) |
                    (ps_read_16(0x00DFF006u) >> 8);
    return vpos < 26u;
}

static void zz9000_flush_l1(uint32_t base_sec, uint32_t count)
{
    arm_flush_cache((uintptr_t)&mmu_table[base_sec], count * 4);
    asm volatile("dsb" ::: "memory");
    asm volatile("mcr p15,0,%0,c8,c7,0" :: "r"(0u));   /* TLBIALL */
    asm volatile("mcr p15,0,%0,c7,c5,6" :: "r"(0u));   /* BPIALL  */
    asm volatile("dsb; isb" ::: "memory");
}

#ifdef EMU68_ZZNET
static uint8_t zz_eth_winbuf[14 * 4096] __attribute__((aligned(4096)));

extern void ps_write_16(unsigned int address, unsigned int data);

static void zz9000_eth_inject(void *ud)
{
    (void)ud;
    ps_write_16(0x00DFF09Cu, 0xA000u);
}

/* called from the JIT C-pass (start_de10nano.c) - header-free coupling */
void zz_eth_pump_global(void)
{
    zz_eth_pump(&g_zz_card);
}
#endif

#ifdef EMU68_A3D
static void zz9000_a3d_fence(void *ud, uint32_t board_off);
static uint32_t zz9000_a3d_fence_of(void *ud, uint32_t board_off);
static int zz9000_a3d_wb_early(void *ud);
static int zz9000_a3d_fence_kick(void *ud, uint32_t fence);
static int zz9000_a3d_cover(void *ud, const uint32_t *lo, const uint32_t *hi, unsigned n, uint32_t fence);
static void zz9000_a3d_uncover(void *ud);
static uint32_t zz9000_qdrain(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
static int zz9000_a3d_fence_try(void *ud, uint32_t fence);
static void zz9000_a3d_fence_wait(void *ud, uint32_t fence);
#endif

static void map(struct ExpansionBoard *board)
{
    uint32_t base = board->map_base;
    if (base & 0x000FFFFFu) {
        kprintf("[ZZ9000] REJECT base %08x (not MB aligned)\n", base);
        return;
    }

    for (unsigned i = 0; i < 16; i++)
        zz_l2[i] = 0;
#ifdef EMU68_ZZNET
    {
        uint32_t win_pa = zz_kva_to_pa(zz_eth_winbuf);
        for (unsigned i = 2; i < 16; i++)
            zz_l2[i] = (win_pa + ((i - 2u) << 12)) | ZZ_L2_FASTRAM;
    }
#endif
    for (unsigned i = 16; i < 256; i++)
        zz_l2[i] = (ZZ9000_VRAM_PHYS + (i << 12)) | ZZ_L2_FASTRAM;
    arm_flush_cache((uintptr_t)zz_l2, sizeof(zz_l2));
    asm volatile("dsb" ::: "memory");

    uint32_t l2_pa = zz_kva_to_pa(zz_l2);
    if (l2_pa & 0x3FFu) {
        kprintf("[ZZ9000] REFUSE map: L2 phys %08x not 1KB aligned\n", l2_pa);
        return;
    }
    uint32_t sec = base >> 20;
    mmu_table[sec] = ZZ_L1_COARSE(l2_pa);
    for (unsigned i = 1; i < ZZ9000_BACKED_MB; i++)
        mmu_table[sec + i] = (ZZ9000_VRAM_PHYS + (i << 20)) | MMU_FASTRAM;
    for (unsigned i = ZZ9000_BACKED_MB; i < ZZ9000_WINDOW_MB; i++)
        mmu_table[sec + i] = 0;
    zz9000_flush_l1(sec, ZZ9000_WINDOW_MB);

    uint8_t *vram_ptr = (uint8_t *)base;
#ifdef EMU68_A3D
    for (unsigned i = 0; i < ZZ9000_BACKED_MB; i++)
        mmu_table[(ZZ9000_VRAM_ALIAS_VA >> 20) + i] = (ZZ9000_VRAM_PHYS + (i << 20)) | MMU_FASTRAM;
    zz9000_flush_l1(ZZ9000_VRAM_ALIAS_VA >> 20, ZZ9000_BACKED_MB);
    vram_ptr = (uint8_t *)ZZ9000_VRAM_ALIAS_VA;
#endif
    for (unsigned i = 0; i < ZZ9000_BACKED_MB; i++)
        mmu_table[(ZZ9000_VRAM_NC_VA >> 20) + i] = (ZZ9000_VRAM_PHYS + (i << 20)) | 0x1c02u;
    zz9000_flush_l1(ZZ9000_VRAM_NC_VA >> 20, ZZ9000_BACKED_MB);
    zz_card_init(&g_zz_card, vram_ptr, ZZ9000_BACKING_BYTES,
                 ZZ9000_BACKING_BYTES);
    g_zz_card.vram_nc = (uint8_t *)ZZ9000_VRAM_NC_VA;
    g_zz_card.vblank_read = zz9000_vblank_read;
    g_zz9000_regbase = base;
#ifdef EMU68_A3D
    {
        extern void a3d_fw_prot_window(uint32_t idx, uint32_t lo, uint32_t bytes);
        g_zz_card.a3d_fence = zz9000_a3d_fence;
        {
            extern uint32_t g_a3d_gate_off;
            g_zz_card.a3d_fence_of = zz9000_a3d_fence_of;
            g_zz_card.a3d_fence_try = zz9000_a3d_fence_try;
            g_zz_card.a3d_fence_wait = zz9000_a3d_fence_wait;
            g_zz_card.a3d_defer = g_a3d_gate_off ? 0u : 1u;
        }
        {
            extern void a3d_fw_prot_qdrop(void);
            extern uint32_t (*g_ap_qdrain)(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
            a3d_fw_prot_qdrop();
            g_zz_card.a3d_wb_early = zz9000_a3d_wb_early;
            g_zz_card.a3d_fence_kick = zz9000_a3d_fence_kick;
            g_zz_card.a3d_cover = zz9000_a3d_cover;
            g_zz_card.a3d_uncover = zz9000_a3d_uncover;
            g_ap_qdrain = zz9000_qdrain;
        }
        /* the VRAM part of the window may be protected (pages 16.. of MB0 and the sections after it) */
        a3d_fw_prot_window(0, base + 0x10000u, ZZ9000_BACKING_BYTES - 0x10000u);
    }
#endif
#ifdef EMU68_ZZNET
    g_zz_card.eth_win    = (uint8_t *)base;
    g_zz_card.eth_ctrl   = (volatile uint32_t *)ZZNET_CTRL_DDR;
    g_zz_card.eth_txring = (uint8_t *)ZZNET_TX_RING_DDR;
    g_zz_card.eth_rxring = (uint8_t *)ZZNET_RX_RING_DDR;
    g_zz_card.eth_inject = zz9000_eth_inject;
    g_zz_card.eth_ctrl[ZZNET_CTRL_TX_PROD / 4] = 0;
    g_zz_card.eth_ctrl[ZZNET_CTRL_TX_CONS / 4] = 0;
    g_zz_card.eth_ctrl[ZZNET_CTRL_RX_PROD / 4] = 0;
    g_zz_card.eth_ctrl[ZZNET_CTRL_RX_CONS / 4] = 0;
    g_zz_card.eth_ctrl[ZZNET_CTRL_MAGIC / 4]   = ZZNET_MAGIC;
    {
        static const uint8_t zznet_defmac[6] = { 0x02, 0xB1, 0x69, 0x00, 0x00, 0x01 };
        zz_eth_set_mac(&g_zz_card, zznet_defmac);
    }
    kprintf("[ZZ9000] eth armed: win pages 2-15 -> %08x, rings %08x\n",
            zz_kva_to_pa(zz_eth_winbuf), (uint32_t)ZZNET_RINGS_DDR);
#endif

    kprintf("[ZZ9000] board at %08x, VRAM %08x+%uMB, L2 at %08x\n",
            base, ZZ9000_VRAM_PHYS, ZZ9000_BACKED_MB, (uint32_t)zz_l2);
}

#ifdef EMU68_A3D
int zz9000_vram_span(uint32_t *guest, uint32_t *bytes, uint32_t *phys, uint8_t **host)
{
    if (!g_zz9000_regbase)
        return 0;
    *guest = g_zz9000_regbase + 0x10000u;
    *bytes = ZZ9000_BACKING_BYTES - 0x10000u;
    *phys  = ZZ9000_VRAM_PHYS + 0x10000u;
    *host  = (uint8_t *)(ZZ9000_VRAM_ALIAS_VA + 0x10000u);
    return 1;
}

static void zz9000_a3d_fence(void *ud, uint32_t board_off)
{
    extern void a3d_fw_fence(uint32_t lo, uint32_t hi);
    (void)ud;
    if (g_zz9000_regbase && board_off < ZZ9000_BACKING_BYTES)
        a3d_fw_fence(g_zz9000_regbase + board_off, g_zz9000_regbase + board_off + 1u);
}

static uint32_t zz9000_a3d_fence_of(void *ud, uint32_t board_off)
{
    extern uint32_t a3d_fw_fence_of(uint32_t lo, uint32_t hi);
    (void)ud;
    if (!g_zz9000_regbase || board_off >= ZZ9000_BACKING_BYTES)
        return 0;
    return a3d_fw_fence_of(g_zz9000_regbase + board_off, g_zz9000_regbase + board_off + 1u);
}

static int zz9000_a3d_wb_early(void *ud)
{
    extern uint32_t a3d_fw_wb_early(void);
    (void)ud;
    return (int)a3d_fw_wb_early();
}

static int zz9000_a3d_fence_kick(void *ud, uint32_t fence)
{
    extern uint32_t a3d_fw_fence_kick(uint32_t fence);
    (void)ud;
    return (int)a3d_fw_fence_kick(fence);
}

static int zz9000_a3d_cover(void *ud, const uint32_t *lo, const uint32_t *hi, unsigned n, uint32_t fence)
{
    extern int a3d_fw_prot_qcover(const uint32_t *lo, const uint32_t *hi, uint32_t n, uint32_t fence);
    uint32_t glo[8], ghi[8];
    (void)ud;
    if (!g_zz9000_regbase || n == 0 || n > 8u)
        return 0;
    for (unsigned i = 0; i < n; i++) {
        if (hi[i] > ZZ9000_BACKING_BYTES || lo[i] >= hi[i])
            return 0;
        glo[i] = g_zz9000_regbase + lo[i];
        ghi[i] = g_zz9000_regbase + hi[i];
    }
    const int ok = a3d_fw_prot_qcover(glo, ghi, n, fence);
    return ok;
}

static void zz9000_a3d_uncover(void *ud)
{
    extern void a3d_fw_prot_quncover(void);
    (void)ud;
    a3d_fw_prot_quncover();
}

/* called through emu68_fastbank_call4 (agent3d/a3d_fw_prot.c ap_qfinish): the card's rows are NEON */
static uint32_t zz9000_qdrain(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    extern void zz_blitq_drain(void);
    (void)a; (void)b; (void)c; (void)d;
    zz_blitq_drain();
    return 0;
}

static int zz9000_a3d_fence_try(void *ud, uint32_t fence)
{
    extern uint32_t a3d_fw_fence_try(uint32_t fence);
    (void)ud;
    return (int)a3d_fw_fence_try(fence);
}

static void zz9000_a3d_fence_wait(void *ud, uint32_t fence)
{
    extern void a3d_fw_fence_wait(uint32_t fence);
    (void)ud;
    a3d_fw_fence_wait(fence);
}

#endif

int zz9000_vram_window(uint32_t *lo, uint32_t *hi)
{
    if (!g_zz9000_regbase)
        return 0;
    *lo = g_zz9000_regbase + 0x10000u;
    *hi = g_zz9000_regbase + ZZ9000_BACKING_BYTES;
    return 1;
}

int zz9000_vram_dma(uint32_t guest, uint32_t len, uint32_t *phys, uintptr_t *maint)
{
    uint32_t lo, hi;
    const char *how = "no 3D engine: nothing to fence";
    if (!zz9000_vram_window(&lo, &hi))
        return 0;
    if (guest < lo || guest >= hi || len > hi - guest)
        return 0;
#ifdef EMU68_A3D
    how = "nothing to fence (empty)";
    if (len)
    {
        extern void a3d_fw_fence_dma(uint32_t lo, uint32_t hi);
        {
            a3d_fw_fence_dma(guest, guest + len);
            how = "fenced";
        }
    }
    *maint = ZZ9000_VRAM_ALIAS_VA + (guest - g_zz9000_regbase);
#else
    *maint = guest;
#endif
    *phys = ZZ9000_VRAM_PHYS + (guest - g_zz9000_regbase);
    (void)how;
    return 1;
}

void zz9000_board_unmap(void)
{
    if (!g_zz9000_regbase)
        return;
#ifdef EMU68_A3D
    { extern void a3d_fw_prot_reset(void); a3d_fw_prot_reset(); }
#endif
#ifdef EMU68_MHI
    mhi_fw_reset();
#endif
    uint32_t sec = g_zz9000_regbase >> 20;
    for (unsigned i = 0; i < ZZ9000_WINDOW_MB; i++)
        mmu_table[sec + i] = 0;
    zz9000_flush_l1(sec, ZZ9000_WINDOW_MB);
    g_zz9000_regbase = 0;
}

extern void ps_write_16(unsigned addr, unsigned data);

static const uint8_t zz_fmt_of[4] = {
    3,                  /* CLUT: 8bpp palette, bytes are bytes             */
    4 + 16 + 32,        /* 16bpp 565 big-endian: swap lanes + BGR          */
    6 + 16,             /* 32bpp BGRA byte order: BGR flag, no swap        */
    4 + 8 + 16 + 32,    /* 15bpp 1555 big-endian                           */
};

extern int  zz_blitq_pending(void);
extern void zz_blitq_drain(void);
extern void zz_blitq_tick(void);

volatile uint32_t g_zz_hw_sprite            = 1;   /* the fabric overlay draws the pointer */

#define ZZ_SPR_BASE  0xB80C00u
#define ZZ_SPR_W(i)  (ZZ_SPR_BASE + (unsigned)(i) * 2u)

static void zz_sprite_hw_push(int force)
{
    struct zz_sprite *s = &g_zz_card.sprite;
    static int32_t  last_sx = 0x7fffffff, last_sy = 0x7fffffff;
    static uint32_t last_gen = 0xffffffffu;
    static int      last_ena = -1;
    if (force) { last_sx = last_sy = 0x7fffffff; last_gen = 0xffffffffu; last_ena = -1; }

    int32_t sx = (int32_t)s->x - (int32_t)s->x_off + 1;
    int32_t sy = (int32_t)s->y - (int32_t)s->y_off + 1;
    if (sx != last_sx || sy != last_sy) {
        ps_write_16(ZZ_SPR_W(198), (unsigned)(sx & 0xFFF));  /* signed 12-bit */
        ps_write_16(ZZ_SPR_W(199), (unsigned)(sy & 0xFFF));
        last_sx = sx; last_sy = sy;
    }

    /* image + colours: only when they changed (zz_card.c bumps sprite.hw_gen). */
    if (s->hw_gen != last_gen) {
        last_gen = s->hw_gen;
        for (int row = 0; row < 48; row++)
            for (int wc = 0; wc < 4; wc++) {
                unsigned w = 0;
                for (int k = 0; k < 8; k++)
                    w |= (unsigned)(s->pixels[row * 32 + wc * 8 + k] & 3u) << (k * 2);
                ps_write_16(ZZ_SPR_W(row * 4 + wc), w);      /* 8 pens / word */
            }
        for (int pen = 1; pen <= 3; pen++) {
            uint32_t col = s->colors[pen];   /* 0x00RRGGBB (zz_card.c convention) */
            unsigned wi = 192u + (unsigned)(pen - 1) * 2u;
            ps_write_16(ZZ_SPR_W(wi),     (col >> 16) & 0xFFu);
            ps_write_16(ZZ_SPR_W(wi + 1),  col & 0xFFFFu);
        }
    }

    int ena = (s->showing && s->pix_w && s->pix_h) ? 1 : 0;
    if (ena != last_ena) {
        ps_write_16(ZZ_SPR_W(200), (unsigned)ena);           /* control bit0 */
        last_ena = ena;
    }
}

static int zz_spr_hw_on;             /* current fabric-overlay mode           */
static void zz_sprite_service(void)
{
    if (!zz_spr_hw_on) {
        /* the first tick: stop compositing the pointer in VRAM, erase it there, push the overlay */
        g_zz_card.composite_sprite = 0;
        zz_sprite_refresh(&g_zz_card);
        zz_sprite_hw_push(1);
        zz_spr_hw_on = 1;
    } else {
        zz_sprite_hw_push(0);
    }
}

#define ZZ_LATCH_MS 21u
static uint32_t g_zz_shown_lo, g_zz_shown_hi, g_zz_replaced_lo, g_zz_replaced_hi, g_zz_shown_t;
static uint32_t zz9000_cycles(void)
{
    uint32_t c;
    __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(c));      /* PMCCNTR */
    return c;
}
/* the scan-out the card shows is about to be pushed: what it replaces, and when */
static void zz9000_shown_now(void)
{
    const struct zz_scanout *so = &g_zz_card.scanout;
    const uint32_t lo = so->fb_off, hi = so->fb_off + so->pitch_bytes * so->height;
    if (lo != g_zz_shown_lo || hi != g_zz_shown_hi) {
        g_zz_replaced_lo = g_zz_shown_lo;
        g_zz_replaced_hi = g_zz_shown_hi;
        g_zz_shown_lo = lo;
        g_zz_shown_hi = hi;
        g_zz_shown_t = zz9000_cycles();
    }
}
static void zz9000_push_scanout(void)
{
    struct zz_scanout *so = &g_zz_card.scanout;
    zz9000_shown_now();
    so->dirty = 0;
    uint32_t base = ZZ9000_VRAM_PHYS + so->fb_off;
    uint32_t fmt  = zz_fmt_of[so->colormode & 3];
    ps_write_16(0xB80100u, base >> 16);
    ps_write_16(0xB80102u, base & 0xffffu);
    ps_write_16(0xB80104u, fmt);
    ps_write_16(0xB80108u, so->width);
    ps_write_16(0xB8010Au, so->height);
    ps_write_16(0xB8010Cu, so->pitch_bytes & 0x3FFFu);
    ps_write_16(0xB80106u, so->enabled ? 1u : 0u);
}

#ifdef EMU68_A3D
uint32_t zz9000_display_guard(uint32_t glo, uint32_t ghi)
{
    extern uint32_t zz_blitq_flip_fence(uint32_t lo, uint32_t hi);
    extern uint32_t a3d_plat_cycles_per_ms(void);
    if (!g_zz9000_regbase || glo < g_zz9000_regbase + ZZ_MEMBASE_OFF || ghi <= glo
        || ghi - g_zz9000_regbase > ZZ9000_BACKING_BYTES)
        return 0;                           /* not the card's VRAM: nothing it shows */
    const uint32_t lo = glo - g_zz9000_regbase, hi = ghi - g_zz9000_regbase;
    const uint32_t f = zz_blitq_flip_fence(lo, hi);
    if (f)
        return f;
    if (g_zz_card.scanout.dirty)
        zz9000_push_scanout();
    if (lo < g_zz_replaced_hi && hi > g_zz_replaced_lo) {
        const uint32_t need = ZZ_LATCH_MS * a3d_plat_cycles_per_ms();
        if (zz9000_cycles() - g_zz_shown_t < need) {
            while (zz9000_cycles() - g_zz_shown_t < need)
                __asm__ volatile("yield");
        }
    }
    return 0;
}
#endif

void zz9000_tick(void)
{
    if (!g_zz9000_regbase)
        return;
#ifdef EMU68_MHI
    mhi_fw_tick();
#endif
    struct zz_scanout *so = &g_zz_card.scanout;

    if ((so->dirty || g_zz_card.palette.dirty) && zz_blitq_pending()) {
#ifdef EMU68_A3D
        extern int zz_blitq_is_covered(void);
        extern void zz_blitq_run_ready(void);
        if (zz_blitq_is_covered())
            zz_blitq_run_ready();
        else
#endif
            zz_blitq_drain();
    }

    if (so->dirty)
        zz9000_push_scanout();

    if (g_zz_card.palette.dirty) {
        g_zz_card.palette.dirty = 0;
        for (unsigned i = 0; i < 256; i++) {
            uint8_t *rgb = g_zz_card.palette.rgb[i];
            ps_write_16(0xB80400u + i * 4, rgb[0]);              /* 00 RR */
            ps_write_16(0xB80402u + i * 4,
                        ((uint32_t)rgb[1] << 8) | rgb[2]);       /* GG BB */
        }
    }

    zz_sprite_service();

    zz_blitq_tick();
}

uint32_t zz9000_mmio(uint32_t off, int is_load, unsigned size, uint32_t wval)
{
#ifdef EMU68_MHI
    if (off >= MHI_MBOX_OFF && off < MHI_MBOX_OFF + MHI_MBOX_LEN)
        return mhi_fw_mmio(off - MHI_MBOX_OFF, is_load, size, wval);
#endif
    if (is_load)
        return zz_card_mmio_read(&g_zz_card, off, size);
    zz_card_mmio_write(&g_zz_card, off, wval, size);
    return 0;
}

static struct ExpansionBoard board = {
    zz9000_acrom,
    sizeof(zz9000_acrom),
    0,
    1,      /* Zorro III */
    1,      /* enabled   */
    map
};

static void * __attribute__((used, section(".boards.z3"))) _zz9000 = &board;

volatile uint32_t g_zz9000_off = 0;
void zz9000_board_enable(int on)
{
    board.enabled = on ? 1u : 0u;
    g_zz9000_off  = on ? 0u : 1u;
}
