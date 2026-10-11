/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* The m68k memory map, the DDR carve-out and the shared pages, in one place. */

#ifndef AMIGA_MEMMAP_H
#define AMIGA_MEMMAP_H

#define MMU_CACHED    0x4c06
#define MMU_CACHED_RO 0xcc06   /* + AP[2:0]=111 read-only (ROM staging) */
#define MMU_FASTRAM   0x4c06
#define MMU_DEVICE    0x0c06   /* Device / strongly-ordered, uncached (HPS periph, AXI seam)    */
#define MMU_TRAP      0x0000   /* translation fault -> Data-Abort -> ps_* over the seam         */

/* ---- Amiga regions, as 1 MB section indices (addr >> 20) ---- */
#define SEC_CHIP_LO      0x000   /* $000000-$0FFFFF chip RAM (Agnus)  */
#define SEC_CHIP_HI      0x001   /* $100000-$1FFFFF chip RAM          */
#define SEC_CPURAM_BASE  0x002   /* $200000 : Zorro-II CPU/Fast RAM window base (autoconfig-assigned) */
#define SEC_CIA_LO       0x00A   /* $A00000-$AFFFFF CIA space, low half - NOT decoded by this core   */
#define SEC_CIA          0x00B   /* $B00000-$BFFFFF CIA space, high half; only $BFxxxx is decoded    */
#define SEC_SLOW         0x00C   /* $C00000-$CFFFFF slow/Ranger       */
#define SEC_CUSTOM       0x00D   /* $D00000-$DFFFFF custom + RTC/IDE ($DFFxxx) */
#define SEC_AUTOCONF     0x00E   /* $E00000-$EFFFFF ext ROM / autoconfig ($E80000) */
#define SEC_ROM          0x00F   /* $F00000-$FFFFFF Kickstart ROM ($F80000)   */

#define Z2_AUTOCONF_MB   8u

#define SEC_FAST_TOP     SEC_CIA_LO

#define CHIP_RAM_BYTES   0x200000   /* 2 MB chip RAM (from SDRAM on DE10; unchanged) */

