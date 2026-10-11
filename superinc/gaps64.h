/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* 64-bit A64 operations reduced to A32. */

#ifndef _ARMHF_GAPS64_H
#define _ARMHF_GAPS64_H

static inline uint32_t add64_reg(uint8_t rd, uint8_t rn, uint8_t rm, shift_t shift, uint8_t amount)
{
    if (rn == 31 && shift == LSR && amount == 32)
        return mov_reg(rd & 15, (rm + 1) & 15);   // MOV rd, <high-word reg> (smull/umull RdHi)
    return _UNDEF;                                 // any other shape: not a single A32 word
}

static inline uint32_t asrv64(uint8_t rd, uint8_t rn, uint8_t rm) {
    return INSN_TO_LE((ARM_CC_AL << 28) | 0x01a00050 | ((rd & 15) << 12) | ((rm & 15) << 8) | (rn & 15));
}

static inline uint32_t lsr64(uint8_t rd, uint8_t rn, uint8_t lsb)
{
    rd &= 15; rn &= 15;
    if (lsb < 32)  return lsr_immed(rd, rn, lsb);                            // LSR rd, rn,   #lsb
    if (lsb == 32) return mov_reg(rd, (rn + 1) & 15);                        // MOV rd, rn+1  (whole high word)
    if (lsb < 64)  return lsr_immed(rd, (rn + 1) & 15, (uint8_t)(lsb - 32)); // LSR rd, rn+1, #(lsb-32)
    return _UNDEF;
}

static inline uint32_t ror64(uint8_t rd, uint8_t rn, uint8_t lsb) {
    uint8_t amt = lsb & 31;
    return amt ? ror_immed(rd & 15, rn & 15, amt)   // A32 ROR rd,rn,#(lsb&31)
               : mov_reg(rd & 15, rn & 15);          // (lsb&31)==0 -> identity (NOT RRX)
}

static inline uint32_t clz64(uint8_t rd, uint8_t rn) { return clz(rd, rn); }

static inline uint32_t rbit64(uint8_t rd, uint8_t rn) { return rbit(rd, rn); }

static inline uint32_t sbfx64(uint8_t rd, uint8_t rn, uint8_t lsb, uint8_t width) {
    if ((unsigned)lsb + (unsigned)width <= 32u)
        return sbfx(rd, rn, lsb, width);   // A32 SBFX rd,rn,#lsb,#width (low-32 field)
    return _UNDEF;
}

static inline uint32_t bfi64(uint8_t rd, uint8_t rn, uint8_t lsb, uint8_t width) {
    if ((uint32_t)lsb + (uint32_t)width <= 32 && width >= 1)
        return bfi(rd, rn, lsb, width);   // A32 BFI rd,rn,#lsb,#width (1 word, low-32 reducible)
    return _UNDEF;
}

static inline uint32_t _a32_logmask64(uint8_t width, uint8_t ror, uint8_t n) {
    if (n) {  // 64-bit element: `width` low ones, rotated right by `ror` within 64 bits
        uint64_t v = (width >= 64) ? ~0ULL : ((1ULL << width) - 1ULL);
        ror &= 63;
        if (ror) v = (v >> ror) | (v << (64 - ror));
        return (uint32_t)v;  // low 32 bits - all the A32 register holds
    }
    return _a32_logmask(width, ror);
}
static inline uint32_t ands64_immed(uint8_t rd, uint8_t rn, uint8_t width, uint8_t ror, uint8_t n) {
    uint16_t f;
    return _a32_imm12(_a32_logmask64(width, ror, n), &f) ? _DPIMM(0x02100000, rd, rn, f) : _UNDEF;
}

static inline uint32_t orr64_immed(uint8_t rd, uint8_t rn, uint8_t width, uint8_t ror, uint8_t n)
{
    uint64_t v;
    if (!imm64_decode_bm(width, ror, n, &v)) return _UNDEF;   // reserved/UNALLOCATED triple
    if ((uint32_t)(v >> 32) != 0)            return _UNDEF;   // mask reaches bits[63:32] -> not one A32 word
    uint16_t f;
    if (_a32_imm12((uint32_t)v, &f))
        return _DPIMM(0x03800000, rd, rn, f);                 // ORR Rd, Rn, #mask
    return _UNDEF;                                            // low-32 but not imm8-ROR encodable
}

