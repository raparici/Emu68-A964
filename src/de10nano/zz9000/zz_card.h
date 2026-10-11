/*
    Copyright (C) 2019-2026, Lucie L. Hartmann, MNT Research GmbH (ZZ9000OS)
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: GPL-3.0-or-later
*/

/* ZZ9000: the card model. */

#ifndef ZZ_CARD_H
#define ZZ_CARD_H

#include <stdint.h>

#define ZZ_WINDOW_SIZE     0x08000000u   /* announced Z3 window: 128 MB       */
#define ZZ_MEMBASE_OFF     0x00010000u
#ifndef ZZ_GFXDATA_OFF
#define ZZ_GFXDATA_OFF     0x03200000u   /* GFXData block (driver constant)   */
#endif
#ifndef ZZ_TEMPLATE_OFF
#define ZZ_TEMPLATE_OFF    0x03210000u   /* template staging (driver constant)*/
#endif
#ifndef ZZ_HEAP_START_OFF
#define ZZ_HEAP_START_OFF  0x03310000u   /* surface heap start (fw-mirrored)  */
#endif
/* heap end is an init parameter: bounded by the PHYSICAL backing (D6).       */

#define ZZ_FW_VERSION      0x0204u

/* MNT colormodes (zz9000.h) */
#define ZZ_CM_8BIT    0u
#define ZZ_CM_16BIT   1u
#define ZZ_CM_32BIT   2u
#define ZZ_CM_15BIT   3u

/* ---- GFXData, exactly the driver's #pragma pack(4) layout ---------------- */
#pragma pack(push, 4)
struct zz_gfxdata {
    uint32_t offset[2];
    uint32_t rgb[2];
    uint16_t x[4], y[4];
    uint16_t user[4];
    uint16_t pitch[4];
    uint8_t  u8_user[8];
    uint8_t  op, mask, minterm, u8offset;
    uint32_t u32_user[8];
    uint8_t  clut1[768];
    uint8_t  clut2[768];
    uint8_t  clut3[768];
    uint8_t  clut4[768];
};
#pragma pack(pop)

struct zz_scanout {
    uint8_t  dirty;          /* set on any change below; consumer clears     */
    uint8_t  enabled;
    uint16_t width, height;  /* visible geometry (after scale halving)       */
    uint8_t  colormode;      /* ZZ_CM_*                                      */
    uint32_t fb_off;         /* BOARD offset of the first visible pixel      */
    uint32_t pitch_bytes;    /* line stride in bytes                         */
};

struct zz_palette {
    uint8_t  dirty;          /* any entry changed since consumer cleared     */
    uint8_t  rgb[256][3];    /* primary CLUT, R G B                          */
    uint8_t  rgb2[256][3];   /* secondary CLUT (screen split)                */
};

struct zz_surface { uint32_t off, size; };   /* MemoryBase-relative          */
#define ZZ_MAX_SURFACES 256

struct zz_sprite {
    uint8_t  showing;                /* reg 0x48 = 1 / 2                     */
    int16_t  x, y;
    int16_t  x_off, y_off;           /* pan offset latch (hotspot+1 basis)   */
    uint16_t width, height;          /* raw fields incl. flag bits           */
    uint8_t  doubled, hires;
    uint32_t colors[4];              /* [0] = transparent key 0x00ff00ff     */
    uint8_t  pixels[32 * 48];        /* decoded pens, 0 = transparent        */
    uint8_t  pix_w, pix_h;           /* decoded dimensions                   */
    uint32_t hw_gen;
    /* draw/undraw compositor (D7) */
    uint8_t  drawn;
    uint16_t drawn_x, drawn_y, drawn_w, drawn_h;
    uint32_t drawn_fb_off, drawn_pitch;
    uint8_t  drawn_cm;
    uint8_t  under[32 * 2 * 48 * 4]; /* save-under, max sprite extent x 32bpp */
};

#ifndef ZZNET_SLOT_SIZE
#define ZZNET_SLOT_SIZE     2048u
#define ZZNET_SLOTS         16u
#define ZZNET_CTRL_MAGIC    0x00u
#define ZZNET_CTRL_TX_PROD  0x04u
#define ZZNET_CTRL_TX_CONS  0x08u
#define ZZNET_CTRL_RX_PROD  0x0Cu
#define ZZNET_CTRL_RX_CONS  0x10u
#define ZZNET_CTRL_MAIN_UP  0x14u
#define ZZNET_CTRL_MAC      0x18u
#define ZZNET_MAGIC         0x424D4E45u
#endif