#if defined(CHIPSET_AXI)
#define EMU68_IMG_LOAD    0x28000000u   /* Emu68.img load + CPU#1 entry                 */
#define EMU68_IMG_GUARD   0x00800000u   /* 8 MB reserved for the load/relocation image  */
#define FASTRAM_DDR_BASE  0x28800000u   /* m68k Fast RAM $200000 -> here (clears the img)*/
#define EXTROM_DDR_BASE   0x3F600000u
#define EXTROM_SIZE       0x00080000u
#define EXTROM_DESC_OFFSET 0x5000u
#define EXTROM_DESC_MAGIC  0x58524D31u  /* 'XRM1' */
#define FEAT_DESC_OFFSET  0x5100u
#define FEAT_DESC_MAGIC   0x46545231u   /* 'FTR1' */
#define FEAT2_OFFSET      (FEAT_DESC_OFFSET + 8u)
#define FEAT2_LCNT_SHIFT  0
#define FEAT2_LCNT_MASK   0x0000000Fu
#define FEAT2_NO_BACKEDGE     0x00000010u
#define FEAT2_NO_OMMX_BANKS   0x00000020u
#define FEAT2_BLTPACE_SHIFT   8
#define FEAT2_BLTPACE_MASK    0x0000FF00u
#define FEAT2_BLTPACE_TUNED   0u
#define FEAT2_BLTPACE_OFF     255u      /* no wait */
#define FEAT2_BLTPACE_UNIT_NS 100u      /* codes 1..254 are this many ns each */
#define FEAT2_DTREE_ON        0x00010000u
#define FEAT2_A3D_WAIT_EXACT  0x00020000u
static inline unsigned feat2_dtree_offered(unsigned feats2, unsigned desc_ok)
{
    return (desc_ok && (feats2 & FEAT2_DTREE_ON)) ? 1u : 0u;
}
#define FEAT_OMMX_DECODE  0x00000001u   /* the OMMX row: decode $FC00-$FFFF (no announce) */
#define FEAT_OMMX_ANNOUNCE 0x00000002u  /* reserved: AttnFlags/vampire.resource - not implemented */
#define FEAT_FPU_OFF       0x00000004u
#define FEAT_CPU_68040     0x00000008u
#define FEAT_CPU_68060     0x00008000u
#define FEAT_RTG_OFF       0x00000010u
#define FEAT_AHI_OFF       0x00000020u
#define FEAT_JITSTATS_OFF  0x00000040u
#define FEAT_CHIP_COMPAT   0x00000080u
#define FEAT_JIT_DEPTH_SHIFT  8
#define FEAT_JIT_DEPTH_MASK   0x00000F00u
#define FEAT_IPL_POLL_SHIFT   12
#define FEAT_IPL_POLL_MASK    0x00007000u
#define FEAT_DBF_SLOWDOWN     0x00010000u
#define FEAT_CHIP_SLOW_SHIFT  17
#define FEAT_CHIP_SLOW_MASK   0x001E0000u
#define FEAT_BLITWAIT         0x00200000u
#define FEAT_BLITWAIT_RSVD    FEAT_BLITWAIT
#define FEAT_CCR_SCAN_SHIFT   22
#define FEAT_CCR_SCAN_MASK    0x01C00000u
#define FEAT_CACR_POLICY_SHIFT    25
#define FEAT_CACR_POLICY_MASK     0x06000000u
#define FEAT_NOCACHE_DEPTH_SHIFT  27
#define FEAT_NOCACHE_DEPTH_MASK   0x78000000u
#define FEAT_FP_RESIDENT_OFF      0x80000000u
#define LIVE_REQ_OFFSET   0x5140u
#define LIVE_ACK_OFFSET   0x5144u
#define LIVE_RB_OFFSET    0x5180u
#define LIVE_REQ_MAGIC    0x4C56u        /* 'LV' in [31:16] of the request word */
#define LIVE_ACK_MAGIC    0x4C41u        /* 'LA' in [31:16] of the ack word     */
#define LIVE_RB_MAGIC     0x4C565231u    /* 'LVR1', readback word [0]           */
#define A3D_STATS_OFFSET      0x52B0u
#define A3D_STATS_MAGIC       0x41334431u   /* 'A3D1' */
#define A3D_HCAP_UP           0x00000001u
#define A3D_HCAP_ONCALL       0x00000002u
#define ROM_DDR_BASE      0x3F700000u
#define KICKSTART_DDR     0x3F780000u   /* m68k $F80000 (512 KB Kickstart)               */
#define EMU68_KERNEL_DDR  0x3F800000u   /* Emu68 kernel/tlsf/JIT (8 MB, top)             */
#define Z3FAST_M68K_BASE  0x40000000u   /* m68k base of the 256 MB Z3 CPU RAM (Z3 space) */
#define Z3FAST_DDR_BASE   0x2A000000u   /* phys backing 0x2A000000-0x3A000000 (256 MB)   */
#define Z3FAST_SIZE_MB    256u

#define RTGVRAM_M68K_BASE 0x02000000u   /* m68k base, = the driver's MEMORY_BASE  */
#define RTGVRAM_DDR_BASE  0x27000000u   /* phys backing, = the driver's FB_BASE   */
#define RTGVRAM_SIZE_MB   8u            /* = the driver's MEMORY_SIZE ($800000)   */

#define SHARE_M68K_BASE   0x00DD4000u   /* m68k window base (8 KB, $DD4000-$DD5FFF)      */
#define SHARE_M68K_TOP    0x00DD6000u   /* exclusive end                                 */
#define SHARE_DDR_BASE    0x27FF4000u   /* phys, = Main's SHMEM_ADDR                     */
#define SHARE_FW_VA       0xD0000000u   /* firmware-private VA for the 1 MB section      */
#define SHARE_FW_PTR      (SHARE_FW_VA | (SHARE_DDR_BASE & 0x000FFFFFu))  /* -> the 8 KB */
#define SHARE_DB_M68K     0x00DD4800u   /* doorbell address inside the trapping flag page   */
#define SHARE_DB_MAGIC    0x53484442u   /* 'SHDB' - cmd block + mailbox magic                */
#define SHARE_MBOX_DDR    0x27FF3000u   /* Main-visible doorbell mailbox (1 page)            */
#define SHARE_MBOX_FW_PTR (SHARE_FW_VA | (SHARE_MBOX_DDR & 0x000FFFFFu))  /* fw NC view       */
#define SHARE_DB_FW_CAP_WORD 10u
#define SHARE_DB_FW_CAP   0x46574442u   /* 'FWDB' */
#define SHARE_DB_READ     1u
#define SHARE_DB_WRITE    2u
#define Z3RO_ALIAS_VA     0xD1000000u