static inline uint32_t stlxrb(uint8_t rn, uint8_t rt, uint8_t rs)
{
    return strexb(rn & 15, rt & 15, rs & 15);   // STREXB Rs, Rt, [Rn]
}

static inline uint32_t stlxr(uint8_t rn, uint8_t rt, uint8_t rs)
{
    return strex(rn & 15, rt & 15, rs & 15);   // STREX Rs, Rt, [Rn]
}

static inline void EMIT_ldr64_offset(uint32_t **s, uint8_t rn, uint8_t rt, uint16_t offset15)
{
    uint32_t off = (uint32_t)offset15;                 // A64 offset15 is already a byte offset
    if ((rn & 31) == 31) {
        if ((rt & 31) != 30) *(*s)++ = _UNDEF;
        return;
    }
    if ((off + 4u) > 0xfffu) {                         // un-encodable immediate
        *(*s)++ = _UNDEF;                              // trap rather than mis-encode
        return;
    }
    *(*s)++ = ldr_offset(rn & 15, rt & 15, (int16_t)off);                        // LDR rt,   [rn,#off]   (low)
    *(*s)++ = ldr_offset(rn & 15, (uint8_t)((rt + 1) & 15), (int16_t)(off + 4)); // LDR rt+1, [rn,#off+4] (high)
}

static inline void EMIT_str64_offset(uint32_t **s, uint8_t rn, uint8_t rt, uint16_t offset15)
{
    if ((rn & 31) == 31) {
        if ((rt & 31) != 30) *(*s)++ = _UNDEF;
        return;
    }
    *(*s)++ = str_offset(rn & 15, rt & 15, (int16_t)offset15);                        // low  word
    *(*s)++ = str_offset(rn & 15, (uint8_t)((rt + 1) & 15), (int16_t)(offset15 + 4)); // high word
}

static inline void EMIT_stur64_offset(uint32_t **s, uint8_t rn, uint8_t rt,
                                      int16_t offset9)
{
    int32_t off = shim_sext9(offset9);
    *(*s)++ = str_offset(rn & 15, rt & 15,              (int16_t)off);       // STR rt,  [rn,#off]   (low word)
    *(*s)++ = str_offset(rn & 15, (uint8_t)((rt+1)&15), (int16_t)(off + 4)); // STR rt+1,[rn,#off+4] (high word)
}

static inline uint32_t str64_offset_preindex(uint8_t rn, uint8_t rt, int16_t imm) {
    return str_offset_preindex((rn == 31) ? 13 : (rn & 15), rt & 15, imm);   // STR rt,[sp,#imm]!
}
static inline uint32_t ldr64_offset_postindex(uint8_t rn, uint8_t rt, int16_t imm) {
    return ldr_offset_postindex((rn == 31) ? 13 : (rn & 15), rt & 15, imm);   // LDR rt,[sp],#imm
}

static inline void EMIT_stp64_preindex(uint32_t **s, uint8_t rn, uint8_t rt1, uint8_t rt2, int16_t imm) {
    (void)rt1; (void)rt2; (void)imm;
    *(*s)++ = ((rn & 31) == 31) ? push(0x5fff) : _UNDEF;   // PUSH {r0-r12,lr} ; else 64-bit DATA (deferred)
}
static inline void EMIT_ldp64_postindex(uint32_t **s, uint8_t rn, uint8_t rt1, uint8_t rt2, int16_t imm) {
    (void)rt1; (void)rt2; (void)imm;
    *(*s)++ = ((rn & 31) == 31) ? pop(0x5fff) : _UNDEF;    // POP {r0-r12,lr} ; else 64-bit DATA (deferred)
}
static inline void EMIT_stp64(uint32_t **s, uint8_t rn, uint8_t rt1, uint8_t rt2, int16_t imm) {
    (void)rt1; (void)rt2; (void)imm;
    if ((rn & 31) != 31) *(*s)++ = _UNDEF;
}
static inline void EMIT_ldp64(uint32_t **s, uint8_t rn, uint8_t rt1, uint8_t rt2, int16_t imm) {
    (void)rt1; (void)rt2; (void)imm;
    if ((rn & 31) != 31) *(*s)++ = _UNDEF;
}

