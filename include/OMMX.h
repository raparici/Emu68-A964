/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* OMMX: the vector extension's registers, encodings and the ONCALL interface to native code. */

#ifndef _OMMX_H
#define _OMMX_H

#include <stdint.h>

extern int EnableOMMX;

/* word-1 fields */
#define OMMX_W1_BLOCK_MASK   0xfc00u
#define OMMX_W1_BLOCK        0xfc00u     /* both blocks: (opcode & 0xfc00) == 0xfc00      */
#define OMMX_W1_IS_AMMX(w)   (((w) & 0x0200u) != 0)   /* bit 9: 1 = $FE (AMMX), 0 = $FC (OMMX) */
#define OMMX_W1_A(w)         (((w) >> 8) & 1u)
#define OMMX_W1_B(w)         (((w) >> 7) & 1u)
#define OMMX_W1_D(w)         (((w) >> 6) & 1u)
#define OMMX_W1_VEA(w)       ((w) & 0x3fu)
#define OMMX_VEA_VPERM       0x3fu
/* word-2 fields */
#define OMMX_W2_REGB(w)      (((w) >> 12) & 0xfu)
#define OMMX_W2_REGD(w)      (((w) >> 8) & 0xfu)
#define OMMX_W2_OP(w)        ((w) & 0xffu)

/* the AMMX opcode table (PRM pages in brackets); identical numbers in the OMMX block */
enum {
    OMMX_OP_VPERM     = 0x00,   /* [78] #n,a,b,d - VEA=111111                                   */
    OMMX_OP_LOAD      = 0x01,   /* [47] <vea>,d ; LOADI = same with REG-B==1 [48]              */
    OMMX_OP_TRANSHI   = 0x02,   /* [76] a0-a3,d:d2 (group in w1 bits 3:2)                      */
    OMMX_OP_TRANSLO   = 0x03,   /* [76]                                                         */
    OMMX_OP_STORE     = 0x04,   /* [68] b,<vea> ; STOREI = same with REG-D==1 [70]             */
    OMMX_OP_STOREM    = 0x05,
    OMMX_OP_PACKUSWB  = 0x06,   /* [52] b,d,<vea>                                               */
    OMMX_OP_PACK3216  = 0x07,   /* [51] b,d,<vea>                                               */
    OMMX_OP_PAND      = 0x08,   /* [54]                                                         */
    OMMX_OP_POR       = 0x09,   /* [66]                                                         */
    OMMX_OP_PEOR      = 0x0a,   /* [59]                                                         */
    OMMX_OP_PANDN     = 0x0b,   /* [55] d = ~a & b                                              */
    OMMX_OP_PAVGB     = 0x0c,   /* [56]                                                         */
    OMMX_OP_PADDB     = 0x10,
    OMMX_OP_PADDW     = 0x11,
    OMMX_OP_PSUBB     = 0x12,   /* [67] 0001 0U1S - d = b - a                                   */
    OMMX_OP_PSUBW     = 0x13,
    OMMX_OP_PADDUSB   = 0x14,
    OMMX_OP_PADDUSW   = 0x15,
    OMMX_OP_PSUBUSB   = 0x16,
    OMMX_OP_PSUBUSW   = 0x17,
    OMMX_OP_PMUL88    = 0x18,   /* [64] 0001 10TT: T=0 pmul88, 1 pmula, 2 pmulh, 3 pmull        */
    OMMX_OP_PMULA     = 0x19,
    OMMX_OP_PMULH     = 0x1a,
    OMMX_OP_PMULL     = 0x1b,
    OMMX_OP_BFLYB     = 0x1c,   /* [44] <vea>,b,d:d2                                            */
    OMMX_OP_BFLYW     = 0x1d,
    OMMX_OP_UNPACK1632= 0x1e,
    OMMX_OP_PCMP_BASE = 0x20,   /* [57/58] 0010 CCC S: CC eq=000 hi=001 ge=110 gt=111          */
    OMMX_OP_STOREC    = 0x24,   /* [69] b,count,<vea>                                            */
    OMMX_OP_STOREILM  = 0x25,
    OMMX_OP_STOREM3   = 0x26,   /* b,#mode,<vea>: mode in the d field (vasm: `storem3 d0,d3,(a0)`)   */
    OMMX_OP_C2P       = 0x28,   /* [46]                                                         */
    OMMX_OP_BSEL      = 0x29,   /* [45] <vea>,mask,d                                            */
    OMMX_OP_MINTERM   = 0x2a,   /* [50]                                                         */
    OMMX_OP_PMIN_BASE = 0x30,   /* [62/63] 0011 00US                                            */
    OMMX_OP_PMAX_BASE = 0x34,   /* [60/61] 0011 01US                                            */
    OMMX_OP_LSLQ      = 0x38,   /* [49] 0011 100d                                               */
    OMMX_OP_LSRQ      = 0x39,
    OMMX_OP_TEX       = 0x3e,   /* [74/75] broken on real cores per the PRM - never            */
    OMMX_OP_OADD_L    = 0x42,
    OMMX_OP_OSUB_L    = 0x46,
    OMMX_OP_OMUL_L    = 0x4a,
    OMMX_OP_OMLA_L    = 0x4e,   /* Ed[i] += low32(Eb[i] * a[i]) - Ed read AND written           */
    OMMX_OP_OSRAR_L   = 0x52,
    OMMX_OP_OPADD_L   = 0x56,
    OMMX_OP_OSRA_L    = 0x5a,
    OMMX_OP_OIDENT    = 0xe0,   /* OIDENT Dn                                                    */
    OMMX_OP_ONCALL    = 0xe1,   /* ONCALL #id - reserved, traps in v1                           */
};