#define BIGMIGHD_ROM_DDR_BASE 0x3E100000u
#define BIGMIGHD_ROM_SIZE_MB  1u

#define DTREE_ROM_DDR_BASE    0x3E200000u
#if (DTREE_ROM_DDR_BASE < BIGMIGHD_ROM_DDR_BASE + 0x100000u) || (DTREE_ROM_DDR_BASE + 0x100000u > 0x3F600000u)
#error DTREE_ROM_DDR_BASE has drifted out of the documented gap
#endif

#define FPRESIDENT_ROM_DDR_BASE 0x3E300000u
#if (FPRESIDENT_ROM_DDR_BASE < DTREE_ROM_DDR_BASE + 0x100000u) || (FPRESIDENT_ROM_DDR_BASE + 0x100000u > 0x3F600000u)
#error FPRESIDENT_ROM_DDR_BASE has drifted out of the documented gap
#endif

#define A3D_HEAP_FW_VA        0xD2000000u
#define A3D_HEAP_SIZE_MB      16u
#define A3D_HEAP_STAGE_MB     3u
#define A3D_HEAP_DDR_BASE     0x3E500000u
#if (A3D_HEAP_DDR_BASE < FPRESIDENT_ROM_DDR_BASE + 0x200000u) || (A3D_HEAP_DDR_BASE + (A3D_HEAP_SIZE_MB << 20) + 0x100000u > EXTROM_DDR_BASE)
#error A3D_HEAP_DDR_BASE has drifted out of the documented gap (keep a megabyte of daylight on each side)
#endif
#if (A3D_HEAP_FW_VA < Z3RO_ALIAS_VA + 0x100000u)
#error A3D_HEAP_FW_VA overlaps the firmware-private sections below it
#endif
#define A3D_HEAP2_SIZE_MB     12u
#define A3D_HEAP2_DDR_BASE    0x29200000u
#if (A3D_HEAP2_DDR_BASE < FASTRAM_DDR_BASE + 0x00A00000u) || (A3D_HEAP2_DDR_BASE + (A3D_HEAP2_SIZE_MB << 20) + 0x100000u > Z3FAST_DDR_BASE)
#error "A3D_HEAP2_DDR_BASE: a megabyte of daylight above the Z2 window (9 MB) and below the Z3 backing"
#endif
#define A3D_MGLC_ARENA_DDR_BASE  0x29E00000u
#define A3D_MGLC_ARENA_BYTES     0x00180000u
#define A3D_MGLC_ARENA_SECTIONS  2u
#if (A3D_MGLC_ARENA_DDR_BASE < A3D_HEAP2_DDR_BASE + (A3D_HEAP2_SIZE_MB << 20)) || (A3D_MGLC_ARENA_DDR_BASE & 0xFFFFFu) \
    || (A3D_MGLC_ARENA_DDR_BASE + A3D_MGLC_ARENA_BYTES + 0x80000u > Z3FAST_DDR_BASE) \
    || (A3D_MGLC_ARENA_BYTES > (A3D_MGLC_ARENA_SECTIONS << 20)) \
    || (A3D_MGLC_ARENA_DDR_BASE + (A3D_MGLC_ARENA_SECTIONS << 20) > Z3FAST_DDR_BASE)