static inline uint32_t lsl64(uint8_t rd, uint8_t rn, uint8_t lsb)
{
    if (lsb < 32) return lsl_immed(rd & 15, rn & 15, lsb);   // LSL rd, rn, #lsb (low-32 faithful)
    return _UNDEF;
}

static inline uint32_t asr64(uint8_t rd, uint8_t rn, uint8_t lsb)
{
    if (lsb >= 1 && lsb < 32) return asr_immed(rd & 15, rn & 15, lsb);   // ASR rd, rn, #lsb (low-32)
    return _UNDEF;                                                       // lsb 0 (reserved) or >=32 (upper half) -> handler-ported
}

static inline uint32_t eor64_immed(uint8_t rd, uint8_t rn, uint8_t width, uint8_t ror, uint8_t n)
{
    uint64_t v;
    if (!imm64_decode_bm(width, ror, n, &v)) return _UNDEF;   // reserved/UNALLOCATED triple
    if ((uint32_t)(v >> 32) != 0)            return _UNDEF;    // mask reaches bits[63:32] -> not one A32 word
    uint16_t f;
    if (_a32_imm12((uint32_t)v, &f))
        return _DPIMM(0x02200000, rd, rn, f);                 // EOR Rd, Rn, #mask
    return _UNDEF;                                            // low-32 but not imm8-ROR encodable
}

static inline uint32_t bic64_immed(uint8_t rd, uint8_t rn, uint8_t width, uint8_t ror, uint8_t n)
{
    uint64_t v;
    if (!imm64_decode_bm(width, ror, n, &v)) return _UNDEF;   // reserved/UNALLOCATED triple
    if ((uint32_t)(v >> 32) != 0)            return _UNDEF;    // clears an upper-half bit -> genuine-64, not one A32 word
    uint16_t f;
    if (_a32_imm12((uint32_t)v, &f))
        return _DPIMM(0x03c00000, rd, rn, f);                 // BIC Rd, Rn, #mask  (clear the masked bits)
    return _UNDEF;                                            // low-32 but not imm8-ROR encodable
}

static inline uint32_t tst64_immed(uint8_t rn, uint8_t width, uint8_t ror, uint8_t n)
{
    uint64_t v;
    if (!imm64_decode_bm(width, ror, n, &v)) return _UNDEF;   // reserved/UNALLOCATED triple
    if ((uint32_t)(v >> 32) != 0)            return _UNDEF;    // tested bit in bits[63:32] -> flags need the upper word, handler-ported
    uint16_t f;
    if (_a32_imm12((uint32_t)v, &f))
        return _DPIMM(0x03100000, 0, rn, f);                  // TST Rn, #mask (Rd=0, S baked in)
    return _UNDEF;                                            // low-32 but not imm8-ROR encodable
}

static inline uint32_t orn64_reg(uint8_t rd, uint8_t rn, uint8_t rm, shift_t shift, uint8_t amount)
{
    (void)rd; (void)rn; (void)rm; (void)shift; (void)amount;
    return _UNDEF;   // no single-word A32 ORN; every call site is genuine-64 BFFFO straddle -> handler-ported
}

static inline void EMIT_ldur64_offset(uint32_t **s, uint8_t rn, uint8_t rt, int16_t offset9)
{
    if ((rn & 31) == 31) { *(*s)++ = _UNDEF; return; }
    int32_t off = shim_sext9(offset9);
    *(*s)++ = ldr_offset(rn & 15, rt & 15,                (int16_t)off);       // LDR rt,  [rn,#off]   (low word)
    *(*s)++ = ldr_offset(rn & 15, (uint8_t)((rt + 1) & 15), (int16_t)(off + 4)); // LDR rt+1,[rn,#off+4] (high word)
}

#ifndef REG_PROTECT
#define REG_PROTECT ((1u << 10) | (1u << 11) | (1u << 12))   /* SR | CTX | PC (armhf) */
#endif

#endif // _ARMHF_GAPS64_H