/* OIDENT capability word */
#define OMMX_IDENT_VERSION      1u
#define OMMX_IDENT_REGCOUNT     32u
#define OMMX_FEAT_AMMX_BLOCK    (1u << 0)
#define OMMX_FEAT_OMMX_BLOCK    (1u << 1)
#define OMMX_FEAT_FLOAT32       (1u << 2)
#define OMMX_FEAT_FUSED_MAC     (1u << 3)
#define OMMX_FEAT_DIV_SQRT      (1u << 4)
#define OMMX_FEAT_ONCALL        (1u << 5)
#define OMMX_FEAT_V128          (1u << 6)
#define OMMX_IDENT_WORD         ((OMMX_IDENT_VERSION << 24) | (OMMX_IDENT_REGCOUNT << 16) | OMMX_FEAT_AMMX_BLOCK | OMMX_FEAT_OMMX_BLOCK)

#define OMMX_ONCALL_W1          0xFD3Cu
#define OMMX_ONCALL_W2          0x00E1u
extern volatile uint32_t g_oncall_on;
extern volatile uint32_t g_oncall_trap;

/* register numbering used inside the JIT: 0-7 = D0-D7 (aliased), 8-15 = E0-E7, 16-31 = E8-E23 */
#define OMMX_REG_D0    0
#define OMMX_REG_E0    8
#define OMMX_REG_E8    16
#define OMMX_REG_IS_FAST(r)   ((r) >= OMMX_REG_E8)

/* decode helpers shared with the length / SR-mask code (pure functions of the two words) */
static inline int OMMX_IsBlock(uint16_t w1) { return (w1 & OMMX_W1_BLOCK_MASK) == OMMX_W1_BLOCK; }
static inline int OMMX_IsVperm(uint16_t w1, uint16_t w2) { return OMMX_W1_VEA(w1) == OMMX_VEA_VPERM && (w2 & 0xf0u) == 0; }
/* register number for a REG-B / REG-D field with its bank bit */
static inline uint8_t OMMX_RegFromField(uint8_t field4, uint8_t bank) { return bank ? (uint8_t)(16 + field4) : field4; }
static inline uint8_t OMMX_RegFromVea(uint8_t vea, uint8_t A) { return (uint8_t)((A ? 16 : 0) + ((vea >> 3) & 1) * 8 + (vea & 7)); }

int OMMX_IsImplemented(uint16_t w1, uint16_t w2);

/* the emitter (src/M68k_OMMX.c) */
uint32_t *EMIT_OMMX(uint32_t *ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed);
void OMMX_UnitStart(void);

struct M68KState;
#define OMMX_BANK_SLOTS 64                       /* tasks with OMMX state held at once (LRU beyond) */
extern volatile uint32_t g_ommx_banks_armed;
extern volatile uint32_t g_ommx_banks_off;
void     ommx_banks_arm(void);                                   /* translator: first touched unit   */
void     ommx_banks_switch_point(struct M68KState *m68k, int via_sr);
void     ommx_banks_reset(struct M68KState *m68k);               /* m68k reset (jit_levers_reset)   */
void     ommx_banks_set_off(uint32_t off);
uint32_t ommx_banks_slots_used(void);
int      ommx_banks_slot_of(uint32_t task);
uint32_t ommx_banks_owner(void);
int      ommx_guest_read32(uint32_t addr, uint32_t *val);

#endif /* _OMMX_H */