#error "A3D_MGLC_ARENA: section-aligned, above the texture pool's second region, 512 KB of daylight below the Z3 backing"
#endif
#if (A3D_HEAP_FW_VA + ((A3D_HEAP_SIZE_MB + A3D_HEAP2_SIZE_MB) << 20) > 0xD4000000u)
#error "the 3D heap VA (both regions) reaches ZZ9000_VRAM_ALIAS_VA"
#endif
#define ZZ9000_VRAM_ALIAS_VA  0xD4000000u
#if (ZZ9000_VRAM_ALIAS_VA < A3D_HEAP_FW_VA + (A3D_HEAP_SIZE_MB << 20))
#error ZZ9000_VRAM_ALIAS_VA overlaps the 3D agent heap
#endif
#define ZZ9000_VRAM_NC_VA     0xD8000000u
#if (ZZ9000_VRAM_NC_VA < ZZ9000_VRAM_ALIAS_VA + 0x04000000u)
#error ZZ9000_VRAM_NC_VA overlaps the cached VRAM alias
#endif

#define HDFBOX_DDR_BASE   0x27FF2000u
#define HDFBOX_SIZE       0x1000u
#define HDFBOX_MAGIC      0x424D4844u   /* 'BMHD' */
#define HDFBOX_FW_PTR     (SHARE_FW_VA | (HDFBOX_DDR_BASE & 0x000FFFFFu))
#define HDF_VRAM_CAP      0x424D5652u   /* 'BMVR' */

#define ZZNET_SLOT_SIZE     2048u
#define ZZNET_SLOTS         16u
#define ZZNET_RINGS_DDR     0x28610000u
#define ZZNET_TX_RING_DDR   (ZZNET_RINGS_DDR)
#define ZZNET_RX_RING_DDR   (ZZNET_RINGS_DDR + ZZNET_SLOT_SIZE * ZZNET_SLOTS)
#define ZZNET_CTRL_DDR      (ZZNET_RINGS_DDR + 2u * ZZNET_SLOT_SIZE * ZZNET_SLOTS)
#define ZZNET_CTRL_MAGIC    0x00u   /* 'BMNE', firmware writes it at map()      */
#define ZZNET_CTRL_TX_PROD  0x04u   /* fw writes                                */
#define ZZNET_CTRL_TX_CONS  0x08u   /* Main writes                              */
#define ZZNET_CTRL_RX_PROD  0x0Cu   /* Main writes                              */
#define ZZNET_CTRL_RX_CONS  0x10u   /* fw writes (diagnostic mirror of its own) */
#define ZZNET_CTRL_MAIN_UP  0x14u   /* Main heartbeats the pump here            */
#define ZZNET_CTRL_MAC      0x18u   /* 6 bytes, guest MAC as latched (BE-ish)   */
#define ZZNET_MAGIC         0x424D4E45u  /* 'BMNE' */

#define UAESND_RING_DDR     0x28630000u
#define UAESND_RING_BYTES   0x10000u        /* 64 KB, power of two               */
#define UAESND_CTRL_DDR     0x28640000u
#define UAESND_CTRL_MAGIC   0x00u   /* 'BMAU', firmware writes it when armed     */
#define UAESND_CTRL_PROD    0x04u   /* fw writes: bytes produced, free-running   */
#define UAESND_CTRL_CONS    0x08u   /* Main writes: bytes consumed, free-running */
#define UAESND_CTRL_MAIN_UP 0x0Cu   /* Main heartbeats the pump here             */
#define UAESND_CTRL_UNDER   0x10u   /* fw: times the ring ran dry (diagnostic)   */
#define UAESND_CTRL_STATE   0x14u   /* fw: sticky discriminator, see uaesnd_ring.h */
#define UAESND_CTRL_TICKS   0x18u
#define UAESND_CTRL_MODE    0x1Cu   /* HOST writes: 0 silence, 1 sine, 2 ramp */
#define UAESND_MAGIC        0x424D4155u  /* 'BMAU' */

#define UAESND_STG_MAIN_CAP    0x70u  /* Main: 'B81a' = can mix from staging */
#define UAESND_STG_FW_ON       0x74u  /* fw: 1 = staging active (PCM ring off) */
#define UAESND_STG_CONS_FRAMES 0x78u
#define UAESND_STG_UNDER       0x7Cu  /* Main: mixed silence, staging ring dry */
#define UAESND_STG_MAGIC       0x42383161u  /* 'B81a' */