struct zz_card {
    uint8_t  *vram;          /* board-offset-indexed backing                 */
    uint32_t backing;        /* physically backed bytes (<= ZZ_WINDOW_SIZE)  */
    uint32_t heap_end;       /* surface heap bound (board offset)            */

    uint16_t regs[0x808];    /* shadow latch: offsets 0x0000..0x100E         */

    /* legacy blitter latches (host-endian, Amiberry/fw transforms applied)  */
    uint16_t x[4], y[4];         /* X1 Y1 X2 Y2 X3 Y3 + spare                */
    uint16_t user[4];
    uint32_t src_off, dst_off;
    uint16_t src_pitch, row_pitch;
    uint8_t  colormode;
    uint8_t  fg_minterm;
    uint32_t fg, bg;             /* LE-native pen form (bswap32 of guest)    */

    /* mode / pan / split */
    uint16_t mode_id;
    uint8_t  scale_mode;
    uint32_t pan_off;            /* MemoryBase-relative visible base         */
    uint32_t pan_width;          /* virtual width in pixels                  */
    uint32_t split_pos, bgbuf_off;
    uint32_t pal_data;           /* +0x1000/+0x1002 staging                  */
    uint8_t  capture_mode;       /* +0x1006: 1 = native passthrough         */

    /* features / config */
    uint16_t config_key;
    uint8_t  feat_secondary_pal, feat_overlay, feat_dpms;

    uint8_t  int_enabled_vblank, int_enabled_eth;   /* CONFIG latches only  */

    uint8_t  *eth_win;
    volatile uint32_t *eth_ctrl;          /* ZZNET_CTRL_* words                */
    uint8_t  *eth_txring, *eth_rxring;    /* slot arrays                       */
    void     (*eth_inject)(void *ud);     /* raise EXTER toward the guest      */
    uint8_t   eth_mac[6];
    uint16_t  eth_rx_serial;              /* serial of the frame in the window */
    uint8_t   eth_int_pending;            /* CONFIG read bit0                  */
    uint16_t  eth_tx_status;              /* +0x80 readback, 0 = send OK       */
    uint32_t  eth_rx_cons;                /* fw-owned consumer index           */
    uint8_t   eth_win_full;
    uint8_t   eth_inject_req;

    struct zz_scanout  scanout;
    struct zz_palette  palette;
    struct zz_sprite   sprite;
    struct zz_surface  surfaces[ZZ_MAX_SURFACES];
    int      surface_count;

    /* platform hooks */
    uint32_t (*vblank_read)(void *ud);   /* 0x4C: nonzero = in vblank        */
    void     *ud;
    uint8_t  composite_sprite;
#ifdef EMU68_A3D
    void     (*a3d_fence)(void *ud, uint32_t board_off);
    uint32_t (*a3d_fence_of)(void *ud, uint32_t board_off);
    int      (*a3d_fence_try)(void *ud, uint32_t fence);
    void     (*a3d_fence_wait)(void *ud, uint32_t fence);
    int      (*a3d_wb_early)(void *ud);
    int      (*a3d_fence_kick)(void *ud, uint32_t fence);
    int      (*a3d_cover)(void *ud, const uint32_t *lo, const uint32_t *hi, unsigned n, uint32_t fence);
    void     (*a3d_uncover)(void *ud);
    uint8_t  a3d_defer;
#endif
    uint8_t  *vram_nc;
};

void     zz_card_init(struct zz_card *c, uint8_t *vram, uint32_t backing,
                      uint32_t heap_end_off);
/* 16-bit register access, offset is board-relative (0x0000..0x100E). */
void     zz_card_reg_write16(struct zz_card *c, uint32_t off, uint16_t val);
uint16_t zz_card_reg_read16(struct zz_card *c, uint32_t off);
void     zz_card_mmio_write(struct zz_card *c, uint32_t off, uint32_t val,
                            unsigned size);
uint32_t zz_card_mmio_read(struct zz_card *c, uint32_t off, unsigned size);

void     zz_sprite_refresh(struct zz_card *c);

void     zz_eth_pump(struct zz_card *c);
void     zz_eth_set_mac(struct zz_card *c, const uint8_t *mac);

#endif /* ZZ_CARD_H */
