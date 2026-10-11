/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* A32 ALU encoders with the A64 signatures. */

#ifndef _FWD_ALU_H
#define _FWD_ALU_H

#ifndef _SHIMS_SHIFT_T_DEFINED
#define _SHIMS_SHIFT_T_DEFINED
typedef enum { LSL = 0, LSR = 1, ASR = 2, ROR = 3 } shift_t;   // == A32 [6:5] field
#endif

#define _DPREG(opc,rd,rn,rm,sh,amt)  INSN_TO_LE((ARM_CC_AL<<28)|(opc)|(((rn)&15)<<16)|(((rd)&15)<<12)|(((amt)&31)<<7)|(((sh)&3)<<5)|((rm)&15))
#define _DPTEST(opc,rn,rm,sh,amt)    INSN_TO_LE((ARM_CC_AL<<28)|(opc)|(((rn)&15)<<16)|(((amt)&31)<<7)|(((sh)&3)<<5)|((rm)&15))
#define _DPIMM(opc,rd,rn,f)          INSN_TO_LE((ARM_CC_AL<<28)|(opc)|(((rn)&15)<<16)|(((rd)&15)<<12)|(f))

// A64 logical-immediate is (width, ror): a run of `width` low ones rotated right by `ror`.
static inline uint32_t _a32_logmask(uint8_t width, uint8_t ror) {
    uint32_t v = (width >= 32) ? 0xffffffffu : ((1u << width) - 1u);
    return ror ? ((v >> ror) | (v << (32 - ror))) : v;
}
// value -> A32 rotated-imm8 12-bit field; returns 0 if not single-instruction-encodable.
static inline int _a32_imm12(uint32_t v, uint16_t *f) {
    for (int r = 0; r < 16; r++) {
        uint32_t rol = (r == 0) ? v : ((v << (2 * r)) | (v >> (32 - 2 * r)));
        if (rol <= 0xff) { *f = (uint16_t)((r << 8) | rol); return 1; }
    }
    return 0;
}
#define _UNDEF INSN_TO_LE(0xe7f000f0)   // permanently-undefined: traps a non-encodable immediate

// ---- register forms : (rd, rn, rm, shift_t, amount) ----
static inline uint32_t add_reg (uint8_t rd,uint8_t rn,uint8_t rm,shift_t sh,uint8_t a){return _DPREG(0x00800000,rd,rn,rm,sh,a);}
static inline uint32_t adds_reg(uint8_t rd,uint8_t rn,uint8_t rm,shift_t sh,uint8_t a){return _DPREG(0x00900000,rd,rn,rm,sh,a);}
static inline uint32_t sub_reg (uint8_t rd,uint8_t rn,uint8_t rm,shift_t sh,uint8_t a){return _DPREG(0x00400000,rd,rn,rm,sh,a);}
static inline uint32_t subs_reg(uint8_t rd,uint8_t rn,uint8_t rm,shift_t sh,uint8_t a){return _DPREG(0x00500000,rd,rn,rm,sh,a);}
static inline uint32_t and_reg (uint8_t rd,uint8_t rn,uint8_t rm,shift_t sh,uint8_t a){return _DPREG(0x00000000,rd,rn,rm,sh,a);}
static inline uint32_t ands_reg(uint8_t rd,uint8_t rn,uint8_t rm,shift_t sh,uint8_t a){
  if ((rd & 31)==31) return _DPTEST(0x01100000,rn,rm,sh,a);   /* TST rn,rm,<sh> #a */
  return _DPREG(0x00100000,rd,rn,rm,sh,a);
}
static inline uint32_t orr_reg (uint8_t rd,uint8_t rn,uint8_t rm,shift_t sh,uint8_t a){return _DPREG(0x01800000,rd,rn,rm,sh,a);}
static inline uint32_t eor_reg (uint8_t rd,uint8_t rn,uint8_t rm,shift_t sh,uint8_t a){return _DPREG(0x00200000,rd,rn,rm,sh,a);}
static inline uint32_t bic_reg (uint8_t rd,uint8_t rn,uint8_t rm,shift_t sh,uint8_t a){return _DPREG(0x01c00000,rd,rn,rm,sh,a);}
static inline uint32_t mvn_reg (uint8_t rd,uint8_t rm,shift_t sh,uint8_t a){return _DPREG(0x01e00000,rd,0,rm,sh,a);}  // MVN: no Rn