#define UAESND_STG_DESC_BASE   0x100u /* 8 x 32-byte per-stream descriptors */
#define UAESND_STG_DESC_STRIDE 0x20u
#define UAESND_STG_S_PROD      0x00u
#define UAESND_STG_S_CONS      0x04u  /* Main: bytes consumed, free-running   */
#define UAESND_STG_S_FMT       0x08u  /* fw: bit0 SIXTEEN, bit1 STEREO        */
#define UAESND_STG_S_STEP      0x0Cu  /* fw: resample step, 16.16 (src/48000) */
#define UAESND_STG_S_VOL       0x10u  /* fw: (volR<<16)|volL, 0..255 each     */
#define UAESND_STG_S_EPOCH     0x14u  /* fw: bumps on any param/buffer change */
#define UAESND_STG_S_EPOCH_BASE 0x18u /* fw: S_PROD at the moment of the bump */
#define UAESND_STG_S_FLAGS     0x1Cu  /* fw: bit0 ACTIVE (mix it) */

#define UAESND_STG_STRIDE      0x2000u /* 8 KB of raw source bytes per stream */

#define MHI_CTRL_DDR        0x28650000u   /* control page (4 KB used)               */
#define MHI_SRC_RING_DDR    0x28660000u   /* compressed MP3 bytes, fw->Main         */
#define MHI_SRC_RING_BYTES  0x10000u      /* 64 KB, power of two                    */
#define MHI_PCM_RING_DDR    0x28670000u   /* 48 kHz S16 stereo PCM, Main->fw        */
#define MHI_PCM_RING_BYTES  0x10000u      /* 64 KB = 16384 frames (~341 ms), pow2   */
#define MHI_PCM_RING_FRAMES (MHI_PCM_RING_BYTES / 4u)   /* stereo S16 = 4 bytes/frame */
#define MHI_CTRL_MAGIC      0x00u
#define MHI_CTRL_EPOCH      0x04u
#define MHI_CTRL_SRC_PROD   0x08u
#define MHI_CTRL_SRC_CONS   0x0Cu   /* Main compressed bytes consumed, free-running  */
#define MHI_CTRL_PCM_PROD   0x10u   /* Main 48 kHz stereo frames produced, free-run  */
#define MHI_CTRL_PCM_CONS   0x14u
#define MHI_CTRL_EOF        0x18u
#define MHI_CTRL_DEC_HZ     0x1Cu   /* Main decoded native rate (telemetry)          */
#define MHI_CTRL_MAIN_UP    0x20u   /* Main decode-thread heartbeat                  */
#define MHI_CTRL_UNDER      0x24u
#define MHI_CTRL_GAIN       0x28u
#define MHI_CTRL_MAGIC_VAL  0x4D484932u   /* 'MHI2' - rev-2 ring protocol present    */
#define MHI_CTRL_PAUSED       0x2Cu
#define MHI_CTRL_EPOCH_SRC    0x30u
#define MHI_CTRL_FW_REV       0x34u   /* fw   MHI_CTRL_FW_REV_VAL: PAUSED + EPOCH_SRC are published (0 = older) */
#define MHI_CTRL_PLAYED_EPOCH 0x38u   /* Main pump: the epoch PLAYED counts in                                  */
#define MHI_CTRL_PLAYED       0x3Cu   /* Main pump: 48 kHz frames taken from the PCM ring for the sink in it    */
#define MHI_CTRL_DEC_CAPS     0x40u   /* Main decoder: bit0 Layer I, bit1 Layer II, bit2 Layer III              */
#define MHI_CTRL_SINK_LEAD    0x44u   /* Main pump: frames the active leg keeps in /dev/MrAudio                 */
#define MHI_CTRL_DEC_EPOCH    0x48u   /* Main decoder (core#0 internal: the pump's await rule)                  */
#define MHI_CTRL_DEC_EPOCH_BASE 0x4Cu /* Main decoder (core#0 internal)                                         */
#define MHI_CTRL_MAIN_REV     0x50u   /* Main decoder: the Main's ctrl-page revision (2)                        */
#define MHI_CTRL_FW_REV_VAL   2u