// ---- compare/test (Rd=WZR on A64) : (rn, rm, shift_t, amount) ----
static inline uint32_t cmn_reg(uint8_t rn,uint8_t rm,shift_t sh,uint8_t a){
  if ((rn & 31)==31 && sh==LSL && a==0)
    return INSN_TO_LE((ARM_CC_AL<<28)|0x03500000|((uint32_t)(rm&15)<<16)); /* CMP rm,#0 */
  return _DPTEST(0x01700000,rn,rm,sh,a);
}
static inline uint32_t cmp_reg(uint8_t rn,uint8_t rm,shift_t sh,uint8_t a){return _DPTEST(0x01500000,rn,rm,sh,a);}
static inline uint32_t tst_reg(uint8_t rn,uint8_t rm,shift_t sh,uint8_t a){return _DPTEST(0x01100000,rn,rm,sh,a);}

// ---- logical immediate : (rd, rn, width, ror) [tst: (rn,width,ror)] ----
static inline uint32_t and_immed (uint8_t rd,uint8_t rn,uint8_t w,uint8_t r){uint16_t f;return _a32_imm12(_a32_logmask(w,r),&f)?_DPIMM(0x02000000,rd,rn,f):_UNDEF;}
static inline uint32_t ands_immed(uint8_t rd,uint8_t rn,uint8_t w,uint8_t r){uint16_t f;if((rd&31)==31){return _a32_imm12(_a32_logmask(w,r),&f)?_DPIMM(0x03100000,0,rn,f):_UNDEF;}return _a32_imm12(_a32_logmask(w,r),&f)?_DPIMM(0x02100000,rd,rn,f):_UNDEF;}
static inline uint32_t orr_immed (uint8_t rd,uint8_t rn,uint8_t w,uint8_t r){uint16_t f;return _a32_imm12(_a32_logmask(w,r),&f)?_DPIMM(0x03800000,rd,rn,f):_UNDEF;}
static inline uint32_t eor_immed (uint8_t rd,uint8_t rn,uint8_t w,uint8_t r){uint16_t f;return _a32_imm12(_a32_logmask(w,r),&f)?_DPIMM(0x02200000,rd,rn,f):_UNDEF;}
static inline uint32_t bic_immed (uint8_t rd,uint8_t rn,uint8_t w,uint8_t r){uint16_t f;return _a32_imm12(_a32_logmask(w,r),&f)?_DPIMM(0x03c00000,rd,rn,f):_UNDEF;}  // BIC #mask clears the field
static inline uint32_t tst_immed (uint8_t rn,uint8_t w,uint8_t r){uint16_t f;return _a32_imm12(_a32_logmask(w,r),&f)?_DPIMM(0x03100000,0,rn,f):_UNDEF;}

static inline uint32_t _orr_val (uint8_t rd,uint8_t rn,uint32_t v){uint16_t f;return _a32_imm12(v,&f)?_DPIMM(0x03800000,rd,rn,f):_UNDEF;}
static inline uint32_t _bic_val (uint8_t rd,uint8_t rn,uint32_t v){uint16_t f;return _a32_imm12(v,&f)?_DPIMM(0x03c00000,rd,rn,f):_UNDEF;}
static inline uint32_t _and_val (uint8_t rd,uint8_t rn,uint32_t v){uint16_t f;return _a32_imm12(v,&f)?_DPIMM(0x02000000,rd,rn,f):_UNDEF;}
static inline uint32_t _ands_val(uint8_t rd,uint8_t rn,uint32_t v){uint16_t f;return _a32_imm12(v,&f)?_DPIMM(0x02100000,rd,rn,f):_UNDEF;}
static inline uint32_t _eor_val (uint8_t rd,uint8_t rn,uint32_t v){uint16_t f;return _a32_imm12(v,&f)?_DPIMM(0x02200000,rd,rn,f):_UNDEF;}
static inline uint32_t _tst_val (uint8_t rn,           uint32_t v){uint16_t f;return _a32_imm12(v,&f)?_DPIMM(0x03100000, 0,rn,f):_UNDEF;}

// ---- byte/halfword extends : (rd, rn), rotate 0 ----
static inline uint32_t sxtb(uint8_t rd,uint8_t rn){return INSN_TO_LE((ARM_CC_AL<<28)|0x06af0070|((rd&15)<<12)|(rn&15));}
static inline uint32_t sxth(uint8_t rd,uint8_t rn){return INSN_TO_LE((ARM_CC_AL<<28)|0x06bf0070|((rd&15)<<12)|(rn&15));}
static inline uint32_t uxtb(uint8_t rd,uint8_t rn){return INSN_TO_LE((ARM_CC_AL<<28)|0x06ef0070|((rd&15)<<12)|(rn&15));}
static inline uint32_t uxth(uint8_t rd,uint8_t rn){return INSN_TO_LE((ARM_CC_AL<<28)|0x06ff0070|((rd&15)<<12)|(rn&15));}

#endif // _FWD_ALU_H