#define JITPOOL_DDR_BASE  0x3A000000u   /* phys backing 0x3A000000-0x3E000000 (64 MB)    */
#ifndef JITPOOL_SIZE_MB
#define JITPOOL_SIZE_MB   64u
#endif
#if (JITPOOL_DDR_BASE + ((JITPOOL_SIZE_MB) * 1024u * 1024u)) > BIGMIGHD_ROM_DDR_BASE
#error "JITPOOL_SIZE_MB overruns BIGMIGHD_ROM_DDR_BASE — the JIT pool would allocate on top of the driver image the guest executes.  Max is (BIGMIGHD_ROM_DDR_BASE - JITPOOL_DDR_BASE) >> 20 MB."
#endif

#define M68K_BOOT_SSP     (((SEC_FAST_TOP) << 20) - 4u)
#else
extern volatile unsigned int emu68_health_scratch[0x1800];
#define SHARE_M68K_BASE   0x00DD4000u                       /* == TOP: empty window */
#define SHARE_M68K_TOP    0x00DD4000u
#define SHARE_FW_PTR      ((void *)emu68_health_scratch)     /* never reached */
#define SHARE_DDR_BASE    0u
#define SHARE_FW_VA       0u
#define M68K_BOOT_SSP     (((SEC_FAST_TOP) << 20) - 4u)
#endif  /* DDR-base block (AXI || DB || DOORBELL) */

#if defined(CHIPSET_AXI)
#define LAUNCH_HEALTH_MIRROR_VA 0xFFFF0000u
#else
extern volatile unsigned int emu68_health_scratch[0x1800];
#define LAUNCH_HEALTH_MIRROR_VA ((uintptr_t)emu68_health_scratch)
#endif

static inline int amiga_is_seam_section(unsigned sec)
{
#if defined(CHIPSET_AXI) || defined(SEAM_CHIPRAM)
    if (sec == SEC_CHIP_LO || sec == SEC_CHIP_HI) return 1;   /* chip RAM via seam */
#endif
    return (sec == SEC_CIA) || (sec == SEC_CUSTOM);          /* $B / $D always trap */
}

static inline int amiga_is_seam_addr(unsigned int addr)
{
    unsigned sec = addr >> 20;
#if defined(CHIPSET_AXI) || defined(SEAM_CHIPRAM)
    if (sec == SEC_CHIP_LO || sec == SEC_CHIP_HI) return 1;   /* chip RAM via seam */
#endif
    if (sec == SEC_CIA) {                                 /* $B00000-$BFFFFF */
        if ((addr & 0x000F0000u) == 0x00080000u) return 1; /*   $B8xxxx Akiko/RTG */
        return (addr & 0x000F0000u) == 0x000F0000u;
    }
    if (sec == SEC_CUSTOM) {                              /* $D00000-$DFFFFF */
        switch (addr & 0x000F0000u) {
            case 0x000A0000u: return 1;                   /*   $DAxxxx IDE     */
            case 0x000C0000u: return 1;                   /*   $DCxxxx RTC     */
            case 0x000E0000u:                             /*   $DE1xxx Gayle   */
                return (addr & 0x0000F000u) == 0x00001000u;
            case 0x000F0000u: return 1;                   /*   $DFxxxx custom  */
            default:          return 0;
        }
    }
    return 0;
}

static inline int amiga_is_fixed_bank_addr(unsigned int addr)
{
    unsigned sec;
    if (addr >> 24) return 0;                     /* canonical $00xxxxxx only     */
    sec = addr >> 20;
    if (sec != SEC_CIA && sec != SEC_CUSTOM)      /* chip/fast/ROM/autoconfig out  */
        return 0;
    if (sec == SEC_CIA && (addr & 0x000F0000u) == 0x00080000u)
        return 0;                                 /* $B8xxxx Akiko/RTG             */
    if (addr == 0x00BFE001u || addr == 0x00DFF016u ||
        addr == 0x00DFF080u || addr == 0x00DFF088u)
        return 0;
    return amiga_is_seam_addr(addr);              /* THE shared decoder list       */
}

#endif /* AMIGA_MEMMAP_H */
