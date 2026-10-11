#include "Emu68_ARM_emit.h"
/*
    Copyright © 2019 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include "support.h"
#include "M68k.h"
#include "RegisterAllocator.h"
#include "config.h"   /* CPUMODEL_* - the tri-state CPU model predicates */

#ifdef EMU68_CPU040
extern int CpuModel;
#endif
#include "EmuFeatures.h"
#include "OMMX.h"
#include "lists.h"
#include "tlsf.h"
#include "math/libm.h"

#if defined(EMU68_INNER_LOOP) && defined(EMU68_BCC_INLINE) && !defined(__aarch64__)
extern uint16_t *g_xlat_unit_start;
extern int       g_xlat_backedge_ok;
#define FDBCC_CLOSES_UNIT_LOOP(target)  (g_xlat_backedge_ok && (uintptr_t)(target) == (uintptr_t)g_xlat_unit_start)
#define EMU68_FDBCC_CLOSE 1
#else
#define EMU68_FDBCC_CLOSE 0
#endif
#define DBCC_LATE_ALLOC 0

enum {
    C_PI = 0,
    C_PI_2,
    C_PI_4,
    C_1_PI,
    C_2_PI,
    C_2_SQRTPI,
    C_1_2PI,
    C_SQRT2,
    C_SQRT1_2,
    C_0_5,
    C_1_5,
    C_LOG10_2 = 0x0b,
    C_E,
    C_LOG2E,
    C_LOG10E,
    C_ZERO,
    C_SIN_COEFF = 0x10,  /* 21-poly for sine approximation - error margin within double precision */
    C_COS_COEFF = 0x20,  /* 20-poly for cosine approximation -error margin within double precision */

    C_SIN_COEFF_SINGLE = 0x1a,
    C_COS_COEFF_SINGLE = 0x2a,

    C_LN2 = 0x30,
    C_LN10,
    C_10P0,
    C_10P1,
    C_10P2,
    C_10P4,
    C_10P8,
    C_10P16,
    C_10P32,
    C_10P64,
    C_10P128,
    C_10P256,
    C_10P512,
    C_10P1024,
    C_10P2048,
    C_10P4096,

    C_TWO54,
    C_LN2HI,
    C_LN2LO,
    C_LG1,
    C_LG2,
    C_LG3,
    C_LG4,
    C_LG5,
    C_LG6,
    C_LG7
};

static double const __attribute__((used)) constants[128] = {
    [C_PI] =        3.14159265358979323846264338327950288, /* Official */
    [C_PI_2] =      1.57079632679489661923132169163975144,
    [C_PI_4] =      0.785398163397448309615660845819875721,
    [C_1_PI] =      0.318309886183790671537767526745028724,
    [C_2_PI] =      0.636619772367581343075535053490057448,
    [C_2_SQRTPI] =  1.12837916709551257389615890312154517,
    [C_1_2PI] =     0.1591549430918953357688837633725143620,
    [C_SQRT2] =     1.41421356237309504880168872420969808,
    [C_SQRT1_2] =   0.707106781186547524400844362104849039,
    [C_0_5] =       0.5,
    [C_1_5] =       1.5,
    [C_LOG10_2] =   0.301029995663981195214, /* Official - Log10(2) */
    [C_E] =         2.71828182845904523536028747135266250,                /* Official */
    [C_LOG2E] =     1.44269504088896340735992468100189214,            /* Official */
    [C_LOG10E] =    0.434294481903251827651128918916605082,           /* Official */
    [C_ZERO] =      0.0,                /* Official */

    /* Polynom coefficients for sin(x*Pi), x=0..0.5*/

    [C_SIN_COEFF] = -2.11100178050346585936E-5,
                    4.65963708473294521719E-4,
                    -7.37035513524020578156E-3,
                    8.21458769726032277098E-2,
                    -5.99264528627362954518E-1,
                    2.55016403985097679243,
                    -5.16771278004952168888,
                    3.14159265358979102647,

    /* Reduced number of polynom coefficients for sin(x*Pi), x=0..0.5 */

    [C_SIN_COEFF_SINGLE] =
                    7.74455095806670556524E-2,
                    -5.98160819620617657839E-1,
                    2.55005088882843729408,
                    -5.1677080762924026306,
                    3.14159259939191476447,

    /* Polynom coefficients for cos(x*Pi), x=0..0.5 */

    [C_COS_COEFF] = 4.15383875943350535407E-6,
                    -1.04570624685965272291E-4,
                    1.92955784205552168426E-3,
                    -2.58068890507489103003E-2,
                    2.35330630164104256943E-1,
                    -1.33526276884550367708,
                    4.05871212641655666324,
                    -4.93480220054467742126,
                    9.99999999999999997244E-1,

    /* Reduced number of polynom coefficients for cos(x*Pi), x=0..0.5 */
    [C_COS_COEFF_SINGLE] =
                    2.20485796302921884119E-1,
                    -1.33223541188749370639,
                    4.058461009872062766402,
                    -4.93479497666537363458,
                    9.99999967245121125386E-1,

    [C_LN2] =       0.693147180559945309417232121458176568,              /* Official */
    [C_LN10] =      2.30258509299404568401799145468436421,             /* Official */
    [C_10P0] =      1.0,                /* Official */
    [C_10P1] =      1E1,                /* Official */
    [C_10P2] =      1E2,                /* Official */
    [C_10P4] =      1E4,                /* Official */
    [C_10P8] =      1E8,                /* Official */
    [C_10P16] =     1E16,               /* Official */
    [C_10P32] =     1E32,               /* Official */
    [C_10P64] =     1E64,               /* Official */
    [C_10P128] =    1E128,              /* Official */
    [C_10P256] =    1E256,              /* Official */
    [C_10P512] =    __builtin_inf(),    /* 1E512  - overflows double: +inf */
    [C_10P1024] =   __builtin_inf(),    /* 1E1024 - overflows double: +inf */
    [C_10P2048] =   __builtin_inf(),    /* 1E2048 - overflows double: +inf */
    [C_10P4096] =   __builtin_inf(),    /* 1E4096 - overflows double: +inf */

    [C_TWO54] =     1.80143985094819840000e+16,
    [C_LN2HI] =     6.93147180369123816490e-01,
    [C_LN2LO] =     1.90821492927058770002e-10,

    [C_LG1] =       6.666666666666735130e-01,
    [C_LG2] =       3.999999999940941908e-01,
    [C_LG3] =       2.857142874366239149e-01,
    [C_LG4] =       2.222219843214978396e-01,
    [C_LG5] =       1.818357216161805012e-01,
    [C_LG6] =       1.531383769920937332e-01,
    [C_LG7] =       1.479819860511658591e-01,
};

/*
    Returns reminder of absolute double number divided by 2, i.e. for any number it calculates result
    of number mod 2. Used by trigonometric functions
*/
double TrimDoubleRange(double a)
{
    union {
        uint64_t i;
        uint32_t i32[2];
        double d;
    } n, out;

    n.d = a;

    uint32_t exp = (n.i32[0] >> 20) & 0x7ff;
    uint64_t man = n.i & 0x000fffffffffffffULL;

    if (man && exp > 0x3ff && exp < (0x3ff + 52))
    {
        man = (man << (exp - 0x3ff)) & 0x001fffffffffffffULL;
        exp = 0x3ff;

        if (man) {
            int d = __builtin_clzll(man) - 11;

            if (d) {
                man = (man << (d)) & 0x000fffffffffffffULL;
                exp = exp - d;
            }
        }
        else
        {
            exp=0;
        }
    }
    else if (!man && exp > 0x3ff)
    {
        exp = 0;
    }

    out.i = man & ~0x0010000000000000ULL;
    out.i32[0] |= exp << 20;

    return out.d;
}

#ifdef __aarch64__

void PolySine(void);

void PolySineSingle(void);

void PolyCosine(void);

void PolyCosineSingle(void);


#else

#ifdef __ARM_FEATURE_FMA
#define VFMA_D(acc, a, b, tmp)  "   vfma.f64 " acc ", " a ", " b "     \n"
#define SIN_SAVE  "{d1,d2,d3}"
#define COS_SAVE  "{d1,d2}"
#else
#define VFMA_D(acc, a, b, tmp)  "   vmul.f64 " tmp ", " a ", " b "     \n" \
                                "   vadd.f64 " acc ", " acc ", " tmp " \n"
#define SIN_SAVE  "{d1,d2,d3,d4}"
#define COS_SAVE  "{d1,d2,d3}"
#endif

#define JITBAT_CLOBBER_FASTBANK

void PolySine(void);
void  __attribute__((used)) stub_PolySine(void)
{
    asm volatile(
        "   .align 4                \n"
        "PolySine:                  \n"
        "   vpush " SIN_SAVE "      \n"
        "   push {r0}               \n"
        JITBAT_CLOBBER_FASTBANK
        "   ldr r0,=constants       \n"
        "   vldr d1, [r0, %0]       \n"
        "   vmul.f64 d3, d0, d0     \n"
        "   vldr d2, [r0, %0+8]     \n"
        VFMA_D("d2","d1","d3","d4")
        "   vldr d1, [r0, %0+16]    \n"
        VFMA_D("d1","d2","d3","d4")
        "   vldr d2, [r0, %0+24]    \n"
        VFMA_D("d2","d1","d3","d4")
        "   vldr d1, [r0, %0+32]    \n"
        VFMA_D("d1","d2","d3","d4")
        "   vldr d2, [r0, %0+40]    \n"
        VFMA_D("d2","d1","d3","d4")
        "   vldr d1, [r0, %0+48]    \n"
        VFMA_D("d1","d2","d3","d4")
        "   vldr d2, [r0, %0+56]    \n"
        VFMA_D("d2","d1","d3","d4")
        "   vmul.f64 d0, d2, d0     \n"
        "   pop {r0}                \n"
        "   vpop " SIN_SAVE "       \n"
        "   bx lr                   \n"
        "   .ltorg                  \n"::"i"(C_SIN_COEFF*8)
    );
}

void PolySineSingle(void);
void  __attribute__((used)) stub_PolySineSingle(void)
{
    asm volatile(
        "   .align 4                \n"
        "PolySineSingle:            \n"
        "   vpush " SIN_SAVE "      \n"
        "   push {r0}               \n"
        "   ldr r0,=constants       \n"
        "   vldr d1, [r0, %0]       \n"
        "   vmul.f64 d3, d0, d0     \n"
        "   vldr d2, [r0, %0+8]     \n"
        VFMA_D("d2","d1","d3","d4")
        "   vldr d1, [r0, %0+16]    \n"
        VFMA_D("d1","d2","d3","d4")
        "   vldr d2, [r0, %0+24]    \n"
        VFMA_D("d2","d1","d3","d4")
        "   vldr d1, [r0, %0+32]    \n"
        VFMA_D("d1","d2","d3","d4")
        "   vmul.f64 d0, d1, d0     \n"
        "   pop {r0}                \n"
        "   vpop " SIN_SAVE "       \n"
        "   bx lr                   \n"
        "   .ltorg                  \n"::"i"(C_SIN_COEFF_SINGLE*8)
    );
}

void PolyCosine(void);
void  __attribute__((used)) stub_PolyCosine(void)
{
    asm volatile(
        "   .align 4                \n"
        "PolyCosine:                \n"
        "   vpush " COS_SAVE "      \n"
        "   push {r0}               \n"
        "   ldr r0,=constants       \n"
        "   vmul.f64 d2, d0, d0     \n"
        "   vldr d0, [r0, %0]       \n"
        "   vldr d1, [r0, %0+8]     \n"
        VFMA_D("d1","d0","d2","d3")
        "   vldr d0, [r0, %0+16]    \n"
        VFMA_D("d0","d1","d2","d3")
        "   vldr d1, [r0, %0+24]    \n"
        VFMA_D("d1","d0","d2","d3")
        "   vldr d0, [r0, %0+32]    \n"
        VFMA_D("d0","d1","d2","d3")
        "   vldr d1, [r0, %0+40]    \n"
        VFMA_D("d1","d0","d2","d3")
        "   vldr d0, [r0, %0+48]    \n"
        VFMA_D("d0","d1","d2","d3")
        "   vldr d1, [r0, %0+56]    \n"
        VFMA_D("d1","d0","d2","d3")
        "   vldr d0, [r0, %0+64]    \n"
        VFMA_D("d0","d1","d2","d3")
        "   pop {r0}                \n"
        "   vpop " COS_SAVE "       \n"
        "   bx lr                   \n"
        "   .ltorg                  \n"::"i"(C_COS_COEFF*8)
    );
}

void PolyCosineSingle(void);
void  __attribute__((used)) stub_PolyCosineSingle(void)
{
    asm volatile(
        "   .align 4                \n"
        "PolyCosineSingle:          \n"
        "   vpush " COS_SAVE "      \n"
        "   push {r0}               \n"
        "   ldr r0,=constants       \n"
        "   vmul.f64 d2, d0, d0     \n"
        "   vldr d0, [r0, %0]       \n"
        "   vldr d1, [r0, %0+8]     \n"
        VFMA_D("d1","d0","d2","d3")
        "   vldr d0, [r0, %0+16]    \n"
        VFMA_D("d0","d1","d2","d3")
        "   vldr d1, [r0, %0+24]    \n"
        VFMA_D("d1","d0","d2","d3")
        "   vldr d0, [r0, %0+32]    \n"
        VFMA_D("d0","d1","d2","d3")
        "   pop {r0}                \n"
        "   vpop " COS_SAVE "       \n"
        "   bx lr                   \n"
        "   .ltorg                  \n"::"i"(C_COS_COEFF_SINGLE*8)
    );
}

#endif

enum FPUOpSize {
    SIZE_L = 0,
    SIZE_S = 1,
    SIZE_X = 2,
    SIZE_P = 3,
    SIZE_W = 4,
    SIZE_D = 5,
    SIZE_B = 6,
};

double scalbn(double x, int n);          /* src/math/scalbn.c  - FSCALE */
double fmod(double x, double y);         /* src/math/fmod.c    - FMOD   */
double remquo(double x, double y, int *quo);  /* src/math/remquo.c - FREM */

uint8_t FPUDataSize[] = {
    [SIZE_L] = 4,
    [SIZE_S] = 4,
    [SIZE_X] = 12,
    [SIZE_P] = 12,
    [SIZE_W] = 2,
    [SIZE_D] = 8,
    [SIZE_B] = 1
};

#ifndef __aarch64__
static uint32_t *EMIT_ExtendedFPBase(uint32_t *ptr, uint8_t *base, int32_t *off, uint8_t *scratch)
{
    int far = (*off < -4095 || *off > 4095 - 8);
    int mapped = RA_IsARMRegisterMapped(*base);

    *scratch = 0xff;
    if (!far && !mapped)
        return ptr;
    if (mapped) {
        RA_FlushFPCR(&ptr);
        RA_FlushFPSR(&ptr);
    }
    *scratch = RA_AllocARMRegister(&ptr);
    if (far) {
        *ptr++ = movw_immed_u16(REG_EMIT_TMP, (uint32_t)(*off) & 0xffff);
        *ptr++ = movt_immed_u16(REG_EMIT_TMP, ((uint32_t)(*off) >> 16) & 0xffff);
        *ptr++ = add_reg(*scratch, *base, REG_EMIT_TMP, LSL, 0);
        *off = 0;
    } else {
        *ptr++ = mov_reg(*scratch, *base);
    }
    *base = *scratch;
    return ptr;
}

static __attribute__((section(".fpfix_tail_text"), noinline, noclone))
void EMIT_FMOVEM_Dynamic_sync(uint32_t **ptr)
{
    RA_StoreDirtyM68kRegs(ptr);
    RA_FlushFPCR(ptr);
    RA_FlushFPSR(ptr);
    for (int i = 0; i < 8; i++)
        RA_SetDirtyFPURegister(ptr, i);
}


static uint32_t *EMIT_LoadExtendedFP(uint32_t *ptr, uint8_t fpreg, uint8_t base, int32_t off)
{
    uint8_t scratch;
    ptr = EMIT_ExtendedFPBase(ptr, &base, &off, &scratch);

    uint8_t hi = RA_AllocARMRegister(&ptr);
    uint8_t lo = RA_AllocARMRegister(&ptr);
    uint8_t ex = RA_AllocARMRegister(&ptr);
    uint8_t sg = RA_AllocARMRegister(&ptr);
    uint8_t e2 = RA_AllocARMRegister(&ptr);
    uint8_t sh = RA_AllocARMRegister(&ptr);
#define ld_t1 ex

    *ptr++ = ldr_offset(base, hi, off + 4);     /* mantissa[63:32]; bit31 = the EXPLICIT int bit */
    *ptr++ = ldr_offset(base, lo, off + 8);     /* mantissa[31:0]                                */
    *ptr++ = ldr_offset(base, ex, off);         /* [sign|exp15] in the high half, pad in the low */

    *ptr++ = lsr_immed(ex, ex, 16);
    *ptr++ = lsr_immed(sg, ex, 15);
    *ptr++ = _bic_val(ex, ex, 0x8000);

    *ptr++ = lsr_immed(lo, lo, 11);
    *ptr++ = bfi(lo, hi, 21, 11);
    *ptr++ = _bic_val(hi, hi, 0x80000000);
    *ptr++ = lsr_immed(hi, hi, 11);

    *ptr++ = sub_immed(e2, ex, 0xc3c);          /* NORMAL: exp11 = exp15 - 0x3C00                */

    *ptr++ = add_immed(ex, ex, 1);
    *ptr++ = _tst_val(ex, 0x8000);
    *ptr++ = orr_cc_immed(ARM_CC_NE, sg, sg, 2);        /* sg bit1 = "this is inf/NaN"   */
    *ptr++ = mov_cc_immed_u8(ARM_CC_NE, e2, 1);         /* park in range; mantissa kept  */

    *ptr++ = cmp_immed(ex, 1);
    *ptr++ = mov_cc_immed_u8(ARM_CC_EQ, e2, 0);
    *ptr++ = mov_cc_immed_u8(ARM_CC_EQ, hi, 0);
    *ptr++ = mov_cc_immed_u8(ARM_CC_EQ, lo, 0);

    *ptr++ = orr_cc_immed(ARM_CC_NE, hi, hi, 0x601);    /* !zero: implicit 1 -> hi bit 20 (imm 1<<20) */

    *ptr++ = cmp_immed(e2, 0);
    *ptr++ = mov_cc_immed_u8(ARM_CC_GT, sh, 0);         /* normal/inf/NaN/overflow: no shift      */
    *ptr++ = rsb_cc_immed(ARM_CC_LE, sh, e2, 1);        /* subnormal/underflow: sh = 1 - e2 (>=1) */
    *ptr++ = mov_cc_immed_u8(ARM_CC_LE, e2, 0);         /* ...and the exponent FIELD is 0         */
    *ptr++ = cmp_immed(sh, 52);
    *ptr++ = mov_cc_immed_u8(ARM_CC_GT, sh, 63);        /* > 52 = a TRUE underflow -> shift it away */

    *ptr++ = rsb_immed(ld_t1, sh, 32);
    *ptr++ = lsr_reg(lo, lo, sh);
    *ptr++ = orr_reg_lsl_reg(lo, lo, hi, ld_t1);        /* sh < 32 branch of the pair             */
    *ptr++ = rsb_immed(ld_t1, ld_t1, 0);
    *ptr++ = orr_reg_lsr_reg(lo, lo, hi, ld_t1);        /* sh >= 32 branch of the pair            */
    *ptr++ = lsr_reg(hi, hi, sh);

    *ptr++ = movw_immed_u16(ex, 0x7fe);         /* ex re-materialised here; see ld_t1 above       */
    *ptr++ = cmp_reg(e2, ex, LSL, 0);
    *ptr++ = mov_cc_immed_u8(ARM_CC_GT, hi, 0);         /* overflow  -> +/-inf */
    *ptr++ = mov_cc_immed_u8(ARM_CC_GT, lo, 0);
    *ptr++ = movw_cc_immed_u16(ARM_CC_GT, e2, 0x7ff);

    *ptr++ = _tst_val(sg, 2);
    *ptr++ = movw_cc_immed_u16(ARM_CC_NE, e2, 0x7ff);

    *ptr++ = bfi(hi, e2, 20, 11);               /* exponent                                      */
    *ptr++ = bfi(hi, sg, 31, 1);                /* sign - width 1, so sg's marker bit is ignored */
    *ptr++ = fmdrr(fpreg, hi, lo);

    RA_FreeARMRegister(&ptr, sh);
    RA_FreeARMRegister(&ptr, e2);
    RA_FreeARMRegister(&ptr, sg);
    RA_FreeARMRegister(&ptr, ex);
    RA_FreeARMRegister(&ptr, lo);
    RA_FreeARMRegister(&ptr, hi);
    if (scratch != 0xff) RA_FreeARMRegister(&ptr, scratch);
#undef ld_t1

    return ptr;
}

static uint32_t *EMIT_StoreExtendedFP(uint32_t *ptr, uint8_t fpreg, uint8_t base, int32_t off)
{
    uint8_t scratch;
    ptr = EMIT_ExtendedFPBase(ptr, &base, &off, &scratch);

    uint8_t hi = RA_AllocARMRegister(&ptr);
    uint8_t lo = RA_AllocARMRegister(&ptr);
    uint8_t ex = RA_AllocARMRegister(&ptr);
    uint8_t mh = RA_AllocARMRegister(&ptr);
    uint8_t ew = RA_AllocARMRegister(&ptr);
    uint8_t nz = RA_AllocARMRegister(&ptr);
#define st_sk ex

    *ptr++ = fmrrd(hi, lo, fpreg);

    uint8_t fz = RA_AllocARMRegister(&ptr);
    *ptr++ = lsl_immed(fz, hi, 12);             /* drop sign+exponent, keep fraction[51:32]      */
    *ptr++ = orr_reg(fz, fz, lo, LSL, 0);       /* fz == 0  iff the whole 52-bit fraction is 0   */

    *ptr++ = ubfx(ex, hi, 20, 11);              /* exp11                                         */

    *ptr++ = lsr_immed(mh, lo, 21);
    *ptr++ = bfi(mh, hi, 11, 20);
    *ptr++ = lsl_immed(lo, lo, 11);             /* mantissa[31:0]                                */
    *ptr++ = lsr_immed(hi, hi, 31);

    *ptr++ = clz(nz, mh);
    *ptr++ = cmp_immed(mh, 0);
    *ptr++ = clz_cc(ARM_CC_EQ, nz, lo);
    *ptr++ = add_cc_immed(ARM_CC_EQ, nz, nz, 32);

    *ptr++ = add_immed(ew, ex, 0xc3c);          /* NORMAL: exp15 = exp11 + 0x3C00                */

    *ptr++ = add_immed(ex, ex, 1);
    *ptr++ = _tst_val(ex, 0x800);               /* exp11 was all ones -> inf/NaN                 */
    *ptr++ = movw_cc_immed_u16(ARM_CC_NE, ew, 0x7fff);

    *ptr++ = cmp_immed(ex, 1);                  /* EQ <=> exp11 was 0                            */
    *ptr++ = orr_cc_immed(ARM_CC_NE, mh, mh, 0x102);    /* normal/inf/NaN: the EXPLICIT int bit  */
    *ptr++ = mov_cc_immed_u8(ARM_CC_NE, nz, 0);         /* ...and no normalising shift           */
    *ptr++ = movw_cc_immed_u16(ARM_CC_EQ, ew, 0x3c01);  /* SUBNORMAL: exp15 = 0x3C01 - c0        */
    *ptr++ = sub_cc_reg(ARM_CC_EQ, ew, ew, nz, 0);

    *ptr++ = cmp_cc_immed(ARM_CC_EQ, fz, 0);
    *ptr++ = mov_cc_immed_u8(ARM_CC_EQ, ew, 0);


    *ptr++ = movw_immed_u16(ex, 0x7fff);
    *ptr++ = eors_reg(ex, ew, ex, 0);              /* 0 iff ew == $7FFF (inf or NaN)                */
    *ptr++ = orr_reg(ex, ex, fz, LSL, 0);       /* 0 iff ALSO the fraction is zero => INFINITY   */
    *ptr++ = cmp_immed(ex, 0);
    *ptr++ = mov_cc_immed_u8(ARM_CC_EQ, mh, 0); /* infinity -> mantissa 0, explicit bit cleared  */

    *ptr++ = rsb_immed(st_sk, nz, 32);
    *ptr++ = lsl_reg(mh, mh, nz);
    *ptr++ = orr_reg_lsr_reg(mh, mh, lo, st_sk);
    *ptr++ = rsb_immed(st_sk, st_sk, 0);
    *ptr++ = orr_reg_lsl_reg(mh, mh, lo, st_sk);
    *ptr++ = lsl_reg(lo, lo, nz);

    *ptr++ = lsl_immed(ew, ew, 16);             /* [sign|exp15][0000] - the 96-bit form's pad    */
    *ptr++ = bfi(ew, hi, 31, 1);

    *ptr++ = str_offset(base, ew, off);
    *ptr++ = str_offset(base, mh, off + 4);
    *ptr++ = str_offset(base, lo, off + 8);

    RA_FreeARMRegister(&ptr, nz);
    RA_FreeARMRegister(&ptr, fz);
    RA_FreeARMRegister(&ptr, ew);
    RA_FreeARMRegister(&ptr, mh);
    RA_FreeARMRegister(&ptr, ex);
    RA_FreeARMRegister(&ptr, lo);
    RA_FreeARMRegister(&ptr, hi);
    if (scratch != 0xff) RA_FreeARMRegister(&ptr, scratch);
#undef st_sk

    return ptr;
}
#endif /* !__aarch64__ */

static uint32_t *EMIT_RoundToPrec(uint32_t *ptr, uint8_t fp_dst)
{
#ifndef __aarch64__
    (void)fp_dst;
#else
    (void)fp_dst;
#endif
    return ptr;
}

static uint32_t *EMIT_RoundToIntegral(uint32_t *ptr, uint8_t fp_dst, uint8_t fp_src, int truncate)
{
    uint8_t dA  = RA_AllocFPURegister(&ptr);
    uint8_t dM  = RA_AllocFPURegister(&ptr);
    uint8_t sgn = RA_AllocARMRegister(&ptr);
    uint8_t rlo = RA_AllocARMRegister(&ptr);
    uint8_t rhi = RA_AllocARMRegister(&ptr);

    *ptr++ = fmrrd(sgn, rlo, fp_src);
    *ptr++ = fabsd(dA, fp_src);

    *ptr++ = mov_immed_u8(rlo, 0);
    *ptr++ = movw_immed_u16(rhi, 0x0000);
    *ptr++ = movt_immed_u16(rhi, 0x4330);
    *ptr++ = fmdrr(dM, rhi, rlo);

    *ptr++ = faddd(fp_dst, dA, dM);
    *ptr++ = fsubd(fp_dst, fp_dst, dM);

    if (truncate)
    {
        *ptr++ = fcmpd(fp_dst, dA);         /* did it round UP past |x|?                         */
        *ptr++ = fmstat();
        *ptr++ = movw_immed_u16(rhi, 0x0000);
        *ptr++ = movt_immed_u16(rhi, 0x3ff0);
        *ptr++ = fmdrr(dM, rhi, rlo);
        *ptr++ = fsubd_cc(ARM_CC_GT, fp_dst, fp_dst, dM);
    }

    *ptr++ = _tst_val(sgn, 0x80000000);
    *ptr++ = fnegd_cc(ARM_CC_MI, fp_dst, fp_dst);

    RA_FreeARMRegister(&ptr, rhi);
    RA_FreeARMRegister(&ptr, rlo);
    RA_FreeARMRegister(&ptr, sgn);
    RA_FreeFPURegister(&ptr, dM);
    RA_FreeFPURegister(&ptr, dA);

    return ptr;
}

static uint32_t *EMIT_GetFPUFlagsFromD(uint32_t *ptr, uint8_t fpsr, uint8_t dreg)
{
#ifdef __aarch64__
    *ptr++ = fcmpzd(dreg);
    ptr = EMIT_GetFPUFlags(ptr, fpsr);
#else
    uint8_t hi = RA_AllocARMRegister(&ptr);

    *ptr++ = fcmpzd(dreg);
    *ptr++ = fmstat();
    *ptr++ = _bic_val(fpsr, fpsr, 0x0F000000);              /* clear the condition-code nibble */
    *ptr++ = orr_cc_immed(ARM_CC_EQ, fpsr, fpsr, 0x404);
    *ptr++ = orr_cc_immed(ARM_CC_VS, fpsr, fpsr, 0x401);

    *ptr++ = INSN_TO_LE(0xee300b10 | (hi << 12) | (dreg << 16));
    *ptr++ = _tst_val(hi, 0x80000000);
    *ptr++ = orr_cc_immed(ARM_CC_MI, fpsr, fpsr, 0x408);    /* N */


    RA_FreeARMRegister(&ptr, hi);
#endif
    return ptr;
}

#ifndef __aarch64__
static uint32_t *EMIT_FMOVEM_Dynamic(uint32_t *ptr, uint16_t opcode2, uint8_t base_reg,
                                     int to_memory, int predecrement, int writeback,
                                     uint8_t an_reg)
{
    uint8_t list = RA_CopyFromM68kRegister(&ptr, (opcode2 >> 4) & 7);
    uint8_t fp[8];
    uint8_t walk = RA_AllocARMRegister(&ptr);
    *ptr++ = mov_reg(walk, base_reg);

    for (int i = 0; i < 8; i++)
        fp[i] = RA_MapFPURegister(&ptr, i);

    EMIT_FMOVEM_Dynamic_sync(&ptr);

    for (int n = 0; n < 8; n++) {
        int i = predecrement ? (7 - n) : n;
        uint32_t bit = predecrement ? (1u << i) : (0x80u >> i);
        uint32_t *skip;

        *ptr++ = _tst_val(list, bit);
        skip = ptr;
        *ptr++ = b_cc(ARM_CC_EQ, 0);        /* patched by ASSIGNMENT below - never |= */

        if (predecrement)
            *ptr++ = sub_immed(walk, walk, 12);

        if (to_memory) ptr = EMIT_StoreExtendedFP(ptr, fp[i], walk, 0);
        else           ptr = EMIT_LoadExtendedFP(ptr, fp[i], walk, 0);

        if (!predecrement)
            *ptr++ = add_immed(walk, walk, 12);

        *skip = b_cc(ARM_CC_EQ, ptr - skip);
    }

    for (int i = 0; i < 8; i++)
        RA_FreeFPURegister(&ptr, fp[i]);
    if (writeback) {
        uint8_t an = RA_MapM68kRegisterForWrite(&ptr, an_reg);
        *ptr++ = mov_reg(an, walk);
        RA_SetDirtyM68kRegister(&ptr, an_reg);
    }
    RA_FreeARMRegister(&ptr, walk);
    RA_FreeARMRegister(&ptr, list);

    return ptr;
}
#endif /* !__aarch64__ */

static uint32_t *EMIT_FPUPredicate(uint32_t *ptr, uint8_t fpsr, uint8_t predicate,
                                   uint8_t *success_condition)
{
    uint8_t tmp_cc = 0xff;

    /* Test predicate with masked signalling bit, operations are the same */
    switch (predicate & 0x0f)
    {
                case F_CC_EQ:
    #ifdef __aarch64__
                    *ptr++ = tst_immed(fpsr, 1, 31 & (32 - FPSRB_Z));
                    *success_condition = A64_CC_NE;
    #else
                    *ptr++ = _tst_val(fpsr, FPSR_Z);
                    *success_condition = ARM_CC_NE;
    #endif
                    break;
                case F_CC_NE:
    #ifdef __aarch64__
                    *ptr++ = tst_immed(fpsr, 1, 31 & (32 - FPSRB_Z));
                    *success_condition = A64_CC_EQ;
    #else
                    *ptr++ = _tst_val(fpsr, FPSR_Z);
                    *success_condition = ARM_CC_EQ;
    #endif
                    break;
                case F_CC_OGT:
    #ifdef __aarch64__
                    tmp_cc = RA_AllocARMRegister(&ptr);
                    *ptr++ = mov_immed_u16(tmp_cc, (FPSR_Z | FPSR_N | FPSR_NAN) >> 16, 1);
                    *ptr++ = tst_reg(fpsr, tmp_cc, LSL, 0);
                    *success_condition = ARM_CC_EQ;
    #else
                    *ptr++ = _tst_val(fpsr, FPSR_Z|FPSR_N|FPSR_NAN);
                    *success_condition = ARM_CC_EQ;
    #endif
                    break;
                case F_CC_ULE:
    #ifdef __aarch64__
                    tmp_cc = RA_AllocARMRegister(&ptr);
                    *ptr++ = mov_immed_u16(tmp_cc, (FPSR_Z | FPSR_N | FPSR_NAN) >> 16, 1);
                    *ptr++ = tst_reg(fpsr, tmp_cc, LSL, 0);
                    *success_condition = ARM_CC_NE;
    #else
                    *ptr++ = _tst_val(fpsr, FPSR_Z|FPSR_N|FPSR_NAN);
                    *success_condition = ARM_CC_NE;
    #endif
                    break;
                case F_CC_OGE: // Z == 1 || (N == 0 && NAN == 0)
    #ifdef __aarch64__
                    tmp_cc = RA_AllocARMRegister(&ptr);
                    *ptr++ = tst_immed(fpsr, 1, 31 & (32 - FPSRB_Z));
                    *ptr++ = b_cc(A64_CC_NE, 4);
                    *ptr++ = orr_reg(tmp_cc, fpsr, fpsr, LSL, 3); // N | NAN -> N (== 0 only if N=0 && NAN=0)
                    *ptr++ = mvn_reg(tmp_cc, tmp_cc, LSL, 0); //eor_immed(tmp_cc, tmp_cc, 1, 31 & (32 - FPSRB_N)); // !N -> N
                    *ptr++ = tst_immed(tmp_cc, 1, 31 & (32 - FPSRB_N));
                    *success_condition = A64_CC_NE;
    #else
                    tmp_cc = RA_AllocARMRegister(&ptr);
                    *ptr++ = _eor_val(tmp_cc, fpsr, FPSR_Z);    /* Copy fpsr to temporary reg, invert Z */
                    *ptr++ = _tst_val(tmp_cc, FPSR_Z); // Test Z
                    *ptr++ = tst_cc_immed(ARM_CC_NE, fpsr, 0x409); // Z == 1? Test N == 0 && NAN == 0
                    *success_condition = ARM_CC_EQ;
    #endif
                    break;
                case F_CC_ULT: // NAN == 1 || (N == 1 && Z == 0)
    #ifdef __aarch64__
                    tmp_cc = RA_AllocARMRegister(&ptr);
                    *ptr++ = tst_immed(fpsr, 1, 31 & (32 - FPSRB_NAN));
                    *ptr++ = b_cc(A64_CC_NE, 4);
                    *ptr++ = eor_immed(tmp_cc, fpsr, 1, 31 & (32 - FPSRB_Z)); // Invert Z
                    *ptr++ = and_reg(tmp_cc, fpsr, tmp_cc, LSL, 1); // !Z & N -> N
                    *ptr++ = tst_immed(tmp_cc, 1, 31 & (32 - FPSRB_N));
                    *success_condition = A64_CC_NE;
    #else
                    tmp_cc = RA_AllocARMRegister(&ptr);
                    *ptr++ = _eor_val(tmp_cc, fpsr, FPSR_N|FPSR_NAN);    /* Copy fpsr to temporary reg, invert N and NAN */
                    *ptr++ = _tst_val(tmp_cc, FPSR_NAN);  // Test NAN
                    *ptr++ = tst_cc_immed(ARM_CC_NE, tmp_cc, 0x40c);
                    *success_condition = ARM_CC_EQ;
    #endif
                    break;
                case F_CC_OLT: // N == 1 && (NAN == 0 && Z == 0)
    #ifdef __aarch64__
                    tmp_cc = RA_AllocARMRegister(&ptr);
                    *ptr++ = orr_reg(tmp_cc, fpsr, fpsr, LSL, 2); // NAN | Z -> Z
                    *ptr++ = eor_immed(tmp_cc, tmp_cc, 1, 31 & (32 - FPSRB_N)); // Invert N
                    *ptr++ = tst_immed(tmp_cc, 2, 31 & (32 - FPSRB_Z)); // Test N==0 && Z == 0
                    *success_condition = A64_CC_EQ;
    #else
                    tmp_cc = RA_AllocARMRegister(&ptr);
                    *ptr++ = _eor_val(tmp_cc, fpsr, FPSR_N);    /* Copy fpsr to temporary reg, invert N */
                    *ptr++ = _tst_val(tmp_cc, FPSR_Z|FPSR_N|FPSR_NAN);
                    *success_condition = ARM_CC_EQ;
    #endif
                    break;
                case F_CC_UGE: // NAN == 1 || (Z == 1 || N == 0)
    #ifdef __aarch64__
                    tmp_cc = RA_AllocARMRegister(&ptr);
                    *ptr++ = eor_immed(tmp_cc, fpsr, 1, 31 & (32 - FPSRB_N));
                    *ptr++ = bic_immed(tmp_cc, tmp_cc, 1, 31 & (32 - FPSRB_I));
                    *ptr++ = tst_immed(tmp_cc, 4, 31 & (32 - FPSRB_NAN));
                    *success_condition = A64_CC_NE;
    #else
                    tmp_cc = RA_AllocARMRegister(&ptr);
                    *ptr++ = _eor_val(tmp_cc, fpsr, FPSR_N);    /* Copy fpsr to temporary reg, invert N */
                    *ptr++ = _tst_val(tmp_cc, FPSR_Z|FPSR_N|FPSR_NAN);
                    *success_condition = ARM_CC_NE;
    #endif
                    break;
                case F_CC_OLE: // Z == 1 || (N == 1 && NAN == 0)
    #ifdef __aarch64__
                    tmp_cc = RA_AllocARMRegister(&ptr);
                    *ptr++ = tst_immed(fpsr, 1, 31 & (32 - FPSRB_Z));
                    *ptr++ = b_cc(A64_CC_NE, 4);
                    *ptr++ = eor_immed(tmp_cc, fpsr, 1, 31 & (32 - FPSRB_NAN)); // Invert NAN
                    *ptr++ = and_reg(tmp_cc, tmp_cc, tmp_cc, LSL, 3);   // !NAN & N -> N
                    *ptr++ = tst_immed(tmp_cc, 1, 31 & (32 - FPSRB_N));
                    *success_condition = A64_CC_NE;
    #else
                    tmp_cc = RA_AllocARMRegister(&ptr);
                    *ptr++ = _eor_val(tmp_cc, fpsr, FPSR_N|FPSR_Z);    /* Copy fpsr to temporary reg, invert Z and N */
                    *ptr++ = _tst_val(tmp_cc, FPSR_Z);          /* Test if inverted Z == 0 */
                    *ptr++ = tst_cc_immed(ARM_CC_NE, tmp_cc, 0x409);    /* Test if !N == 0 && NAN == 0 */
                    *success_condition = ARM_CC_EQ;
    #endif
                    break;
                case F_CC_UGT: // NAN == 1 || (N == 0 && Z == 0)
    #ifdef __aarch64__
                    tmp_cc = RA_AllocARMRegister(&ptr);
                    *ptr++ = tst_immed(fpsr, 1, 31 & (32 - FPSRB_NAN));
                    *ptr++ = b_cc(A64_CC_NE, 4);
                    *ptr++ = orr_reg(tmp_cc, fpsr, fpsr, LSR, 1);
                    *ptr++ = mvn_reg(tmp_cc, tmp_cc, LSL, 0); //eor_immed(tmp_cc, tmp_cc, 1, 31 & (32 - FPSRB_Z));
                    *ptr++ = tst_immed(tmp_cc, 1, 31 & (32 - FPSRB_Z));
                    *success_condition = A64_CC_NE;
    #else
                    tmp_cc = RA_AllocARMRegister(&ptr);
                    *ptr++ = _eor_val(tmp_cc, fpsr, FPSR_NAN);    /* Copy fpsr to temporary reg, invert NAN */
                    *ptr++ = _tst_val(tmp_cc, FPSR_NAN);          /* Test if inverted NAN == 0 */
                    *ptr++ = tst_cc_immed(ARM_CC_NE, tmp_cc, 0x40c);    /* Test if !N == 0 && Z == 0 */
                    *success_condition = ARM_CC_EQ;
    #endif
                    break;
                case F_CC_OGL:
    #ifdef __aarch64__
                    tmp_cc = RA_AllocARMRegister(&ptr);
                    *ptr++ = mov_immed_u16(tmp_cc, (FPSR_Z | FPSR_NAN) >> 16, 1);
                    *ptr++ = tst_reg(fpsr, tmp_cc, LSL, 0);
                    *success_condition = A64_CC_EQ;
    #else
                    *ptr++ = _tst_val(fpsr, FPSR_Z|FPSR_NAN);
                    *success_condition = ARM_CC_EQ;
    #endif
                    break;
                case F_CC_UEQ:
    #ifdef __aarch64__
                    tmp_cc = RA_AllocARMRegister(&ptr);
                    *ptr++ = mov_immed_u16(tmp_cc, (FPSR_Z | FPSR_NAN) >> 16, 1);
                    *ptr++ = tst_reg(fpsr, tmp_cc, LSL, 0);
                    *success_condition = A64_CC_NE;
    #else
                    *ptr++ = _tst_val(fpsr, FPSR_Z|FPSR_NAN);
                    *success_condition = ARM_CC_NE;
    #endif
                    break;
                case F_CC_OR:
    #ifdef __aarch64__
                    *ptr++ = tst_immed(fpsr, 1, 31 & (32 - FPSRB_NAN));
                    *success_condition = A64_CC_EQ;
    #else
                    *ptr++ = _tst_val(fpsr, FPSR_NAN);
                    *success_condition = ARM_CC_EQ;
    #endif
                    break;
                case F_CC_UN:
    #ifdef __aarch64__
                    *ptr++ = tst_immed(fpsr, 1, 31 & (32 - FPSRB_NAN));
                    *success_condition = A64_CC_NE;
    #else
                    *ptr++ = _tst_val(fpsr, FPSR_NAN);
                    *success_condition = ARM_CC_NE;
    #endif
                    break;

                case F_CC_F:
    #ifdef __aarch64__
                    *ptr++ = tst_immed(31, 1, 0);       /* TST WZR,#1 -> Z=1 */
                    *success_condition = A64_CC_NE;      /* NE can never be true: never */
    #else
                    *ptr++ = _tst_val(fpsr, 0);         /* TST Rn,#0  -> Z=1 */
                    *success_condition = ARM_CC_NE;
    #endif
                    break;
                case F_CC_T:
    #ifdef __aarch64__
                    *ptr++ = tst_immed(31, 1, 0);
                    *success_condition = A64_CC_EQ;      /* EQ is always true: always */
    #else
                    *ptr++ = _tst_val(fpsr, 0);
                    *success_condition = ARM_CC_EQ;
    #endif
                    break;

                default:
                    kprintf("[JIT] FPU predicate %02x unhandled\n", predicate);
                    *ptr++ = udf(0xfbcc);
                    break;
    }
    RA_FreeARMRegister(&ptr, tmp_cc);

    return ptr;
}


int FPSR_Update_Needed(uint16_t **m68k_ptr)
{
    uint16_t *ptr = *m68k_ptr;
    int cnt = 0;


    for (;;)
    {
        while((BE16(*ptr) & 0xfe00) != 0xf200)
        {
            if (cnt++ > 15)
                return 1;
            if (M68K_IsBranch(ptr))
                return 1;
            else {
                int len = M68K_GetINSNLength(ptr);
                if (len == 0)
                    return 1;
                ptr += len;
            }
        }

        uint16_t opcode = BE16(ptr[0]);
        uint16_t opcode2 = BE16(ptr[1]);

        /*
            Update FPSR condition codes only if subsequent FPU instruction
            is one of following: FBcc, FDBcc, FMOVEM, FScc, FTRAPcc
        */
        if ((opcode & 0xff80) == 0xf280)    /* FBcc */
            return 1;
        if ((opcode & 0xfff8) == 0xf248 && (opcode2 & 0xffc0) == 0) /* FDBcc */
            return 1;
        if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xc700) == 0xc000) /* FMOVEM */
            return 1;
        if ((opcode & 0xffc0) == 0xf240 && (opcode2 & 0xffc0) == 0) /* FScc */
            return 1;
        if ((opcode & 0xfff8) == 0xf278 && (opcode2 & 0xffc0) == 0) /* FTRAPcc */
            return 1;
        if ((opcode & 0xffc0) == 0xf200 &&
            (opcode2 & 0xe000) == 0xa000 && (opcode2 & 0x0800))
            return 1;

        if ((opcode & 0xffc0) == 0xf200)
        {
            int passive = 0;
            if ((opcode2 & 0xe07f) == 0x6000)                           /* FMOVE FPn,<ea>, k = 0 */
                passive = 1;
            else if ((opcode2 & 0xc3ff) == 0x8000 && !(opcode2 & 0x0800))
                passive = 1;
            if (passive)
            {
                if (cnt++ > 15)
                    return 1;
                int len = M68K_GetINSNLength(ptr);
                if (len == 0)
                    return 1;
                ptr += len;
                continue;
            }
            /* FMOVE.P FPn,<ea>{k}: passive too, but the scanner cannot size it - be safe */
            if ((opcode2 & 0xe000) == 0x6000)
                return 1;
        }
        /* FSAVE / FRESTORE ($F300-$F37F): not arithmetic, and FRESTORE can reset the FPSR - be safe */
        if ((opcode & 0xff80) == 0xf300)
            return 1;

        /* What is left writes the condition codes before anything can read them. */
        return 0;
    }
}

/* Allocates FPU register and fetches data according to the R/M field of the FPU opcode */
uint32_t *FPU_FetchData(uint32_t *ptr, uint16_t **m68k_ptr, uint8_t *reg, uint16_t opcode,
        uint16_t opcode2, uint8_t *ext_count)
{
    /* IF R/M is zero, then source identifier is FPU reg number. */
    if ((opcode2 & 0x4000) == 0)
    {
        *reg = RA_MapFPURegister(&ptr, (opcode2 >> 10) & 7);
    }
    else
    {
        /*
            R/M was set to 1, the source is defined by EA stored in first part of the
            opcode. Source identifier specifies the data length.

            Get EA, eventually (in case of mode 000 - Dn) perform simple data transfer.
            Otherwise get address from EA and fetch data here
        */

        /* The regtister was not yet assigned? assign it to a temporary reg now */
        if (*reg == 0xff)
            *reg = RA_AllocFPURegister(&ptr);

        uint8_t ea = opcode & 0x3f;
        enum FPUOpSize size = (opcode2 >> 10) & 7;
        int32_t imm_offset = 0;

        /* Case 1: mode 000 - Dn */
        if ((ea & 0x38) == 0)
        {
            uint8_t int_reg = 0xff;
            uint8_t tmp_reg;

            switch (size)
            {
                /* Single - move to single half of the reg, convert to double */
                case SIZE_S:
                    ptr = EMIT_LoadFromEffectiveAddress(ptr, 4, &int_reg, ea, *m68k_ptr, ext_count, 1, NULL);
#ifdef __aarch64__
                    *ptr++ = fmsr(*reg, int_reg);
                    *ptr++ = fcvtds(*reg, *reg);
#else
                    *ptr++ = fmsr(*reg * 2, int_reg);
                    *ptr++ = fcvtds(*reg, *reg * 2);
#endif
                    RA_FreeARMRegister(&ptr, int_reg);
                    break;

                case SIZE_L:
                    ptr = EMIT_LoadFromEffectiveAddress(ptr, 4, &int_reg, ea, *m68k_ptr, ext_count, 1, NULL);
#ifdef __aarch64__
                    EMIT_scvtf_32toD(&ptr, *reg, int_reg);
#else
                    *ptr++ = fmsr(*reg * 2, int_reg);
                    *ptr++ = fsitod(*reg, *reg * 2);
#endif
                    RA_FreeARMRegister(&ptr, int_reg);
                    break;

                case SIZE_W:
                    ptr = EMIT_LoadFromEffectiveAddress(ptr, 2, &int_reg, ea, *m68k_ptr, ext_count, 1, NULL);
                    tmp_reg = RA_AllocARMRegister(&ptr);
#ifdef __aarch64__
                    *ptr++ = sxth(tmp_reg, int_reg);
                    EMIT_scvtf_32toD(&ptr, *reg, tmp_reg);
#else
                    *ptr++ = sxth(tmp_reg, int_reg);
                    *ptr++ = fmsr(*reg * 2, tmp_reg);
                    *ptr++ = fsitod(*reg, *reg * 2);
#endif
                    RA_FreeARMRegister(&ptr, tmp_reg);
                    RA_FreeARMRegister(&ptr, int_reg);
                    break;

                case SIZE_B:
                    ptr = EMIT_LoadFromEffectiveAddress(ptr, 1, &int_reg, ea, *m68k_ptr, ext_count, 1, NULL);
                    tmp_reg = RA_AllocARMRegister(&ptr);
#ifdef __aarch64__
                    *ptr++ = sxtb(tmp_reg, int_reg);
                    EMIT_scvtf_32toD(&ptr, *reg, tmp_reg);
#else
                    *ptr++ = sxtb(tmp_reg, int_reg);
                    *ptr++ = fmsr(*reg * 2, tmp_reg);
                    *ptr++ = fsitod(*reg, *reg * 2);
#endif
                    RA_FreeARMRegister(&ptr, tmp_reg);
                    RA_FreeARMRegister(&ptr, int_reg);
                    break;

                default:
                    kprintf("[JIT] LineF: wrong argument size %d for Dn access at %08x\n", (int)size, *m68k_ptr-1);
            }
        }
        /* Case 2: mode 111:100 - immediate */
        else if (ea == 0x3c)
        {
            /* Fetch data *or* pointer to data into int_reg */
            uint8_t int_reg = 0xff;
            int not_yet_done = 0;

            switch (size)
            {
                case SIZE_S:
#ifdef __aarch64__
                {
                    int8_t off = 4;
                    ptr = EMIT_GetOffsetPC(ptr, &off);
                    *ptr++ = flds(*reg, REG_PC, off);
                    *ptr++ = fcvtds(*reg, *reg);
                    *ext_count += 2;
                    break;
                }
#else
                    ptr = EMIT_LoadFromEffectiveAddress(ptr, 4, &int_reg, ea, *m68k_ptr, ext_count, 0, NULL);
                    *ptr++ = fmsr(*reg * 2, int_reg);
                    *ptr++ = fcvtds(*reg, *reg * 2);
#endif
                    break;
                case SIZE_L:
                    ptr = EMIT_LoadFromEffectiveAddress(ptr, 4, &int_reg, ea, *m68k_ptr, ext_count, 0, NULL);
#ifdef __aarch64__
                    EMIT_scvtf_32toD(&ptr, *reg, int_reg);
#else
                    *ptr++ = fmsr(*reg * 2, int_reg);
                    *ptr++ = fsitod(*reg, *reg * 2);
#endif
                    break;
                case SIZE_W:
#ifdef __aarch64__
                {
                    int_reg = RA_AllocARMRegister(&ptr);
                    int16_t imm = (int16_t)BE16((*m68k_ptr)[1]);
                    *ptr++ = movw_immed_u16(int_reg, imm & 0xffff);
                    if (imm < 0)
                        *ptr++ = movt_immed_u16(int_reg, 0xffff);
                    EMIT_scvtf_32toD(&ptr, *reg, int_reg);
                    *ext_count += 1;
                }
#else
                    ptr = EMIT_LoadFromEffectiveAddress(ptr, 2, &int_reg, ea, *m68k_ptr, ext_count, 0, NULL);
                    *ptr++ = sxth(int_reg, int_reg);
                    *ptr++ = fmsr(*reg * 2, int_reg);
                    *ptr++ = fsitod(*reg, *reg * 2);
#endif
                    break;
                case SIZE_B:
#ifdef __aarch64__
                {
                    int_reg = RA_AllocARMRegister(&ptr);
                    int8_t imm = (int8_t)BE16((*m68k_ptr)[1]);
                    *ptr++ = mov_immed_s8(int_reg, imm);
                    EMIT_scvtf_32toD(&ptr, *reg, int_reg);
                    *ext_count += 1;
                }
#else
                    ptr = EMIT_LoadFromEffectiveAddress(ptr, 1, &int_reg, ea, *m68k_ptr, ext_count, 0, NULL);
                    *ptr++ = sxtb(int_reg, int_reg);
                    *ptr++ = fmsr(*reg * 2, int_reg);
                    *ptr++ = fsitod(*reg, *reg * 2);
#endif
                    break;
#ifdef __aarch64__
                case SIZE_D:
                {
                    int8_t off = 4;
                    ptr = EMIT_GetOffsetPC(ptr, &off);
                    *ptr++ = fldd(*reg, REG_PC, off);
                    *ext_count += 4;
                    break;
                }
#endif
                default:
                    ptr = EMIT_LoadFromEffectiveAddress(ptr, 0, &int_reg, ea, *m68k_ptr, ext_count, 0, NULL);
                    not_yet_done = 1;
                    break;
            }

            /* if data not yet in the reg, use the address to load it into FPU register */
            if (not_yet_done)
            {
                switch(size)
                {
                    case SIZE_D:
#ifdef __aarch64__
                        *ptr++ = fldd(*reg, int_reg, 0);
#else
                        {
                            uint8_t tmp1 = RA_AllocARMRegister(&ptr);
                            uint8_t tmp2 = RA_AllocARMRegister(&ptr);

                            *ptr++ = ldr_offset(int_reg, tmp1, 0);
                            *ptr++ = ldr_offset(int_reg, tmp2, 4);
                            *ptr++ = fmdrr(*reg, tmp1, tmp2);

                            RA_FreeARMRegister(&ptr, tmp2);
                            RA_FreeARMRegister(&ptr, tmp1);
                        }
#endif
                        *ext_count += 4;
                        break;

                    case SIZE_X:
                        {
#ifdef __aarch64__
                            ptr = EMIT_Load96bitFP(ptr, *reg, int_reg, 0);
#else
                            ptr = EMIT_LoadExtendedFP(ptr, *reg, int_reg, 0);
#endif
                        }
                        *ext_count += 6;
                        break;

                    case SIZE_P:
                        kprintf("Packed load!\n");
                        *ext_count += 6;
                        break;

                    default:
                        break;
                }
            }

            RA_FreeARMRegister(&ptr, int_reg);
        }
        /* Case 3: get pointer to data (EA) and fetch yourself */
        else
        {
            uint8_t int_reg = 0xff;
            uint8_t val_reg = 0xff;
            uint8_t mode = (opcode & 0x0038) >> 3;
            int8_t pre_sz = 0;
            int8_t post_sz = 0;

            if (mode == 4 || mode == 3)
                ptr = EMIT_LoadFromEffectiveAddress(ptr, 0, &int_reg, opcode & 0x3f, *m68k_ptr, ext_count, 0, NULL);
            else
                ptr = EMIT_LoadFromEffectiveAddress(ptr, 0, &int_reg, opcode & 0x3f, *m68k_ptr, ext_count, 1, &imm_offset);

            /* Pre index? Adjust base register accordingly */
            if (mode == 4) {
                pre_sz = FPUDataSize[size];

                if ((pre_sz == 1) && ((opcode & 7) == 7))
                    pre_sz = 2;
                
                pre_sz = -pre_sz;
            }
            /* Post index? Adjust base register accordingly */
            else if (mode == 3) {
                post_sz = FPUDataSize[size];

                if ((post_sz == 1) && ((opcode & 7) == 7))
                    post_sz = 2;
            }

#ifndef __aarch64__
            if (pre_sz)
                *ptr++ = sub_immed(int_reg, int_reg, -pre_sz);
#endif

            switch (size)
            {
                case SIZE_X:
                    {
#ifdef __aarch64__
                        if (pre_sz)
                        {
                            *ptr++ = sub_immed(int_reg, int_reg, -pre_sz);
                        }
                        if (imm_offset < -255 || imm_offset > 251) {
                            uint8_t off = RA_AllocARMRegister(&ptr);

                            if (imm_offset > -4096 && imm_offset < 0)
                            {
                                *ptr++ = sub_immed(off, int_reg, -imm_offset);
                            }
                            else if (imm_offset >= 0 && imm_offset < 4096)
                            {
                                *ptr++ = add_immed(off, int_reg, imm_offset);
                            }
                            else
                            {
                                *ptr++ = movw_immed_u16(off, (imm_offset) & 0xffff);
                                imm_offset >>= 16;
                                if (imm_offset)
                                    *ptr++ = movt_immed_u16(off, (imm_offset) & 0xffff);
                                *ptr++ = add_reg(off, int_reg, off, LSL, 0);
                            }
                            RA_FreeARMRegister(&ptr, int_reg);
                            int_reg = off;
                            imm_offset = 0;
                        }

                        ptr = EMIT_Load96bitFP(ptr, *reg, int_reg, imm_offset);

                        if (post_sz)
                        {
                            *ptr++ = add_immed(int_reg, int_reg, post_sz);
                        }
#else
                        /* (no offset guard here: EMIT_LoadExtendedFP normalises its own base) */

                        ptr = EMIT_LoadExtendedFP(ptr, *reg, int_reg, imm_offset);
#endif
                    }
                    break;
                case SIZE_D:
                    {
#ifdef __aarch64__
                        if (pre_sz)
                        {
                            *ptr++ = fldd_preindex(*reg, int_reg, pre_sz);
                        }
                        else if (post_sz)
                        {
                            *ptr++ = fldd_postindex(*reg, int_reg, post_sz);
                        }
                        else if (imm_offset >= -255 && imm_offset <= 255)
                        {
                            *ptr++ = fldd(*reg, int_reg, imm_offset);
                        }
                        else if (imm_offset >= 0 && imm_offset < 32760 && !(imm_offset & 7))
                        {
                            *ptr++ = fldd_pimm(*reg, int_reg, imm_offset >> 3);
                        }
                        else
                        {
                            uint8_t off = RA_AllocARMRegister(&ptr);
                            if (imm_offset > -4096 && imm_offset < 0)
                            {
                                *ptr++ = sub_immed(off, int_reg, -imm_offset);
                            }
                            else if (imm_offset >= 0 && imm_offset < 4096)
                            {
                                *ptr++ = add_immed(off, int_reg, imm_offset);
                            }
                            else
                            {
                                *ptr++ = movw_immed_u16(off, (imm_offset) & 0xffff);
                                imm_offset >>= 16;
                                if (imm_offset)
                                    *ptr++ = movt_immed_u16(off, (imm_offset) & 0xffff);
                                *ptr++ = add_reg(off, int_reg, off, LSL, 0);
                            }
                            *ptr++ = fldd(*reg, off, 0);
                            RA_FreeARMRegister(&ptr, off);
                        }
#else
                        uint8_t tmp1 = RA_AllocARMRegister(&ptr);
                        uint8_t tmp2 = RA_AllocARMRegister(&ptr);

                        {
                            if (imm_offset < -4095 || imm_offset > 4095-4) {
                                uint8_t off = RA_AllocARMRegister(&ptr);

                                *ptr++ = movw_immed_u16(off, (imm_offset) & 0xffff);
                                imm_offset >>= 16;
                                if (imm_offset)
                                    *ptr++ = movt_immed_u16(off, (imm_offset) & 0xffff);
                                *ptr++ = add_reg(off, int_reg, off, LSL, 0);

                                RA_FreeARMRegister(&ptr, int_reg);
                                int_reg = off;
                                imm_offset = 0;
                            }

                            *ptr++ = ldr_offset(int_reg, tmp1, imm_offset);
                            *ptr++ = ldr_offset(int_reg, tmp2, imm_offset + 4);
                            *ptr++ = fmdrr(*reg, tmp1, tmp2);
                        }

                        RA_FreeARMRegister(&ptr, tmp2);
                        RA_FreeARMRegister(&ptr, tmp1);
#endif
                    }
                    break;
                case SIZE_S:
#ifdef __aarch64__
                        if (pre_sz)
                        {
                            *ptr++ = flds_preindex(*reg, int_reg, pre_sz);
                        }
                        else if (post_sz)
                        {
                            *ptr++ = flds_postindex(*reg, int_reg, post_sz);
                        }
                        else if (imm_offset >= -255 && imm_offset <= 255)
                        {
                            *ptr++ = flds(*reg, int_reg, imm_offset);
                        }
                        else if (imm_offset >= 0 && imm_offset < 16380 && !(imm_offset & 3))
                        {
                            *ptr++ = flds_pimm(*reg, int_reg, imm_offset >> 2);
                        }
                        else
                        {
                            uint8_t off = RA_AllocARMRegister(&ptr);
                            if (imm_offset > -4096 && imm_offset < 0)
                            {
                                *ptr++ = sub_immed(off, int_reg, -imm_offset);
                            }
                            else if (imm_offset >= 0 && imm_offset < 4096)
                            {
                                *ptr++ = add_immed(off, int_reg, imm_offset);
                            }
                            else
                            {
                                *ptr++ = movw_immed_u16(off, (imm_offset) & 0xffff);
                                imm_offset >>= 16;
                                if (imm_offset)
                                    *ptr++ = movt_immed_u16(off, (imm_offset) & 0xffff);
                                *ptr++ = add_reg(off, int_reg, off, LSL, 0);
                            }
                            *ptr++ = flds(*reg, off, 0);
                            RA_FreeARMRegister(&ptr, off);
                        }
                        *ptr++ = fcvtds(*reg, *reg);
#else
                    if (imm_offset < -4095 || imm_offset > 4095) {
                        uint8_t off = RA_AllocARMRegister(&ptr);

                        *ptr++ = movw_immed_u16(off, (imm_offset) & 0xffff);
                        imm_offset >>= 16;
                        if (imm_offset)
                            *ptr++ = movt_immed_u16(off, (imm_offset) & 0xffff);
                        *ptr++ = add_reg(off, int_reg, off, LSL, 0);

                        RA_FreeARMRegister(&ptr, int_reg);
                        int_reg = off;
                        imm_offset = 0;
                    }
                    val_reg = RA_AllocARMRegister(&ptr);
                    *ptr++ = ldr_offset(int_reg, val_reg, imm_offset);
                    *ptr++ = fmsr(*reg * 2, val_reg);
                    *ptr++ = fcvtds(*reg, *reg * 2);
#endif
                    break;
                case SIZE_L:
#ifdef __aarch64__
                    val_reg = RA_AllocARMRegister(&ptr);

                    if (pre_sz)
                    {
                        *ptr++ = ldr_offset_preindex(int_reg, val_reg, pre_sz);
                    }
                    else if (post_sz)
                    {
                        *ptr++ = ldr_offset_postindex(int_reg, val_reg, pre_sz);
                    }
                    else if (imm_offset >= -255 && imm_offset <= 255)
                    {
                        *ptr++ = ldur_offset(int_reg, val_reg, imm_offset);
                    }
                    else if (imm_offset >= 0 && imm_offset < 16380 && !(imm_offset & 3))
                    {
                        *ptr++ = ldr_offset(int_reg, val_reg, imm_offset);
                    }
                    else
                    {
                        uint8_t off = RA_AllocARMRegister(&ptr);
                        if (imm_offset > -4096 && imm_offset < 0)
                        {
                            *ptr++ = sub_immed(off, int_reg, -imm_offset);
                        }
                        else if (imm_offset >= 0 && imm_offset < 4096)
                        {
                            *ptr++ = add_immed(off, int_reg, imm_offset);
                        }
                        else
                        {
                            *ptr++ = movw_immed_u16(off, (imm_offset) & 0xffff);
                            imm_offset >>= 16;
                            if (imm_offset)
                                *ptr++ = movt_immed_u16(off, (imm_offset) & 0xffff);
                            *ptr++ = add_reg(off, int_reg, off, LSL, 0);
                        }
                        *ptr++ = ldr_offset(off, val_reg, 0);
                        RA_FreeARMRegister(&ptr, off);
                    }
                    EMIT_scvtf_32toD(&ptr, *reg, val_reg);
#else
                    if (imm_offset < -4095 || imm_offset > 4095) {
                        uint8_t off = RA_AllocARMRegister(&ptr);

                        *ptr++ = movw_immed_u16(off, (imm_offset) & 0xffff);
                        imm_offset >>= 16;
                        if (imm_offset)
                            *ptr++ = movt_immed_u16(off, (imm_offset) & 0xffff);
                        *ptr++ = add_reg(off, int_reg, off, LSL, 0);

                        RA_FreeARMRegister(&ptr, int_reg);
                        int_reg = off;
                        imm_offset = 0;
                    }

                    val_reg = RA_AllocARMRegister(&ptr);
                    *ptr++ = ldr_offset(int_reg, val_reg, imm_offset);
                    *ptr++ = fmsr(*reg * 2, val_reg);
                    *ptr++ = fsitod(*reg, *reg * 2);
#endif
                    break;
                case SIZE_W:
#ifdef __aarch64__
                    val_reg = RA_AllocARMRegister(&ptr);

                    if (pre_sz)
                    {
                        *ptr++ = ldrh_offset_preindex(int_reg, val_reg, pre_sz);
                    }
                    else if (post_sz)
                    {
                        *ptr++ = ldrh_offset_postindex(int_reg, val_reg, pre_sz);
                    }
                    else if (imm_offset >= -255 && imm_offset <= 255)
                    {
                        *ptr++ = ldurh_offset(int_reg, val_reg, imm_offset);
                    }
                    else if (imm_offset >= 0 && imm_offset < 8190 && !(imm_offset & 1))
                    {
                        *ptr++ = ldrh_offset(int_reg, val_reg, imm_offset);
                    }
                    else
                    {
                        uint8_t off = RA_AllocARMRegister(&ptr);
                        if (imm_offset > -4096 && imm_offset < 0)
                        {
                            *ptr++ = sub_immed(off, int_reg, -imm_offset);
                        }
                        else if (imm_offset >= 0 && imm_offset < 4096)
                        {
                            *ptr++ = add_immed(off, int_reg, imm_offset);
                        }
                        else
                        {
                            *ptr++ = movw_immed_u16(off, (imm_offset) & 0xffff);
                            imm_offset >>= 16;
                            if (imm_offset)
                                *ptr++ = movt_immed_u16(off, (imm_offset) & 0xffff);
                            *ptr++ = add_reg(off, int_reg, off, LSL, 0);
                        }
                        *ptr++ = ldrh_offset(off, val_reg, 0);
                        RA_FreeARMRegister(&ptr, off);
                    }
                    *ptr++ = sxth(val_reg, val_reg);
                    EMIT_scvtf_32toD(&ptr, *reg, val_reg);
#else
                    if (imm_offset < -255 || imm_offset > 255) {
                        uint8_t off = RA_AllocARMRegister(&ptr);

                        *ptr++ = movw_immed_u16(off, (imm_offset) & 0xffff);
                        imm_offset >>= 16;
                        if (imm_offset)
                            *ptr++ = movt_immed_u16(off, (imm_offset) & 0xffff);
                        *ptr++ = add_reg(off, int_reg, off, LSL, 0);

                        RA_FreeARMRegister(&ptr, int_reg);
                        int_reg = off;
                        imm_offset = 0;
                    }
                    val_reg = RA_AllocARMRegister(&ptr);
                    *ptr++ = ldrsh_offset(int_reg, val_reg, imm_offset);
                    *ptr++ = fmsr(*reg * 2, val_reg);
                    *ptr++ = fsitod(*reg, *reg * 2);
#endif
                    break;
                case SIZE_B:
#ifdef __aarch64__
                    val_reg = RA_AllocARMRegister(&ptr);

                    if (pre_sz)
                    {
                        *ptr++ = ldrb_offset_preindex(int_reg, val_reg, pre_sz);
                    }
                    else if (post_sz)
                    {
                        *ptr++ = ldrb_offset_postindex(int_reg, val_reg, pre_sz);
                    }
                    else if (imm_offset >= -255 && imm_offset <= 255)
                    {
                        *ptr++ = ldurb_offset(int_reg, val_reg, imm_offset);
                    }
                    else if (imm_offset >= 0 && imm_offset < 4096)
                    {
                        *ptr++ = ldrb_offset(int_reg, val_reg, imm_offset);
                    }
                    else
                    {
                        uint8_t off = RA_AllocARMRegister(&ptr);
                        if (imm_offset > -4096 && imm_offset < 0)
                        {
                            *ptr++ = sub_immed(off, int_reg, -imm_offset);
                        }
                        else if (imm_offset >= 0 && imm_offset < 4096)
                        {
                            *ptr++ = add_immed(off, int_reg, imm_offset);
                        }
                        else
                        {
                            *ptr++ = movw_immed_u16(off, (imm_offset) & 0xffff);
                            imm_offset >>= 16;
                            if (imm_offset)
                                *ptr++ = movt_immed_u16(off, (imm_offset) & 0xffff);
                            *ptr++ = add_reg(off, int_reg, off, LSL, 0);
                        }
                        *ptr++ = ldrb_offset(off, val_reg, 0);
                        RA_FreeARMRegister(&ptr, off);
                    }
                    *ptr++ = sxtb(val_reg, val_reg);
                    EMIT_scvtf_32toD(&ptr, *reg, val_reg);
#else

                    if (imm_offset < -4095 || imm_offset > 4095) {
                        uint8_t off = RA_AllocARMRegister(&ptr);

                        *ptr++ = movw_immed_u16(off, (imm_offset) & 0xffff);
                        imm_offset >>= 16;
                        if (imm_offset)
                            *ptr++ = movt_immed_u16(off, (imm_offset) & 0xffff);
                        *ptr++ = add_reg(off, int_reg, off, LSL, 0);

                        RA_FreeARMRegister(&ptr, int_reg);
                        int_reg = off;
                        imm_offset = 0;
                    }
                    val_reg = RA_AllocARMRegister(&ptr);
                    *ptr++ = ldrsb_offset(int_reg, val_reg, imm_offset);
                    *ptr++ = fmsr(*reg * 2, val_reg);
                    *ptr++ = fsitod(*reg, *reg * 2);
#endif
                    break;
                default:
                    break;
            }

#ifndef __aarch64__
            if (post_sz)
                *ptr++ = add_immed(int_reg, int_reg, post_sz);
#endif

            if ((mode == 4) || (mode == 3))
            {
                RA_SetDirtyM68kRegister(&ptr, 8 + (opcode & 7));
            }

            RA_FreeARMRegister(&ptr, int_reg);
            RA_FreeARMRegister(&ptr, val_reg);
        }
    }

    return ptr;
}

/* Allocates FPU register and fetches data according to the R/M field of the FPU opcode */
uint32_t *FPU_StoreData(uint32_t *ptr, uint16_t **m68k_ptr, uint8_t reg, uint16_t opcode,
        uint16_t opcode2, uint8_t *ext_count)
{
    /*
        Store is always to memory, R/M was set to 1, the source is FPU register, target is
        defined by EA stored in first part of the opcode. Destination identifier specifies
        the data length.

        Get EA, eventually (in case of mode 000 - Dn) perform simple data transfer.
        Otherwise get address from EA and store data here
    */

    uint8_t ea = opcode & 0x3f;
    enum FPUOpSize size = (opcode2 >> 10) & 7;
    int32_t imm_offset = 0;

    /* Case 1: mode 000 - Dn */
    if ((ea & 0x38) == 0)
    {
        uint8_t int_reg = 0xff;
        uint8_t tmp_reg = 0xff;
        uint8_t vfp_reg = RA_AllocFPURegister(&ptr);

        switch (size)
        {
            case SIZE_S:
                int_reg = RA_MapM68kRegisterForWrite(&ptr, ea & 7); // Destination for write only, discard contents
#ifdef __aarch64__
                *ptr++ = fcvtsd(vfp_reg, reg);                  // Convert double to single
                *ptr++ = fmrs(int_reg, vfp_reg);                // Move single to destination ARM reg
#else
                *ptr++ = fcvtsd(vfp_reg * 2, reg);                  // Convert double to single
                *ptr++ = fmrs(int_reg, vfp_reg * 2);                // Move single to destination ARM reg
#endif
                RA_FreeARMRegister(&ptr, int_reg);
                break;

            case SIZE_L:
                int_reg = RA_MapM68kRegisterForWrite(&ptr, ea & 7); // Destination for write only, discard contents
#ifdef __aarch64__
                // No rounding mode specified? Round to zero?
                EMIT_fcvtzs_Dto32(&ptr, int_reg, reg);
#else
                *ptr++ = ftosid(vfp_reg * 2, reg);                  // Convert double to signed integer
                *ptr++ = fmrs(int_reg, vfp_reg * 2);                // Move signed int to destination ARM reg
#endif
                RA_FreeARMRegister(&ptr, int_reg);
                break;

            case SIZE_W:
                int_reg = RA_MapM68kRegister(&ptr, ea & 7);
                tmp_reg = RA_AllocARMRegister(&ptr);
#ifdef __aarch64__
                EMIT_fcvtzs_Dto32(&ptr, tmp_reg, reg);
                *ptr++ = bfi(int_reg, tmp_reg, 0, 16);
#else
                *ptr++ = ftosid(vfp_reg * 2, reg);                  // Convert double to signed integer
                *ptr++ = fmrs(tmp_reg, vfp_reg * 2);                // Move signed int to temporary ARM reg
                *ptr++ = bfi(int_reg, tmp_reg, 0, 16);              // Insert lower 16 bits of temporary into target
#endif
                RA_SetDirtyM68kRegister(&ptr, ea & 7);
                RA_FreeARMRegister(&ptr, tmp_reg);
                RA_FreeARMRegister(&ptr, int_reg);
                break;

            case SIZE_B:
                int_reg = RA_MapM68kRegister(&ptr, ea & 7);
                tmp_reg = RA_AllocARMRegister(&ptr);
#ifdef __aarch64__
                EMIT_fcvtzs_Dto32(&ptr, tmp_reg, reg);
                *ptr++ = bfi(int_reg, tmp_reg, 0, 8);
#else
                *ptr++ = ftosid(vfp_reg * 2, reg);                  // Convert double to signed integer
                *ptr++ = fmrs(tmp_reg, vfp_reg * 2);                // Move signed int to temporary ARM reg
                *ptr++ = bfi(int_reg, tmp_reg, 0, 8);               // Insert lower 16 bits of temporary into target
#endif
                RA_SetDirtyM68kRegister(&ptr, ea & 7);
                RA_FreeARMRegister(&ptr, tmp_reg);
                RA_FreeARMRegister(&ptr, int_reg);
                break;

            default:
                kprintf("[JIT] LineF: wrong argument size %d for Dn access\n", (int)size);
        }

        RA_FreeFPURegister(&ptr, vfp_reg);
    }
    /* Case 2: get pointer to data (EA) and store yourself */
    else
    {
        uint8_t int_reg = 0xff;
        uint8_t val_reg = 0xff;
        uint8_t mode = (opcode & 0x0038) >> 3;
        uint8_t vfp_reg = RA_AllocFPURegister(&ptr);
        int8_t pre_sz = 0;
        int8_t post_sz = 0;

        if (mode == 4 || mode == 3)
            ptr = EMIT_LoadFromEffectiveAddress(ptr, 0, &int_reg, opcode & 0x3f, *m68k_ptr, ext_count, 0, NULL);
        else
            ptr = EMIT_LoadFromEffectiveAddress(ptr, 0, &int_reg, opcode & 0x3f, *m68k_ptr, ext_count, 1, &imm_offset);

        /* Pre index? Adjust base register accordingly */
        if (mode == 4) {
            pre_sz = FPUDataSize[size];

            if ((pre_sz == 1) && ((opcode & 7) == 7))
                pre_sz = 2;
            
            pre_sz = -pre_sz;
        }
        /* Post index? Adjust base register accordingly */
        else if (mode == 3) {
            post_sz = FPUDataSize[size];

            if ((post_sz == 1) && ((opcode & 7) == 7))
                post_sz = 2;
        }

#ifndef __aarch64__
        if (pre_sz)
            *ptr++ = sub_immed(int_reg, int_reg, -pre_sz);
#endif

        switch (size)
        {
            case SIZE_X:
                {
#ifdef __aarch64__
                    if (pre_sz)
                    {
                        *ptr++ = sub_immed(int_reg, int_reg, -pre_sz);
                    }
                    if (imm_offset >= -255 && imm_offset <= 251)
                    {
                        ptr = EMIT_Store96bitFP(ptr, reg, int_reg, imm_offset);
                    }
                    else
                    {
                        uint8_t off = RA_AllocARMRegister(&ptr);
                        if (imm_offset > -4096 && imm_offset < 0)
                        {
                            *ptr++ = sub_immed(off, int_reg, -imm_offset);
                        }
                        else if (imm_offset >= 0 && imm_offset < 4096)
                        {
                            *ptr++ = add_immed(off, int_reg, imm_offset);
                        }
                        else
                        {
                            *ptr++ = movw_immed_u16(off, (imm_offset) & 0xffff);
                            imm_offset >>= 16;
                            if (imm_offset)
                                *ptr++ = movt_immed_u16(off, (imm_offset) & 0xffff);
                            *ptr++ = add_reg(off, int_reg, off, LSL, 0);
                        }

                        ptr = EMIT_Store96bitFP(ptr, reg, off, 0);
                        RA_FreeARMRegister(&ptr, off);
                    }
                    if (post_sz)
                    {
                        *ptr++ = add_immed(int_reg, int_reg, post_sz);
                    }
#else
                    ptr = EMIT_StoreExtendedFP(ptr, reg, int_reg, imm_offset);
#endif
                }
                break;
            case SIZE_D:
                {
#ifdef __aarch64__
                    if (pre_sz)
                    {
                        *ptr++ = fstd_preindex(reg, int_reg, pre_sz);
                    }
                    else if (post_sz)
                    {
                        *ptr++ = fstd_postindex(reg, int_reg, post_sz);
                    }
                    else if (imm_offset >= -255 && imm_offset <= 255)
                    {
                        *ptr++ = fstd(reg, int_reg, imm_offset);
                    }
                    else if (imm_offset >= 0 && imm_offset < 32760 && !(imm_offset & 7))
                    {
                        *ptr++ = fstd_pimm(reg, int_reg, imm_offset >> 3);
                    }
                    else
                    {
                        uint8_t off = RA_AllocARMRegister(&ptr);
                        if (imm_offset > -4096 && imm_offset < 0)
                        {
                            *ptr++ = sub_immed(off, int_reg, -imm_offset);
                        }
                        else if (imm_offset >= 0 && imm_offset < 4096)
                        {
                            *ptr++ = add_immed(off, int_reg, imm_offset);
                        }
                        else
                        {
                            *ptr++ = movw_immed_u16(off, (imm_offset) & 0xffff);
                            imm_offset >>= 16;
                            if (imm_offset)
                                *ptr++ = movt_immed_u16(off, (imm_offset) & 0xffff);
                            *ptr++ = add_reg(off, int_reg, off, LSL, 0);
                        }
                        *ptr++ = fstd(reg, off, 0);
                        RA_FreeARMRegister(&ptr, off);
                    }
#else
                    uint8_t tmp1 = RA_AllocARMRegister(&ptr);
                    uint8_t tmp2 = RA_AllocARMRegister(&ptr);

                    {
                        *ptr++ = fmrrd(tmp1, tmp2, reg);
                        if (imm_offset > -4096 && imm_offset < 4096-4) {
                            *ptr++ = str_offset(int_reg, tmp1, imm_offset);
                            *ptr++ = str_offset(int_reg, tmp2, imm_offset + 4);
                        } else {
                            uint8_t off = RA_AllocARMRegister(&ptr);

                            *ptr++ = movw_immed_u16(off, (imm_offset) & 0xffff);
                            imm_offset >>= 16;
                            if (imm_offset)
                                *ptr++ = movt_immed_u16(off, (imm_offset) & 0xffff);
                            *ptr++ = add_reg(off, int_reg, off, LSL, 0);

                            *ptr++ = str_offset(off, tmp1, 0);
                            *ptr++ = str_offset(off, tmp2, 4);

                            RA_FreeARMRegister(&ptr, off);
                        }
                    }

                    RA_FreeARMRegister(&ptr, tmp2);
                    RA_FreeARMRegister(&ptr, tmp1);
#endif
                }
                break;
            case SIZE_S:
#ifdef __aarch64__
                *ptr++ = fcvtsd(vfp_reg, reg);
                if (pre_sz)
                {
                    *ptr++ = fsts_preindex(vfp_reg, int_reg, pre_sz);
                }
                else if (post_sz)
                {
                    *ptr++ = fsts_postindex(vfp_reg, int_reg, post_sz);
                }
                else if (imm_offset >= -255 && imm_offset <= 255)
                {
                    *ptr++ = fsts(vfp_reg, int_reg, imm_offset);
                }
                else if (imm_offset >= 0 && imm_offset < 16380 && !(imm_offset & 3))
                {
                    *ptr++ = fsts_pimm(vfp_reg, int_reg, imm_offset >> 2);
                }
                else
                {
                    uint8_t off = RA_AllocARMRegister(&ptr);
                    if (imm_offset > -4096 && imm_offset < 0)
                    {
                        *ptr++ = sub_immed(off, int_reg, -imm_offset);
                    }
                    else if (imm_offset >= 0 && imm_offset < 4096)
                    {
                        *ptr++ = add_immed(off, int_reg, imm_offset);
                    }
                    else
                    {
                        *ptr++ = movw_immed_u16(off, (imm_offset) & 0xffff);
                        imm_offset >>= 16;
                        if (imm_offset)
                            *ptr++ = movt_immed_u16(off, (imm_offset) & 0xffff);
                        *ptr++ = add_reg(off, int_reg, off, LSL, 0);
                    }
                    *ptr++ = fsts(vfp_reg, off, 0);
                    RA_FreeARMRegister(&ptr, off);
                }
#else
                *ptr++ = fcvtsd(vfp_reg * 2, reg);
                val_reg = RA_AllocARMRegister(&ptr);
                *ptr++ = fmrs(val_reg, vfp_reg * 2);
                if (imm_offset < -4095 || imm_offset > 4095) {
                    uint8_t off = RA_AllocARMRegister(&ptr);

                    *ptr++ = movw_immed_u16(off, (imm_offset) & 0xffff);
                    imm_offset >>= 16;
                    if (imm_offset)
                        *ptr++ = movt_immed_u16(off, (imm_offset) & 0xffff);
                    *ptr++ = add_reg(off, int_reg, off, LSL, 0);

                    RA_FreeARMRegister(&ptr, int_reg);
                    int_reg = off;
                    imm_offset = 0;
                }
                *ptr++ = str_offset(int_reg, val_reg, imm_offset);
#endif
                break;
            case SIZE_L:
#ifdef __aarch64__
                val_reg = RA_AllocARMRegister(&ptr);
                EMIT_fcvtzs_Dto32(&ptr, val_reg, reg);

                if (pre_sz)
                {
                    *ptr++ = str_offset_preindex(int_reg, val_reg, pre_sz);
                }
                else if (post_sz)
                {
                    *ptr++ = str_offset_postindex(int_reg, val_reg, post_sz);
                }
                else if (imm_offset >= -255 && imm_offset <= 255)
                {
                    *ptr++ = stur_offset(int_reg, val_reg, imm_offset);
                }
                else if (imm_offset >= 0 && imm_offset < 16380 && !(imm_offset & 3))
                {
                    *ptr++ = str_offset(int_reg, val_reg, imm_offset);
                }
                else
                {
                    uint8_t off = RA_AllocARMRegister(&ptr);
                    if (imm_offset > -4096 && imm_offset < 0)
                    {
                        *ptr++ = sub_immed(off, int_reg, -imm_offset);
                    }
                    else if (imm_offset >= 0 && imm_offset < 4096)
                    {
                        *ptr++ = add_immed(off, int_reg, imm_offset);
                    }
                    else
                    {
                        *ptr++ = movw_immed_u16(off, (imm_offset) & 0xffff);
                        imm_offset >>= 16;
                        if (imm_offset)
                            *ptr++ = movt_immed_u16(off, (imm_offset) & 0xffff);
                        *ptr++ = add_reg(off, int_reg, off, LSL, 0);
                    }
                    *ptr++ = str_offset(off, val_reg, 0);
                    RA_FreeARMRegister(&ptr, off);
                }
#else
                val_reg = RA_AllocARMRegister(&ptr);
                *ptr++ = ftosid(vfp_reg * 2, reg);
                *ptr++ = fmrs(val_reg, vfp_reg * 2);
                if (imm_offset > -4096 && imm_offset < 4096) {
                    *ptr++ = str_offset(int_reg, val_reg, imm_offset);
                } else {
                    uint8_t off = RA_AllocARMRegister(&ptr);

                    *ptr++ = movw_immed_u16(off, (imm_offset) & 0xffff);
                    imm_offset >>= 16;
                    if (imm_offset)
                        *ptr++ = movt_immed_u16(off, (imm_offset) & 0xffff);
                    *ptr++ = add_reg(off, int_reg, off, LSL, 0);
                    *ptr++ = str_offset(off, val_reg, 0);

                    RA_FreeARMRegister(&ptr, off);
                }
#endif
                break;
            case SIZE_W:
#ifdef __aarch64__
                val_reg = RA_AllocARMRegister(&ptr);
                EMIT_fcvtzs_Dto32(&ptr, val_reg, reg);

                if (pre_sz)
                {
                    *ptr++ = strh_offset_preindex(int_reg, val_reg, pre_sz);
                }
                else if (post_sz)
                {
                    *ptr++ = strh_offset_postindex(int_reg, val_reg, post_sz);
                }
                else if (imm_offset >= -255 && imm_offset <= 255)
                {
                    *ptr++ = sturh_offset(int_reg, val_reg, imm_offset);
                }
                else if (imm_offset >= 0 && imm_offset < 8190 && !(imm_offset & 1))
                {
                    *ptr++ = strh_offset(int_reg, val_reg, imm_offset);
                }
                else
                {
                    uint8_t off = RA_AllocARMRegister(&ptr);
                    if (imm_offset > -4096 && imm_offset < 0)
                    {
                        *ptr++ = sub_immed(off, int_reg, -imm_offset);
                    }
                    else if (imm_offset >= 0 && imm_offset < 4096)
                    {
                        *ptr++ = add_immed(off, int_reg, imm_offset);
                    }
                    else
                    {
                        *ptr++ = movw_immed_u16(off, (imm_offset) & 0xffff);
                        imm_offset >>= 16;
                        if (imm_offset)
                            *ptr++ = movt_immed_u16(off, (imm_offset) & 0xffff);
                        *ptr++ = add_reg(off, int_reg, off, LSL, 0);
                    }
                    *ptr++ = strh_offset(off, val_reg, 0);
                    RA_FreeARMRegister(&ptr, off);
                }
#else
                val_reg = RA_AllocARMRegister(&ptr);
                *ptr++ = ftosid(vfp_reg * 2, reg);
                *ptr++ = fmrs(val_reg, vfp_reg * 2);
                if (imm_offset > -256 && imm_offset < 256) {
                    *ptr++ = strh_offset(int_reg, val_reg, imm_offset);
                } else {
                    uint8_t off = RA_AllocARMRegister(&ptr);

                    *ptr++ = movw_immed_u16(off, (imm_offset) & 0xffff);
                    imm_offset >>= 16;
                    if (imm_offset)
                        *ptr++ = movt_immed_u16(off, (imm_offset) & 0xffff);
                    *ptr++ = add_reg(off, int_reg, off, LSL, 0);
                    *ptr++ = strh_offset(off, val_reg, 0);

                    RA_FreeARMRegister(&ptr, off);
                }
#endif
                break;
            case SIZE_B:
#ifdef __aarch64__
                val_reg = RA_AllocARMRegister(&ptr);
                EMIT_fcvtzs_Dto32(&ptr, val_reg, reg);

                if (pre_sz)
                {
                    *ptr++ = strb_offset_preindex(int_reg, val_reg, pre_sz);
                }
                else if (post_sz)
                {
                    *ptr++ = strb_offset_postindex(int_reg, val_reg, post_sz);
                }
                else if (imm_offset >= -255 && imm_offset <= 255)
                {
                    *ptr++ = sturb_offset(int_reg, val_reg, imm_offset);
                }
                else if (imm_offset >= 0 && imm_offset < 4096)
                {
                    *ptr++ = strb_offset(int_reg, val_reg, imm_offset);
                }
                else
                {
                    uint8_t off = RA_AllocARMRegister(&ptr);
                    if (imm_offset > -4096 && imm_offset < 0)
                    {
                        *ptr++ = sub_immed(off, int_reg, -imm_offset);
                    }
                    else if (imm_offset >= 0 && imm_offset < 4096)
                    {
                        *ptr++ = add_immed(off, int_reg, imm_offset);
                    }
                    else
                    {
                        *ptr++ = movw_immed_u16(off, (imm_offset) & 0xffff);
                        imm_offset >>= 16;
                        if (imm_offset)
                            *ptr++ = movt_immed_u16(off, (imm_offset) & 0xffff);
                        *ptr++ = add_reg(off, int_reg, off, LSL, 0);
                    }
                    *ptr++ = strb_offset(off, val_reg, 0);
                    RA_FreeARMRegister(&ptr, off);
                }
#else
                val_reg = RA_AllocARMRegister(&ptr);
                *ptr++ = ftosid(vfp_reg * 2, reg);
                *ptr++ = fmrs(val_reg, vfp_reg * 2);
                if (imm_offset > -4096 && imm_offset < 4096) {
                    *ptr++ = strb_offset(int_reg, val_reg, imm_offset);
                } else {
                    uint8_t off = RA_AllocARMRegister(&ptr);

                    *ptr++ = movw_immed_u16(off, (imm_offset) & 0xffff);
                    imm_offset >>= 16;
                    if (imm_offset)
                        *ptr++ = movt_immed_u16(off, (imm_offset) & 0xffff);
                    *ptr++ = add_reg(off, int_reg, off, LSL, 0);
                    *ptr++ = strb_offset(off, val_reg, 0);

                    RA_FreeARMRegister(&ptr, off);
                }
#endif
                break;
            default:
                break;
        }

#ifndef __aarch64__
        if (post_sz)
            *ptr++ = add_immed(int_reg, int_reg, post_sz);
#endif

        if ((mode == 4) || (mode == 3))
        {
            RA_SetDirtyM68kRegister(&ptr, 8 + (opcode & 7));
        }

        RA_FreeFPURegister(&ptr, vfp_reg);
        RA_FreeARMRegister(&ptr, int_reg);
        RA_FreeARMRegister(&ptr, val_reg);
    }

    return ptr;
}

#ifdef __aarch64__
void clear_entire_dcache(void);
/* Clean and invalidate entire data cache, code after ARMv8 architecture reference manual */

void invalidate_entire_dcache(void);
/* Invalidate entire data cache, code after ARMv8 architecture reference manual */

#else
/* Clean and invalidate entire data cache, code after ARMv7 architecture reference manual */
void __attribute__((naked)) clear_entire_dcache(void)
{
    asm volatile(
"   push {r0, r1, r2, r3, r4, r5, r7, r9, r10, r11, lr} \n"
"   mrc p15, 1, r0, c0, c0, 1   \n"     // Read CLIDR into R0
"   ands r3, r0, #0x07000000    \n"
"   mov r3, r3, lsr #23         \n"     // Cache level value (naturally aligned)
"   beq 5f                      \n"
"   mov r10, #0                 \n"
"1: add r2, r10, r10, lsr #1    \n"     // Work out 3 x cachelevel
"   mov r1, r0, lsr r2          \n"     // bottom 3 bits are the Cache type for this level
"   and r1, r1, #7              \n"     // get those 3 bits alone
"   cmp r1, #2                  \n"
"   blt 4f                      \n"     // no cache or only instruction cache at this level
"   mcr p15, 2, r10, c0, c0, 0  \n"     // write CSSELR from R10
"   isb                         \n"     // ISB to sync the change to the CCSIDR
"   mrc p15, 1, r1, c0, c0, 0   \n"     // read current CCSIDR to R1
"   and r2, r1, #7              \n"     // extract the line length field
"   add r2, r2, #4              \n"     // add 4 for the line length offset (log2 16 bytes)
"   ldr r4, =0x3FF              \n"
"   ands r4, r4, r1, lsr #3     \n"     // R4 is the max number on the way size (right aligned)
"   clz r5, r4                  \n"     // R5 is the bit position of the way size increment
"   mov r9, r4                  \n"     // R9 working copy of the max way size (right aligned)
"2: ldr r7, =0x00007FFF         \n"
"   ands r7, r7, r1, lsr #13    \n"     // R7 is the max number of the index size (right aligned)
"3: orr r11, r10, r9, lsl r5    \n"     // factor in the way number and cache number into R11
"   orr r11, r11, r7, lsl r2    \n"     // factor in the index number
"   mcr p15, 0, r11, c7, c14, 2 \n"     // clean and invalidate by set/way
"   subs r7, r7, #1             \n"     // decrement the index
"   bge 3b                      \n"
"   subs r9, r9, #1             \n"     // decrement the way number
"   bge 2b                      \n"
"4: add r10, r10, #2            \n"     // increment the cache number
"   cmp r3, r10                 \n"
"   bgt 1b                      \n"
"   dsb                         \n"
"5: pop {r0, r1, r2, r3, r4, r5, r7, r9, r10, r11, pc} \n"
"   .ltorg                      \n"
    );
}

/* Clean and invalidate entire data cache, code after ARMv7 architecture reference manual */
void __attribute__((naked)) invalidate_entire_dcache(void)
{
    asm volatile(
"   push {r4, r5, r7, r9, r10, r11, lr} \n"
"   mrc p15, 1, r0, c0, c0, 1   \n"     // Read CLIDR into R0
"   ands r3, r0, #0x07000000    \n"
"   mov r3, r3, lsr #23         \n"     // Cache level value (naturally aligned)
"   beq 5f                      \n"
"   mov r10, #0                 \n"
"1: add r2, r10, r10, lsr #1    \n"     // Work out 3 x cachelevel
"   mov r1, r0, lsr r2          \n"     // bottom 3 bits are the Cache type for this level
"   and r1, r1, #7              \n"     // get those 3 bits alone
"   cmp r1, #2                  \n"
"   blt 4f                      \n"     // no cache or only instruction cache at this level
"   mcr p15, 2, r10, c0, c0, 0  \n"     // write CSSELR from R10
"   isb                         \n"     // ISB to sync the change to the CCSIDR
"   mrc p15, 1, r1, c0, c0, 0   \n"     // read current CCSIDR to R1
"   and r2, r1, #7              \n"     // extract the line length field
"   add r2, r2, #4              \n"     // add 4 for the line length offset (log2 16 bytes)
"   ldr r4, =0x3FF              \n"
"   ands r4, r4, r1, lsr #3     \n"     // R4 is the max number on the way size (right aligned)
"   clz r5, r4                  \n"     // R5 is the bit position of the way size increment
"   mov r9, r4                  \n"     // R9 working copy of the max way size (right aligned)
"2: ldr r7, =0x00007FFF         \n"
"   ands r7, r7, r1, lsr #13    \n"     // R7 is the max number of the index size (right aligned)
"3: orr r11, r10, r9, lsl r5    \n"     // factor in the way number and cache number into R11
"   orr r11, r11, r7, lsl r2    \n"     // factor in the index number
"   mcr p15, 0, r11, c7, c6, 2  \n"     // invalidate by set/way
"   subs r7, r7, #1             \n"     // decrement the index
"   bge 3b                      \n"
"   subs r9, r9, #1             \n"     // decrement the way number
"   bge 2b                      \n"
"4: add r10, r10, #2            \n"     // increment the cache number
"   cmp r3, r10                 \n"
"   bgt 1b                      \n"
"   dsb                         \n"
"5: pop {r4, r5, r7, r9, r10, r11, pc} \n"
"   .ltorg                      \n"
    );
}
#endif

void __clear_cache(void *begin, void *end);

#define MAX_EPILOGUE_LENGTH 256
uint32_t icache_epilogue[MAX_EPILOGUE_LENGTH];

void __attribute__((weak)) m68k_icache_announce(void)
{
}

void *invalidate_instruction_cache(uintptr_t target_addr, uint16_t *pc, uint32_t *arm_pc)
{
    m68k_icache_announce();
    int i;
    uint16_t opcode = BE16(pc[0]);
    struct M68KTranslationUnit *u;
    struct Node *n, *next;
    extern struct List LRU;
    extern void *jit_tlsf;
    extern struct M68KState *__m68k_state;
    extern void jit_evict_flush(struct M68KTranslationUnit *u);
    extern void a1_tlsf_free(void *handle, void *ptr);
    #ifndef __aarch64__
    extern uint32_t last_PC;
    #endif

    (void)jit_tlsf;

    //kprintf("[LINEF] ICache flush... Opcode=%04x, Target=%08x, PC=%08x, ARM PC=%p\n", opcode, target_addr, pc, arm_pc);
    // kprintf("[LINEF] ARM insn: %08x\n", *arm_pc);

    for (i=0; i < MAX_EPILOGUE_LENGTH; i++)
    {
        if (arm_pc[i] == 0xffffffff)
            break;

        icache_epilogue[i] = arm_pc[i];
    }

    //kprintf("[LINEF] Copied %d instructions of epilogue\n", i);
    __clear_cache(&icache_epilogue[0], &icache_epilogue[i]);

    #ifdef __aarch64__
    asm volatile("mcr p15, 0, %0, c13, c0, 4"::   "r"(0xffffffff));
    #else
    last_PC = 0xffffffff;
    #endif

    /* Get the scope */
    switch (opcode & 0x18) {
        case 0x08:  /* Line */
            // kprintf("[LINEF] Invalidating line\n");
            ForeachNodeSafe(&LRU, n, next)
            {
                u = (struct M68KTranslationUnit *)((intptr_t)n - __builtin_offsetof(struct M68KTranslationUnit, mt_LRUNode));

                // If highest address of unit is lower than the begin flushed area, or lowest address of unit higher than the flushed area end
                // then skip the unit
                if ((uintptr_t)u->mt_M68kLow > ((target_addr + 16) & ~15) || (uintptr_t)u->mt_M68kHigh < (target_addr & ~15))
                    continue;

                if (jit_soft_flush_active())
                {
                    jit_unit_mark(u);
                }
                else
                {
                    // kprintf("[LINEF] Unit %p, %08x-%08x match! Removing.\n", u, u->mt_M68kLow, u->mt_M68kHigh);
                    REMOVE(&u->mt_LRUNode);
                    REMOVE(&u->mt_HashNode);
                    jit_evict_flush(u);
                    a1_tlsf_free(jit_tlsf, u);

                    __m68k_state->JIT_UNIT_COUNT--;
                    __m68k_state->JIT_CACHE_FREE = tlsf_get_free_size(jit_tlsf);
                }
            }
            break;
        case 0x10:  /* Page */
            // kprintf("[LINEF] Invalidating page\n");
            ForeachNodeSafe(&LRU, n, next)
            {
                u = (struct M68KTranslationUnit *)((intptr_t)n - __builtin_offsetof(struct M68KTranslationUnit, mt_LRUNode));

                // If highest address of unit is lower than the begin flushed area, or lowest address of unit higher than the flushed area end
                // then skip the unit
                if ((uintptr_t)u->mt_M68kLow > ((target_addr + 4096) & ~4095) || (uintptr_t)u->mt_M68kHigh < (target_addr & ~4095))
                    continue;

                // kprintf("[LINEF] Unit %p, %08x-%08x match! Removing.\n", u, u->mt_M68kLow, u->mt_M68kHigh);

                if (jit_soft_flush_active())
                {
                    jit_unit_mark(u);
                }
                else
                {
                    REMOVE(&u->mt_LRUNode);
                    REMOVE(&u->mt_HashNode);
                    jit_evict_flush(u);
                    a1_tlsf_free(jit_tlsf, u);

                    __m68k_state->JIT_UNIT_COUNT--;
                    __m68k_state->JIT_CACHE_FREE = tlsf_get_free_size(jit_tlsf);
                }
            }
            break;
        case 0x18:  /* All */
            // kprintf("[LINEF] Invalidating all\n");            
            jit_flush_all_guest();
            break;
    }

    return &icache_epilogue[0];
}

#ifdef __aarch64__
void trampoline_icache_invalidate(void);
#else
void __attribute__((naked)) trampoline_icache_invalidate(void)
{
    asm volatile("bl invalidate_instruction_cache\n\tbx r0");
}
#endif

uint32_t *EMIT_FPU(uint32_t *ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed)
{
    uint16_t opcode = BE16((*m68k_ptr)[0]);
    uint16_t opcode2 = BE16((*m68k_ptr)[1]);
    uint8_t ext_count = 1;
    (*m68k_ptr)++;
    *insn_consumed = 1;

    RA_FlushOMMXRegs(&ptr);

    /* FMOVECR reg */
    if (opcode == 0xf200 && (opcode2 & 0xfc00) == 0x5c00)
    {
        union {
            double d;
            uint32_t u32[2];
        } u;
        uint8_t fp_dst = (opcode2 >> 7) & 7;
        uint8_t offset = opcode2 & 0x7f;

        /* Alloc destination FP register for write */
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        /*
            Load pointer to constants into base register, then load the value from table into
            destination VFP register, finally skip the base address (which is not an ARM INSN)
        */
        if (offset == C_10P0) {
            *ptr++ = fmov_1(fp_dst);
        }
        else if (offset == C_ZERO) {
            *ptr++ = fmov_0(fp_dst);
        }
        else {
            u.d = constants[offset];
#ifdef __aarch64__
            *ptr++ = fldd_pcrel(fp_dst, 2);
            *ptr++ = b(3);
            *ptr++ = u.u32[0];
            *ptr++ = u.u32[1];
#else
            *ptr++ = fldd(fp_dst, 15, 0);
            *ptr++ = b_cc(ARM_CC_AL, 3);
            *ptr++ = BE32(u.u32[0]);
            *ptr++ = BE32(u.u32[1]);
#endif
        }
        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            if (offset == C_ZERO)
            {
                *ptr++ = bic_immed(fpsr, fpsr, 4, 32 - FPSRB_NAN);
                *ptr++ = orr_immed(fpsr, fpsr, 1, 32 - FPSRB_Z);
            }
            else if (offset < C_ZERO || offset >= C_LN2)
            {
                *ptr++ = bic_immed(fpsr, fpsr, 4, 32 - FPSRB_NAN);
            }
            else
            {
                ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
            }
        }
    }
    /* FABS */
    else if ((opcode & 0xffc0) == 0xf200 && ((opcode2 & 0xa07f) == 0x0018 || (opcode2 & 0xa07b) == 0x0058))
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;
        uint8_t precision = 0;

        if (opcode2 & 0x0040) {
            if (opcode2 & 0x0004)
                precision = 8;
            else
                precision = 4;
        }

        (void)precision;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        *ptr++ = fabsd(fp_dst, fp_src);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }
    }
    /* FADD */
    else if ((opcode & 0xffc0) == 0xf200 && ((opcode2 & 0xa07f) == 0x0022 || (opcode2 & 0xa07b) == 0x0062))
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;
        uint8_t precision = 0;

        if (opcode2 & 0x0040) {
            if (opcode2 & 0x0004)
                precision = 8;
            else
                precision = 4;
        }

        (void)precision;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegister(&ptr, fp_dst);

        *ptr++ = faddd(fp_dst, fp_dst, fp_src);

        RA_SetDirtyFPURegister(&ptr, fp_dst);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        ptr = EMIT_RoundToPrec(ptr, fp_dst);
        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }
    }
    /* FBcc */
    else if ((opcode & 0xff80) == 0xf280)
    {
        uint8_t fpsr = RA_GetFPSR(&ptr);
        uint8_t predicate = opcode & 0x3f;
        uint8_t success_condition = 0;
        uint32_t *tmpptr;

        ptr = EMIT_FPUPredicate(ptr, fpsr, predicate, &success_condition);

        int8_t local_pc_off = 2;

        ptr = EMIT_GetOffsetPC(ptr, &local_pc_off);
        ptr = EMIT_ResetOffsetPC(ptr);

        uint8_t reg = RA_AllocARMRegister(&ptr);

        intptr_t branch_target = (intptr_t)(*m68k_ptr);
        intptr_t branch_offset = 0;

        /* use 16-bit offset */
        if ((opcode & 0x0040) == 0x0000)
        {
            branch_offset = (int16_t)BE16(*(*m68k_ptr)++);
        }
        /* use 32-bit offset */
        else
        {
            uint16_t lo16, hi16;
            hi16 = BE16(*(*m68k_ptr)++);
            lo16 = BE16(*(*m68k_ptr)++);
            branch_offset = lo16 | (hi16 << 16);
        }

        branch_offset += local_pc_off;

#ifdef __aarch64__
        uint8_t pc_yes = RA_AllocARMRegister(&ptr);
        uint8_t pc_no = RA_AllocARMRegister(&ptr);

        if (branch_offset > 0 && branch_offset < 4096)
            *ptr++ = add_immed(pc_yes, REG_PC, branch_offset);
        else if (branch_offset > -4096 && branch_offset < 0)
            *ptr++ = sub_immed(pc_yes, REG_PC, -branch_offset);
        else if (branch_offset != 0) {
            *ptr++ = movw_immed_u16(reg, branch_offset);
            if ((branch_offset >> 16) & 0xffff)
                *ptr++ = movt_immed_u16(reg, (branch_offset >> 16) & 0xffff);
            *ptr++ = add_reg(pc_yes, REG_PC, reg, LSL, 0);
        }
        else { *ptr++ = mov_reg(pc_yes, REG_PC); }
#else
        if (branch_offset > 0 && branch_offset < 255)
            *ptr++ = add_cc_immed(success_condition, REG_PC, REG_PC, branch_offset);
        else if (branch_offset > -256 && branch_offset < 0)
            *ptr++ = sub_cc_immed(success_condition, REG_PC, REG_PC, -branch_offset);
        else if (branch_offset != 0) {
            *ptr++ = movw_cc_immed_u16(success_condition, reg, branch_offset);
            if ((branch_offset >> 16) & 0xffff)
                *ptr++ = movt_cc_immed_u16(success_condition, reg, (branch_offset >> 16) & 0xffff);
            *ptr++ = add_cc_reg(success_condition, REG_PC, REG_PC, reg, 0);
        }

        /* armhf: no skip-branch needed; dual conditional movs cover both paths */
        (void)tmpptr;
#endif

        branch_target += branch_offset - local_pc_off;

        int16_t local_pc_off_16 = local_pc_off - 2;

        /* Adjust PC accordingly */
        if ((opcode & 0x0040) == 0x0000)
        {
            local_pc_off_16 += 4;
        }
        /* use 32-bit offset */
        else
        {
            local_pc_off_16 += 6;
        }

#ifdef __aarch64__
        if (local_pc_off_16 > 0 && local_pc_off_16 < 255)
            *ptr++ = add_immed(pc_no, REG_PC, local_pc_off_16);
        else if (local_pc_off_16 > -256 && local_pc_off_16 < 0)
            *ptr++ = sub_immed(pc_no, REG_PC, -local_pc_off_16);
        else if (local_pc_off_16 != 0) {
            *ptr++ = movw_immed_u16(reg, local_pc_off_16);
            if ((local_pc_off_16 >> 16) & 0xffff)
                *ptr++ = movt_immed_u16(reg, local_pc_off_16 >> 16);
            *ptr++ = add_reg(pc_no, REG_PC, reg, LSL, 0);
        }
        EMIT_csel(&ptr, REG_PC, pc_yes, pc_no, success_condition);
        RA_FreeARMRegister(&ptr, pc_yes);
        RA_FreeARMRegister(&ptr, pc_no);
        tmpptr = ptr;
#if EMU68_DEF_BRANCH_AUTO
        if(
            branch_target < (intptr_t)*m68k_ptr &&
            ((intptr_t)*m68k_ptr - branch_target) < EMU68_DEF_BRANCH_AUTO_RANGE
        )
            *ptr++ = b_cc(success_condition, 1);
        else
            *ptr++ = b_cc(success_condition^1, 1);
#else
#if EMU68_DEF_BRANCH_TAKEN
        *ptr++ = b_cc(success_condition, 1);
#else
        *ptr++ = b_cc(success_condition^1, 1);
#endif
#endif
#else
        /* armhf: not-taken path uses inverted condition - no skip-branch needed */
        if (local_pc_off_16 > 0 && local_pc_off_16 < 255)
            *ptr++ = add_cc_immed(success_condition^1, REG_PC, REG_PC, local_pc_off_16);
        else if (local_pc_off_16 > -256 && local_pc_off_16 < 0)
            *ptr++ = sub_cc_immed(success_condition^1, REG_PC, REG_PC, -local_pc_off_16);
        else if (local_pc_off_16 != 0) {
            *ptr++ = movw_cc_immed_u16(success_condition^1, reg, local_pc_off_16);
            if ((local_pc_off_16 >> 16) & 0xffff)
                *ptr++ = movt_cc_immed_u16(success_condition^1, reg, local_pc_off_16 >> 16);
            *ptr++ = add_cc_reg(success_condition^1, REG_PC, REG_PC, reg, 0);
        }
#endif

#if EMU68_DEF_BRANCH_AUTO
        if(
            branch_target < (intptr_t)*m68k_ptr &&
            ((intptr_t)*m68k_ptr - branch_target) < EMU68_DEF_BRANCH_AUTO_RANGE
        )
            *m68k_ptr = (uint16_t *)branch_target;
#else
#if EMU68_DEF_BRANCH_TAKEN
        *m68k_ptr = (uint16_t *)branch_target;
#endif
#endif
        RA_FreeARMRegister(&ptr, reg);
#ifdef __aarch64__
        *ptr++ = (uint32_t)(uintptr_t)tmpptr;
        *ptr++ = 1;
#else
        *ptr++ = 0;
#endif
        *ptr++ = branch_target;
        *ptr++ = INSN_TO_LE(0xfffffffe);
    }
    /* FCMP */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x0038)
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegister(&ptr, fp_dst);

        *ptr++ = fcmpd(fp_dst, fp_src);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);
            ptr = EMIT_GetFPUFlags(ptr, fpsr);
        }
    }
    /* FDIV */
    else if ((opcode & 0xffc0) == 0xf200 && ((opcode2 & 0xa07f) == 0x0020 || (opcode2 & 0xa07b) == 0x0060))
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;
        uint8_t precision = 0;

        if (opcode2 & 0x0040) {
            if (opcode2 & 0x0004)
                precision = 8;
            else
                precision = 4;
        }

        (void)precision;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegister(&ptr, fp_dst);

        *ptr++ = fdivd(fp_dst, fp_dst, fp_src);

        RA_SetDirtyFPURegister(&ptr, fp_dst);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        ptr = EMIT_RoundToPrec(ptr, fp_dst);
        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }
    }
    /* FSGLDIV */
    else if ((opcode & 0xffc0) == 0xf200 && ((opcode2 & 0xa07f) == 0x0024))
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;
        uint8_t precision = 0;

        if (opcode2 & 0x0040) {
            if (opcode2 & 0x0004)
                precision = 8;
            else
                precision = 4;
        }

        (void)precision;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegister(&ptr, fp_dst);

        *ptr++ = fdivd(fp_dst, fp_dst, fp_src);
        *ptr++ = fcvtsd(fp_dst, fp_dst);
        *ptr++ = fcvtds(fp_dst, fp_dst);

        RA_SetDirtyFPURegister(&ptr, fp_dst);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }
    }
    /* FSINCOS */
    else if ((opcode & 0xffc0) == 0xf200 && ((opcode2 & 0xa078) == 0x0030))
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst_sin = (opcode2 >> 7) & 7;
        uint8_t fp_dst_cos = opcode2 & 7;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst_sin = RA_MapFPURegisterForWrite(&ptr, fp_dst_sin);
        fp_dst_cos = RA_MapFPURegisterForWrite(&ptr, fp_dst_cos);

        *ptr++ = fcpyd(0, fp_src);

        union {
            uint64_t u64;
            uint32_t u32[2];
        } u;

        u.u64 = (uintptr_t)sincos;

        EMIT_stp64_preindex(&ptr, 31, 0, 1, -80);
        EMIT_stp64(&ptr, 31, 2, 3, 16);
        EMIT_stp64(&ptr, 31, 4, 5, 32);
        EMIT_stp64(&ptr, 31, 6, 7, 48);
        EMIT_str64_offset(&ptr, 31, 30, 64);

        *ptr++ = adr(30, 20);
        *ptr++ = ldr64_pcrel(0, 2);
        *ptr++ = br(0);

        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];

        *ptr++ = fcpyd(fp_dst_sin, 0);
        if (fp_dst_cos != fp_dst_sin)
            *ptr++ = fcpyd(fp_dst_cos, 1);

        EMIT_ldp64(&ptr, 31, 2, 3, 16);
        EMIT_ldp64(&ptr, 31, 4, 5, 32);
        EMIT_ldp64(&ptr, 31, 6, 7, 48);
        EMIT_ldr64_offset(&ptr, 31, 30, 64);
        EMIT_ldp64_postindex(&ptr, 31, 0, 1, 80);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst_sin);
        }

        *ptr++ = INSN_TO_LE(0xfffffff0);
    }
    /* FINT */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x0001)
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

#ifdef __aarch64__
        EMIT_frint64x(&ptr, fp_dst, fp_src);
#else
        ptr = EMIT_RoundToIntegral(ptr, fp_dst, fp_src, 0);
#endif

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }
    }
    /* FINTRZ */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x0003)
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

#ifdef __aarch64__
        EMIT_frint64z(&ptr, fp_dst, fp_src);
#else
        ptr = EMIT_RoundToIntegral(ptr, fp_dst, fp_src, 1);
#endif

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }
    }
    /* FLOGN */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x0014)
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        *ptr++ = fcpyd(0, fp_src);

        union {
            uint64_t u64;
            uint32_t u32[2];
        } u;

        u.u64 = (uintptr_t)log;

        EMIT_stp64_preindex(&ptr, 31, 0, 1, -80);
        EMIT_stp64(&ptr, 31, 2, 3, 16);
        EMIT_stp64(&ptr, 31, 4, 5, 32);
        EMIT_stp64(&ptr, 31, 6, 7, 48);
        EMIT_str64_offset(&ptr, 31, 30, 64);

        *ptr++ = adr(30, 20);
        *ptr++ = ldr64_pcrel(0, 2);
        *ptr++ = br(0);

        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];

        *ptr++ = fcpyd(fp_dst, 0);

        EMIT_ldp64(&ptr, 31, 2, 3, 16);
        EMIT_ldp64(&ptr, 31, 4, 5, 32);
        EMIT_ldp64(&ptr, 31, 6, 7, 48);
        EMIT_ldr64_offset(&ptr, 31, 30, 64);
        EMIT_ldp64_postindex(&ptr, 31, 0, 1, 80);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        ptr = EMIT_RoundToPrec(ptr, fp_dst);
        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }

        *ptr++ = INSN_TO_LE(0xfffffff0);
    }
    /* FLOGNP1 */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x0006)
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        *ptr++ = fcpyd(0, fp_src);

        union {
            uint64_t u64;
            uint32_t u32[2];
        } u;

        u.u64 = (uintptr_t)log1p;

        EMIT_stp64_preindex(&ptr, 31, 0, 1, -80);
        EMIT_stp64(&ptr, 31, 2, 3, 16);
        EMIT_stp64(&ptr, 31, 4, 5, 32);
        EMIT_stp64(&ptr, 31, 6, 7, 48);
        EMIT_str64_offset(&ptr, 31, 30, 64);

        *ptr++ = adr(30, 20);
        *ptr++ = ldr64_pcrel(0, 2);
        *ptr++ = br(0);

        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];

        *ptr++ = fcpyd(fp_dst, 0);

        EMIT_ldp64(&ptr, 31, 2, 3, 16);
        EMIT_ldp64(&ptr, 31, 4, 5, 32);
        EMIT_ldp64(&ptr, 31, 6, 7, 48);
        EMIT_ldr64_offset(&ptr, 31, 30, 64);
        EMIT_ldp64_postindex(&ptr, 31, 0, 1, 80);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }

        *ptr++ = INSN_TO_LE(0xfffffff0);
    }
    /* FLOG10 */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x0015)
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        *ptr++ = fcpyd(0, fp_src);

        union {
            uint64_t u64;
            uint32_t u32[2];
        } u;

        u.u64 = (uintptr_t)log10;

        EMIT_stp64_preindex(&ptr, 31, 0, 1, -80);
        EMIT_stp64(&ptr, 31, 2, 3, 16);
        EMIT_stp64(&ptr, 31, 4, 5, 32);
        EMIT_stp64(&ptr, 31, 6, 7, 48);
        EMIT_str64_offset(&ptr, 31, 30, 64);

        *ptr++ = adr(30, 20);
        *ptr++ = ldr64_pcrel(0, 2);
        *ptr++ = br(0);

        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];

        *ptr++ = fcpyd(fp_dst, 0);

        EMIT_ldp64(&ptr, 31, 2, 3, 16);
        EMIT_ldp64(&ptr, 31, 4, 5, 32);
        EMIT_ldp64(&ptr, 31, 6, 7, 48);
        EMIT_ldr64_offset(&ptr, 31, 30, 64);
        EMIT_ldp64_postindex(&ptr, 31, 0, 1, 80);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }

        *ptr++ = INSN_TO_LE(0xfffffff0);
    }
    /* FLOG2 */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x0016)
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        *ptr++ = fcpyd(0, fp_src);

        union {
            uint64_t u64;
            uint32_t u32[2];
        } u;

        u.u64 = (uintptr_t)log2;

        EMIT_stp64_preindex(&ptr, 31, 0, 1, -80);
        EMIT_stp64(&ptr, 31, 2, 3, 16);
        EMIT_stp64(&ptr, 31, 4, 5, 32);
        EMIT_stp64(&ptr, 31, 6, 7, 48);
        EMIT_str64_offset(&ptr, 31, 30, 64);

        *ptr++ = adr(30, 20);
        *ptr++ = ldr64_pcrel(0, 2);
        *ptr++ = br(0);

        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];

        *ptr++ = fcpyd(fp_dst, 0);

        EMIT_ldp64(&ptr, 31, 2, 3, 16);
        EMIT_ldp64(&ptr, 31, 4, 5, 32);
        EMIT_ldp64(&ptr, 31, 6, 7, 48);
        EMIT_ldr64_offset(&ptr, 31, 30, 64);
        EMIT_ldp64_postindex(&ptr, 31, 0, 1, 80);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }

        *ptr++ = INSN_TO_LE(0xfffffff0);
    }
    /* FETOX */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x0010)
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        *ptr++ = fcpyd(0, fp_src);

        union {
            uint64_t u64;
            uint32_t u32[2];
        } u;

        u.u64 = (uintptr_t)exp;

        EMIT_stp64_preindex(&ptr, 31, 0, 1, -80);
        EMIT_stp64(&ptr, 31, 2, 3, 16);
        EMIT_stp64(&ptr, 31, 4, 5, 32);
        EMIT_stp64(&ptr, 31, 6, 7, 48);
        EMIT_str64_offset(&ptr, 31, 30, 64);

        *ptr++ = adr(30, 20);
        *ptr++ = ldr64_pcrel(0, 2);
        *ptr++ = br(0);

        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];

        *ptr++ = fcpyd(fp_dst, 0);

        EMIT_ldp64(&ptr, 31, 2, 3, 16);
        EMIT_ldp64(&ptr, 31, 4, 5, 32);
        EMIT_ldp64(&ptr, 31, 6, 7, 48);
        EMIT_ldr64_offset(&ptr, 31, 30, 64);
        EMIT_ldp64_postindex(&ptr, 31, 0, 1, 80);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        ptr = EMIT_RoundToPrec(ptr, fp_dst);
        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }

        *ptr++ = INSN_TO_LE(0xfffffff0);
    }
    /* FETOXM1 */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x0008)
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        *ptr++ = fcpyd(0, fp_src);

        union {
            uint64_t u64;
            uint32_t u32[2];
        } u;

        u.u64 = (uintptr_t)expm1;

        EMIT_stp64_preindex(&ptr, 31, 0, 1, -80);
        EMIT_stp64(&ptr, 31, 2, 3, 16);
        EMIT_stp64(&ptr, 31, 4, 5, 32);
        EMIT_stp64(&ptr, 31, 6, 7, 48);
        EMIT_str64_offset(&ptr, 31, 30, 64);

        *ptr++ = adr(30, 20);
        *ptr++ = ldr64_pcrel(0, 2);
        *ptr++ = br(0);

        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];

        *ptr++ = fcpyd(fp_dst, 0);

        EMIT_ldp64(&ptr, 31, 2, 3, 16);
        EMIT_ldp64(&ptr, 31, 4, 5, 32);
        EMIT_ldp64(&ptr, 31, 6, 7, 48);
        EMIT_ldr64_offset(&ptr, 31, 30, 64);
        EMIT_ldp64_postindex(&ptr, 31, 0, 1, 80);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }

        *ptr++ = INSN_TO_LE(0xfffffff0);
    }
    /* FSINH */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x0002)
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        *ptr++ = fcpyd(0, fp_src);

        union {
            uint64_t u64;
            uint32_t u32[2];
        } u;

        u.u64 = (uintptr_t)sinh;

        EMIT_stp64_preindex(&ptr, 31, 0, 1, -80);
        EMIT_stp64(&ptr, 31, 2, 3, 16);
        EMIT_stp64(&ptr, 31, 4, 5, 32);
        EMIT_stp64(&ptr, 31, 6, 7, 48);
        EMIT_str64_offset(&ptr, 31, 30, 64);

        *ptr++ = adr(30, 20);
        *ptr++ = ldr64_pcrel(0, 2);
        *ptr++ = br(0);

        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];

        *ptr++ = fcpyd(fp_dst, 0);

        EMIT_ldp64(&ptr, 31, 2, 3, 16);
        EMIT_ldp64(&ptr, 31, 4, 5, 32);
        EMIT_ldp64(&ptr, 31, 6, 7, 48);
        EMIT_ldr64_offset(&ptr, 31, 30, 64);
        EMIT_ldp64_postindex(&ptr, 31, 0, 1, 80);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }

        *ptr++ = INSN_TO_LE(0xfffffff0);
    }
    /* FCOSH */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x0019)
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        *ptr++ = fcpyd(0, fp_src);

        union {
            uint64_t u64;
            uint32_t u32[2];
        } u;

        u.u64 = (uintptr_t)cosh;

        EMIT_stp64_preindex(&ptr, 31, 0, 1, -80);
        EMIT_stp64(&ptr, 31, 2, 3, 16);
        EMIT_stp64(&ptr, 31, 4, 5, 32);
        EMIT_stp64(&ptr, 31, 6, 7, 48);
        EMIT_str64_offset(&ptr, 31, 30, 64);

        *ptr++ = adr(30, 20);
        *ptr++ = ldr64_pcrel(0, 2);
        *ptr++ = br(0);

        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];

        *ptr++ = fcpyd(fp_dst, 0);

        EMIT_ldp64(&ptr, 31, 2, 3, 16);
        EMIT_ldp64(&ptr, 31, 4, 5, 32);
        EMIT_ldp64(&ptr, 31, 6, 7, 48);
        EMIT_ldr64_offset(&ptr, 31, 30, 64);
        EMIT_ldp64_postindex(&ptr, 31, 0, 1, 80);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }

        *ptr++ = INSN_TO_LE(0xfffffff0);
    }
    /* FATAN */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x000a)
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        *ptr++ = fcpyd(0, fp_src);

        union {
            uint64_t u64;
            uint32_t u32[2];
        } u;

        u.u64 = (uintptr_t)atan;

        EMIT_stp64_preindex(&ptr, 31, 0, 1, -80);
        EMIT_stp64(&ptr, 31, 2, 3, 16);
        EMIT_stp64(&ptr, 31, 4, 5, 32);
        EMIT_stp64(&ptr, 31, 6, 7, 48);
        EMIT_str64_offset(&ptr, 31, 30, 64);

        *ptr++ = adr(30, 20);
        *ptr++ = ldr64_pcrel(0, 2);
        *ptr++ = br(0);

        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];

        *ptr++ = fcpyd(fp_dst, 0);

        EMIT_ldp64(&ptr, 31, 2, 3, 16);
        EMIT_ldp64(&ptr, 31, 4, 5, 32);
        EMIT_ldp64(&ptr, 31, 6, 7, 48);
        EMIT_ldr64_offset(&ptr, 31, 30, 64);
        EMIT_ldp64_postindex(&ptr, 31, 0, 1, 80);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }

        *ptr++ = INSN_TO_LE(0xfffffff0);
    }
    /* FATANH */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x000d)
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        *ptr++ = fcpyd(0, fp_src);

        union {
            uint64_t u64;
            uint32_t u32[2];
        } u;

        u.u64 = (uintptr_t)atanh;

        EMIT_stp64_preindex(&ptr, 31, 0, 1, -80);
        EMIT_stp64(&ptr, 31, 2, 3, 16);
        EMIT_stp64(&ptr, 31, 4, 5, 32);
        EMIT_stp64(&ptr, 31, 6, 7, 48);
        EMIT_str64_offset(&ptr, 31, 30, 64);

        *ptr++ = adr(30, 20);
        *ptr++ = ldr64_pcrel(0, 2);
        *ptr++ = br(0);

        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];

        *ptr++ = fcpyd(fp_dst, 0);

        EMIT_ldp64(&ptr, 31, 2, 3, 16);
        EMIT_ldp64(&ptr, 31, 4, 5, 32);
        EMIT_ldp64(&ptr, 31, 6, 7, 48);
        EMIT_ldr64_offset(&ptr, 31, 30, 64);
        EMIT_ldp64_postindex(&ptr, 31, 0, 1, 80);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }

        *ptr++ = INSN_TO_LE(0xfffffff0);
    }
    /* FACOS */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x001c)
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        *ptr++ = fcpyd(0, fp_src);

        union {
            uint64_t u64;
            uint32_t u32[2];
        } u;

        u.u64 = (uintptr_t)acos;

        EMIT_stp64_preindex(&ptr, 31, 0, 1, -80);
        EMIT_stp64(&ptr, 31, 2, 3, 16);
        EMIT_stp64(&ptr, 31, 4, 5, 32);
        EMIT_stp64(&ptr, 31, 6, 7, 48);
        EMIT_str64_offset(&ptr, 31, 30, 64);

        *ptr++ = adr(30, 20);
        *ptr++ = ldr64_pcrel(0, 2);
        *ptr++ = br(0);

        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];

        *ptr++ = fcpyd(fp_dst, 0);

        EMIT_ldp64(&ptr, 31, 2, 3, 16);
        EMIT_ldp64(&ptr, 31, 4, 5, 32);
        EMIT_ldp64(&ptr, 31, 6, 7, 48);
        EMIT_ldr64_offset(&ptr, 31, 30, 64);
        EMIT_ldp64_postindex(&ptr, 31, 0, 1, 80);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }

        *ptr++ = INSN_TO_LE(0xfffffff0);
    }
    /* FASIN */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x000c)
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        *ptr++ = fcpyd(0, fp_src);

        union {
            uint64_t u64;
            uint32_t u32[2];
        } u;

        u.u64 = (uintptr_t)asin;

        EMIT_stp64_preindex(&ptr, 31, 0, 1, -80);
        EMIT_stp64(&ptr, 31, 2, 3, 16);
        EMIT_stp64(&ptr, 31, 4, 5, 32);
        EMIT_stp64(&ptr, 31, 6, 7, 48);
        EMIT_str64_offset(&ptr, 31, 30, 64);

        *ptr++ = adr(30, 20);
        *ptr++ = ldr64_pcrel(0, 2);
        *ptr++ = br(0);

        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];

        *ptr++ = fcpyd(fp_dst, 0);

        EMIT_ldp64(&ptr, 31, 2, 3, 16);
        EMIT_ldp64(&ptr, 31, 4, 5, 32);
        EMIT_ldp64(&ptr, 31, 6, 7, 48);
        EMIT_ldr64_offset(&ptr, 31, 30, 64);
        EMIT_ldp64_postindex(&ptr, 31, 0, 1, 80);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }

        *ptr++ = INSN_TO_LE(0xfffffff0);
    }
    /* FTAN */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x000f)
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        *ptr++ = fcpyd(0, fp_src);

        union {
            uint64_t u64;
            uint32_t u32[2];
        } u;

        u.u64 = (uintptr_t)tan;

        EMIT_stp64_preindex(&ptr, 31, 0, 1, -80);
        EMIT_stp64(&ptr, 31, 2, 3, 16);
        EMIT_stp64(&ptr, 31, 4, 5, 32);
        EMIT_stp64(&ptr, 31, 6, 7, 48);
        EMIT_str64_offset(&ptr, 31, 30, 64);

        *ptr++ = adr(30, 20);
        *ptr++ = ldr64_pcrel(0, 2);
        *ptr++ = br(0);

        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];

        *ptr++ = fcpyd(fp_dst, 0);

        EMIT_ldp64(&ptr, 31, 2, 3, 16);
        EMIT_ldp64(&ptr, 31, 4, 5, 32);
        EMIT_ldp64(&ptr, 31, 6, 7, 48);
        EMIT_ldr64_offset(&ptr, 31, 30, 64);
        EMIT_ldp64_postindex(&ptr, 31, 0, 1, 80);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }

        *ptr++ = INSN_TO_LE(0xfffffff0);
    }
    /* FTANH */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x0009)
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        *ptr++ = fcpyd(0, fp_src);

        union {
            uint64_t u64;
            uint32_t u32[2];
        } u;

        u.u64 = (uintptr_t)tanh;

        EMIT_stp64_preindex(&ptr, 31, 0, 1, -80);
        EMIT_stp64(&ptr, 31, 2, 3, 16);
        EMIT_stp64(&ptr, 31, 4, 5, 32);
        EMIT_stp64(&ptr, 31, 6, 7, 48);
        EMIT_str64_offset(&ptr, 31, 30, 64);

        *ptr++ = adr(30, 20);
        *ptr++ = ldr64_pcrel(0, 2);
        *ptr++ = br(0);

        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];

        *ptr++ = fcpyd(fp_dst, 0);

        EMIT_ldp64(&ptr, 31, 2, 3, 16);
        EMIT_ldp64(&ptr, 31, 4, 5, 32);
        EMIT_ldp64(&ptr, 31, 6, 7, 48);
        EMIT_ldr64_offset(&ptr, 31, 30, 64);
        EMIT_ldp64_postindex(&ptr, 31, 0, 1, 80);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }

        *ptr++ = INSN_TO_LE(0xfffffff0);
    }
    /* FTENTOX */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x0012)
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        *ptr++ = fcpyd(0, fp_src);

        union {
            uint64_t u64;
            uint32_t u32[2];
        } u;

        u.u64 = (uintptr_t)exp10;

        EMIT_stp64_preindex(&ptr, 31, 0, 1, -80);
        EMIT_stp64(&ptr, 31, 2, 3, 16);
        EMIT_stp64(&ptr, 31, 4, 5, 32);
        EMIT_stp64(&ptr, 31, 6, 7, 48);
        EMIT_str64_offset(&ptr, 31, 30, 64);

        *ptr++ = adr(30, 20);
        *ptr++ = ldr64_pcrel(0, 2);
        *ptr++ = br(0);

        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];

        *ptr++ = fcpyd(fp_dst, 0);

        EMIT_ldp64(&ptr, 31, 2, 3, 16);
        EMIT_ldp64(&ptr, 31, 4, 5, 32);
        EMIT_ldp64(&ptr, 31, 6, 7, 48);
        EMIT_ldr64_offset(&ptr, 31, 30, 64);
        EMIT_ldp64_postindex(&ptr, 31, 0, 1, 80);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }

        *ptr++ = INSN_TO_LE(0xfffffff0);
    }
    /* FTWOTOX */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x0011)
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        *ptr++ = fcpyd(0, fp_src);

        union {
            uint64_t u64;
            uint32_t u32[2];
        } u;

        u.u64 = (uintptr_t)exp2;

        EMIT_stp64_preindex(&ptr, 31, 0, 1, -80);
        EMIT_stp64(&ptr, 31, 2, 3, 16);
        EMIT_stp64(&ptr, 31, 4, 5, 32);
        EMIT_stp64(&ptr, 31, 6, 7, 48);
        EMIT_str64_offset(&ptr, 31, 30, 64);

        *ptr++ = adr(30, 20);
        *ptr++ = ldr64_pcrel(0, 2);
        *ptr++ = br(0);

        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];

        *ptr++ = fcpyd(fp_dst, 0);

        EMIT_ldp64(&ptr, 31, 2, 3, 16);
        EMIT_ldp64(&ptr, 31, 4, 5, 32);
        EMIT_ldp64(&ptr, 31, 6, 7, 48);
        EMIT_ldr64_offset(&ptr, 31, 30, 64);
        EMIT_ldp64_postindex(&ptr, 31, 0, 1, 80);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }

        *ptr++ = INSN_TO_LE(0xfffffff0);
    }
    /* FMOVE to REG */
    else if ((opcode & 0xffc0) == 0xf200 && ((opcode2 & 0xa07f) == 0x0000 || (opcode2 & 0xa07b) == 0x0040))
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;
        uint8_t precision = 0;

        if (opcode2 & 0x0040) {
            if (opcode2 & 0x0004)
                precision = 8;
            else
                precision = 4;
        }

        (void)precision;

        /* Was such a construct really necessary */
#if 0
        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        *ptr++ = fcpyd(fp_dst, fp_src);
#else
        if ((opcode2 & 0x4000) == 0)
        {
            fp_src = RA_MapFPURegister(&ptr, (opcode2 >> 10) & 7);
            fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);
            *ptr++ = fcpyd(fp_dst, fp_src);
        }
        else
        {
            fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);
            ptr = FPU_FetchData(ptr, m68k_ptr, &fp_dst, opcode, opcode2, &ext_count);
        }
#endif

        RA_FreeFPURegister(&ptr, fp_src);
        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }
    }
    /* FMOVE to MEM */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xe07f) == 0x6000)
    {
        uint8_t fp_src = (opcode2 >> 7) & 7;
        fp_src = RA_MapFPURegister(&ptr, fp_src);
        ptr = FPU_StoreData(ptr, m68k_ptr, fp_src, opcode, opcode2, &ext_count);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;
    }
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xe3ff) == 0xa000
             && (opcode2 & 0x1c00) != 0x1000 && (opcode2 & 0x1c00) != 0x0800
             && (opcode2 & 0x1c00) != 0x0400 && (opcode2 & 0x1c00) != 0x0000)
    {
        uint8_t reg_CTX = RA_GetCTX(&ptr);
        uint8_t addr = 0xff;
        uint8_t mode = (opcode & 0x38) >> 3;
        int slot = 0;
        int regnum = 0;

        if (opcode2 & 0x1000) regnum++;
        if (opcode2 & 0x0800) regnum++;
        if (opcode2 & 0x0400) regnum++;

        ptr = EMIT_LoadFromEffectiveAddress(ptr, 0, &addr, opcode & 0x3f, *m68k_ptr, &ext_count, 1, NULL);

        if (mode == 4) {
            *ptr++ = sub_immed(addr, addr, 4 * regnum);
            RA_SetDirtyM68kRegister(&ptr, 8 + (opcode & 7));
        }

        for (int which = 0; which < 3; which++)
        {
            uint8_t src;
            if (!(opcode2 & (0x1000 >> which)))
                continue;
            if (which == 0)      src = RA_GetFPCR(&ptr);
            else if (which == 1) src = RA_GetFPSR(&ptr);
            else {
                src = RA_AllocARMRegister(&ptr);
                *ptr++ = ldr_offset(reg_CTX, src, __builtin_offsetof(struct M68KState, FPIAR));
            }
            *ptr++ = str_offset(addr, src, 4 * slot++);
            if (which == 2)
                RA_FreeARMRegister(&ptr, src);
        }

        if (mode == 3) {
            *ptr++ = add_immed(addr, addr, 4 * regnum);
            RA_SetDirtyM68kRegister(&ptr, 8 + (opcode & 7));
        }

        RA_FreeARMRegister(&ptr, addr);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;
    }
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xe3ff) == 0x8000
             && (opcode2 & 0x1c00) != 0x1000 && (opcode2 & 0x1c00) != 0x0800
             && (opcode2 & 0x1c00) != 0x0400 && (opcode2 & 0x1c00) != 0x0000)
    {
        uint8_t reg_CTX = RA_GetCTX(&ptr);
        uint8_t addr = 0xff;
        uint8_t val = RA_AllocARMRegister(&ptr);
        uint8_t mode = (opcode & 0x38) >> 3;
        int offset = 0;
        int regnum = 0;

        if (opcode2 & 0x1000) regnum++;
        if (opcode2 & 0x0800) regnum++;
        if (opcode2 & 0x0400) regnum++;

        ptr = EMIT_LoadFromEffectiveAddress(ptr, 0, &addr, opcode & 0x3f, *m68k_ptr, &ext_count, 1, NULL);

        if (mode == 4) {
            *ptr++ = sub_immed(addr, addr, 4 * regnum);
            RA_SetDirtyM68kRegister(&ptr, 8 + (opcode & 7));
        }

        if (opcode2 & 0x1000)   /* FPCR - and the ARM rounding mode follows it, as in the single case */
        {
            uint8_t reg = RA_ModifyFPCR(&ptr);
            *ptr++ = ldr_offset(addr, val, offset);
            *ptr++ = mov_reg(reg, val);
#ifndef __aarch64__
            {
                uint8_t tmp = RA_AllocARMRegister(&ptr);
                *ptr++ = fmxr(tmp, FPSCR);
                *ptr++ = lsr_immed(val, val, 4);
                *ptr++ = _tst_val(val, 1);
                *ptr++ = eor_cc_immed(ARM_CC_NE, val, val, 2);
                *ptr++ = bfi(tmp, val, 22, 2);
                *ptr++ = fmrx(FPSCR, tmp);
                RA_FreeARMRegister(&ptr, tmp);
            }
#endif
            offset += 4;
        }
        if (opcode2 & 0x0800)   /* FPSR */
        {
            uint8_t reg = RA_ModifyFPSR(&ptr);
            *ptr++ = ldr_offset(addr, val, offset);
            *ptr++ = mov_reg(reg, val);
            offset += 4;
        }
        if (opcode2 & 0x0400)   /* FPIAR */
        {
            *ptr++ = ldr_offset(addr, val, offset);
            *ptr++ = str_offset(reg_CTX, val, __builtin_offsetof(struct M68KState, FPIAR));
            offset += 4;
        }

        if (mode == 3) {
            *ptr++ = add_immed(addr, addr, 4 * regnum);
            RA_SetDirtyM68kRegister(&ptr, 8 + (opcode & 7));
        }

        RA_FreeARMRegister(&ptr, val);
        RA_FreeARMRegister(&ptr, addr);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;
    }
    /* FMOVE from special */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xe3ff) == 0xa000)
    {
        uint8_t reg_CTX = RA_GetCTX(&ptr);
        uint8_t special_reg = 0xff;
        switch (opcode2 & 0x1c00)
        {
            case 0x1000:    /* FPCR */
                special_reg = RA_GetFPCR(&ptr);
                break;
            case 0x0800:    /* FPSR */
                special_reg = RA_GetFPSR(&ptr);
                break;
            case 0x0400:    /* FPIAR */
            case 0x0000:
                special_reg = RA_AllocARMRegister(&ptr);
                *ptr++ = ldr_offset(reg_CTX, special_reg, __builtin_offsetof(struct M68KState, FPIAR));
                break;
        }

        if ((opcode & 0x38) == 0)
        {
            uint8_t reg = RA_MapM68kRegisterForWrite(&ptr, opcode & 7);
            *ptr++ = mov_reg(reg, special_reg);
        }
        else
        {
            ptr = EMIT_StoreToEffectiveAddress(ptr, 4, &special_reg, opcode & 0x3f, *m68k_ptr, &ext_count);
        }

        if ((opcode2 & 0x1c00) == 0x0400)
            RA_FreeARMRegister(&ptr, special_reg);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;
    }
    /* FMOVE to special */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xe3ff) == 0x8000)
    {
        uint8_t src = 0xff;
        uint8_t tmp = 0xff;
        uint8_t reg = 0xff;
        uint8_t reg_CTX = RA_GetCTX(&ptr);

        ptr = EMIT_LoadFromEffectiveAddress(ptr, 4, &src, opcode & 0x3f, *m68k_ptr, &ext_count, 0, NULL);

        switch (opcode2 & 0x1c00)
        {
            case 0x1000:    /* FPCR */
                tmp = RA_AllocARMRegister(&ptr);
                reg = RA_ModifyFPCR(&ptr);
                *ptr++ = mov_reg(reg, src);
#ifndef __aarch64__
                *ptr++ = fmxr(tmp, FPSCR);
                *ptr++ = lsr_immed(src, src, 4);
                *ptr++ = _tst_val(src, 1);
                *ptr++ = eor_cc_immed(ARM_CC_NE, src, src, 2);
                *ptr++ = bfi(tmp, src, 22, 2);
                *ptr++ = fmrx(FPSCR, tmp);
#endif
                break;
            case 0x0800:    /* FPSR */
                reg = RA_ModifyFPSR(&ptr);
                *ptr++ = mov_reg(reg, src);
                break;
            case 0x0400:    /* FPIAR */
                *ptr++ = str_offset(reg_CTX, src, __builtin_offsetof(struct M68KState, FPIAR));
                break;
        }

        RA_FreeARMRegister(&ptr, src);
        RA_FreeARMRegister(&ptr, tmp);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

    }
    /* FMOVEM */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xc700) == 0xc000)
    {
        char dir = (opcode2 >> 13) & 1;
        int dyn = (opcode2 >> 11) & 1;
        uint8_t base_reg = 0xff;

        if (dir) { /* FPn to memory */
            uint8_t mode = (opcode & 0x0038) >> 3;

            if (mode == 4 || mode == 3)
                ptr = EMIT_LoadFromEffectiveAddress(ptr, 0, &base_reg, opcode & 0x3f, *m68k_ptr, &ext_count, 0, NULL);
            else
                ptr = EMIT_LoadFromEffectiveAddress(ptr, 0, &base_reg, opcode & 0x3f, *m68k_ptr, &ext_count, 1, NULL);

#ifndef __aarch64__
            if (dyn) {
                ptr = EMIT_FMOVEM_Dynamic(ptr, opcode2, base_reg, 1, mode == 4, mode == 4,
                                          8 + (opcode & 7));
                if (mode == 4)
                    RA_SetDirtyM68kRegister(&ptr, 8 + (opcode & 7));
            } else
#endif
            if (mode == 4) {
                int size = 0;
                int cnt = 0;
                for (int i=0; i < 8; i++)
                    if ((opcode2 & (1 << i)))
                        size++;
                *ptr++ = sub_immed(base_reg, base_reg, 12*size);

                for (int i=0; i < 8; i++) {
                    if ((opcode2 & (1 << i)) != 0) {
                        uint8_t fp_reg = RA_MapFPURegister(&ptr, i);
                        //*ptr++ = sub_immed(base_reg, base_reg, 12);
#ifdef __aarch64__
                        ptr = EMIT_Store96bitFP(ptr, fp_reg, base_reg, 12*cnt++);
#else
                        ptr = EMIT_StoreExtendedFP(ptr, fp_reg, base_reg, 12*cnt++);
#endif
                        RA_FreeFPURegister(&ptr, fp_reg);
                    }
                }
                RA_SetDirtyM68kRegister(&ptr, 8 + (opcode & 7));
            } else if (mode == 3) {
                kprintf("[JIT] Unsupported FMOVEM operation (REG to MEM postindex)\n");
            } else {
                int cnt = 0;
                for (int i=0; i < 8; i++) {
                    if ((opcode2 & (0x80 >> i)) != 0) {
                        uint8_t fp_reg = RA_MapFPURegister(&ptr, i);
#ifdef __aarch64__
                        ptr = EMIT_Store96bitFP(ptr, fp_reg, base_reg, 12*cnt);
#else
                        ptr = EMIT_StoreExtendedFP(ptr, fp_reg, base_reg, 12*cnt);
#endif
                        cnt++;
                        RA_FreeFPURegister(&ptr, fp_reg);
                    }
                }
            }
        } else { /* memory to FPn */
            uint8_t mode = (opcode & 0x0038) >> 3;

            if (mode == 4 || mode == 3)
                ptr = EMIT_LoadFromEffectiveAddress(ptr, 0, &base_reg, opcode & 0x3f, *m68k_ptr, &ext_count, 0, NULL);
            else
                ptr = EMIT_LoadFromEffectiveAddress(ptr, 0, &base_reg, opcode & 0x3f, *m68k_ptr, &ext_count, 1, NULL);

#ifndef __aarch64__
            if (dyn) {
                /* (An)+ is the write-back mode on this side; -(An) is illegal for mem->FPn. */
                ptr = EMIT_FMOVEM_Dynamic(ptr, opcode2, base_reg, 0, 0, mode == 3,
                                          8 + (opcode & 7));
                if (mode == 3)
                    RA_SetDirtyM68kRegister(&ptr, 8 + (opcode & 7));
            } else
#endif
            if (mode == 3) {
                int cnt = 0;
                for (int i=0; i < 8; i++) {
                    if ((opcode2 & (0x80 >> i)) != 0) {
                        uint8_t fp_reg = RA_MapFPURegisterForWrite(&ptr, i);
#ifdef __aarch64__
                        ptr = EMIT_Load96bitFP(ptr, fp_reg, base_reg, 12*cnt++);
#else
                        ptr = EMIT_LoadExtendedFP(ptr, fp_reg, base_reg, 12*cnt++);
#endif
                        //*ptr++ = add_immed(base_reg, base_reg, 12);
                        RA_FreeFPURegister(&ptr, fp_reg);
                    }
                }
                *ptr++ = add_immed(base_reg, base_reg, 12*cnt);
                RA_SetDirtyM68kRegister(&ptr, 8 + (opcode & 7));
            } else if (mode == 4) {
                kprintf("[JIT] Unsupported FMOVEM operation (REG to MEM preindex)\n");
            } else {
                int cnt = 0;
                for (int i=0; i < 8; i++) {
                    if ((opcode2 & (0x80 >> i)) != 0) {
                        uint8_t fp_reg = RA_MapFPURegisterForWrite(&ptr, i);
#ifdef __aarch64__
                        ptr = EMIT_Load96bitFP(ptr, fp_reg, base_reg, 12*cnt);
#else
                        ptr = EMIT_LoadExtendedFP(ptr, fp_reg, base_reg, 12*cnt);
#endif
                        cnt++;
                        RA_FreeFPURegister(&ptr, fp_reg);
                    }
                }
            }
        }

        RA_FreeARMRegister(&ptr, base_reg);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;
    }
    /* FMUL */
    else if ((opcode & 0xffc0) == 0xf200 && ((opcode2 & 0xa07f) == 0x0023 || (opcode2 & 0xa07b) == 0x0063))
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;
        uint8_t precision = 0;

        if (opcode2 & 0x0040) {
            if (opcode2 & 0x0004)
                precision = 8;
            else
                precision = 4;
        }

        (void)precision;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegister(&ptr, fp_dst);

        *ptr++ = fmuld(fp_dst, fp_dst, fp_src);

        RA_SetDirtyFPURegister(&ptr, fp_dst);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        ptr = EMIT_RoundToPrec(ptr, fp_dst);
        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }
    }
    /* FSGLMUL */
    else if ((opcode & 0xffc0) == 0xf200 && ((opcode2 & 0xa07f) == 0x0027))
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;
        uint8_t precision = 0;

        if (opcode2 & 0x0040) {
            if (opcode2 & 0x0004)
                precision = 8;
            else
                precision = 4;
        }

        (void)precision;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegister(&ptr, fp_dst);

        *ptr++ = fmuld(fp_dst, fp_dst, fp_src);
        *ptr++ = fcvtsd(fp_dst, fp_dst);
        *ptr++ = fcvtds(fp_dst, fp_dst);

        RA_SetDirtyFPURegister(&ptr, fp_dst);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }
    }
    /* FNEG */
    else if ((opcode & 0xffc0) == 0xf200 && ((opcode2 & 0xa07f) == 0x001a || (opcode2 & 0xa07b) == 0x005a))
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;
        uint8_t precision = 0;

        if (opcode2 & 0x0040) {
            if (opcode2 & 0x0004)
                precision = 8;
            else
                precision = 4;
        }

        (void)precision;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        *ptr++ = fnegd(fp_dst, fp_src);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }
    }
    /* FTST */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x003a)
    {
        uint8_t fp_src = 0xff;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);
            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_src);
        }

        RA_FreeFPURegister(&ptr, fp_src);
    }

    else if ((opcode & 0xfff8) == 0xf248 && (opcode2 & 0xffc0) == 0)
    {
#ifdef __aarch64__
        ptr = EMIT_FlushPC(ptr);
        ptr = EMIT_Exception(ptr, VECTOR_LINE_F, 0);
        *ptr++ = INSN_TO_LE(0xffffffff);
#else
        uint8_t predicate = opcode2 & 0x3f;
        int32_t branch_offset = 4 + (int16_t)BE16((*m68k_ptr)[1]);
        uint16_t *bra_rel_ptr = *m68k_ptr - 1;      /* the FDBcc opcode word itself */
#if EMU68_FDBCC_CLOSE
        const int close_loop = FDBCC_CLOSES_UNIT_LOOP((uintptr_t)bra_rel_ptr + branch_offset);
#endif

        if ((predicate & 0x0f) == F_CC_T)
        {
            /* FDBT is a three-word no-op: true means neither decrement nor branch. */
            ptr = EMIT_AdvancePC(ptr, 6);
            (*m68k_ptr) += 2;
        }
        else
        {
            uint8_t counter_reg = RA_MapM68kRegister(&ptr, opcode & 7);
            uint8_t c_true  = RA_AllocARMRegister(&ptr);
            uint8_t c_false = RA_AllocARMRegister(&ptr);
            uint8_t success_condition = 0;
            uint32_t *branch_1 = NULL;
            int8_t off8 = 0;
            int32_t off = 6;                        /* fall-through is past ALL THREE words */

            ptr = EMIT_GetOffsetPC(ptr, &off8);
            off += off8;
            ptr = EMIT_ResetOffsetPC(ptr);
            *ptr++ = add_immed(c_true, REG_PC, off);

            off = branch_offset + off8;
            if (off > -256 && off < 0)
                *ptr++ = sub_immed(c_false, REG_PC, -off);
            else if (off > 0 && off < 256)
                *ptr++ = add_immed(c_false, REG_PC, off);
            else if (off != 0)
            {
                uint8_t reg = RA_AllocARMRegister(&ptr);
                *ptr++ = movw_immed_u16(reg, off & 0xffff);
                *ptr++ = movt_immed_u16(reg, (off >> 16) & 0xffff);
                *ptr++ = add_reg(c_false, REG_PC, reg, LSL, 0);
                RA_FreeARMRegister(&ptr, reg);
            }
            else
                *ptr++ = mov_reg(c_false, REG_PC);

#if !DBCC_LATE_ALLOC
            uint8_t reg = RA_AllocARMRegister(&ptr);
#endif
            /* FDBF never takes this test: F is never true, so it always decrements. */
            if ((predicate & 0x0f) != F_CC_F)
            {
                uint8_t fpsr = RA_GetFPSR(&ptr);
                ptr = EMIT_FPUPredicate(ptr, fpsr, predicate, &success_condition);
                *ptr++ = mov_cc_reg(success_condition ^ 1, REG_PC, c_false);
                *ptr++ = mov_cc_reg(success_condition,     REG_PC, c_true);
                branch_1 = ptr;
                *ptr++ = b_cc(success_condition, 0);   /* predicate true -> leave the loop */
            }

            {
#if DBCC_LATE_ALLOC
                uint8_t reg = RA_AllocARMRegister(&ptr);
#endif
                *ptr++ = mov_reg_shift(reg, counter_reg, 16);   /* low word into the high half */
                *ptr++ = sub_immed(reg, reg, 0x801);            /* -0x10000, ENCODED imm12     */
                *ptr++ = cmn_immed(reg, 0x801);                 /* Z set iff it underflowed    */
                *ptr++ = lsr_immed(reg, reg, 16);
                *ptr++ = bfi(counter_reg, reg, 0, 16);          /* only the low word changes   */
                RA_SetDirtyM68kRegister(&ptr, opcode & 7);
                *ptr++ = mov_reg(REG_PC, c_false);              /* default: take the branch    */
                *ptr++ = mov_cc_reg(ARM_CC_EQ, REG_PC, c_true); /* underflowed: fall through   */
                RA_FreeARMRegister(&ptr, reg);
            }

#if EMU68_FDBCC_CLOSE
            if (close_loop)
            {
                uint32_t *skip = ptr;
                *ptr++ = b_cc(ARM_CC_NE, 1);
                if (branch_1)
                    *branch_1 = b_cc(success_condition, ptr - branch_1);
                *m68k_ptr = (void *)((uintptr_t)bra_rel_ptr + branch_offset);
                *ptr++ = (uint32_t)(uintptr_t)skip;
                *ptr++ = 1;
                *ptr++ = (uint32_t)((uintptr_t)bra_rel_ptr + branch_offset);
                *ptr++ = INSN_TO_LE(0xfffffffe);
                RA_FreeARMRegister(&ptr, c_true);
                RA_FreeARMRegister(&ptr, c_false);
                RA_FreeARMRegister(&ptr, counter_reg);
                return ptr;
            }
#endif
            if (branch_1)
                *branch_1 = b_cc(success_condition, ptr - branch_1);

            *m68k_ptr = (void *)((uintptr_t)bra_rel_ptr + branch_offset);
            *ptr++ = 0;
            *ptr++ = 0;
            *ptr++ = INSN_TO_LE(0xfffffffe);
            *ptr++ = INSN_TO_LE(0xfffffff1);

            RA_FreeARMRegister(&ptr, c_true);
            RA_FreeARMRegister(&ptr, c_false);
            RA_FreeARMRegister(&ptr, counter_reg);
        }
#endif
    }
    else if ((opcode & 0xfff8) == 0xf278 && (opcode & 7) >= 2 && (opcode2 & 0xffc0) == 0)
    {
        uint32_t source = (uint32_t)(uintptr_t)(*m68k_ptr - 1);
        uint8_t predicate = opcode2 & 0x3f;

        switch (opcode & 7)
        {
            case 4:  ptr = EMIT_AdvancePC(ptr, 4); (*m68k_ptr) += 1; break;  /* no operand   */
            case 2:  ptr = EMIT_AdvancePC(ptr, 6); (*m68k_ptr) += 2; break;  /* word operand */
            case 3:  ptr = EMIT_AdvancePC(ptr, 8); (*m68k_ptr) += 3; break;  /* long operand */
            default:
                ptr = EMIT_InjectDebugString(ptr, "[JIT] Illegal OPMODE %d in FTRAPcc at %08x\n",
                                             opcode & 7, source);
                ptr = EMIT_FlushPC(ptr);
                ptr = EMIT_Exception(ptr, VECTOR_ILLEGAL_INSTRUCTION, 0);
                *ptr++ = INSN_TO_LE(0xffffffff);
                break;
        }

        if ((opcode & 7) >= 2 && (opcode & 7) <= 4)
        {
            ptr = EMIT_FlushPC(ptr);

            if ((predicate & 0x0f) == F_CC_T)
            {
                ptr = EMIT_Exception(ptr, VECTOR_TRAPcc, 2, source);
                *ptr++ = INSN_TO_LE(0xffffffff);
            }
            else if ((predicate & 0x0f) != F_CC_F)
            {
                uint8_t fpsr = RA_GetFPSR(&ptr);
                uint8_t success_condition = 0;
                uint32_t *tmpptr;

                ptr = EMIT_FPUPredicate(ptr, fpsr, predicate, &success_condition);
                tmpptr = ptr;
                *ptr++ = b_cc(success_condition ^ 1, 0);
#ifdef __aarch64__
                ptr = EMIT_Exception(ptr, VECTOR_TRAPcc, 2, source);
                *tmpptr = b_cc(success_condition ^ 1, ptr - tmpptr);
                *ptr++ = (uint32_t)(uintptr_t)tmpptr;
                *ptr++ = 1;
                *ptr++ = 0;
                *ptr++ = INSN_TO_LE(0xfffffffe);
#else
                {
                    struct RA_Snapshot ra_snap;
                    uint32_t *ftrapcc_pop_loc = NULL;
                    RA_SaveState(&ra_snap);
                    ptr = EMIT_Exception(ptr, VECTOR_TRAPcc, 2, source);
                    ptr = EMIT_ExceptionUnitExit(ptr, &ftrapcc_pop_loc);
                    *tmpptr = b_cc(success_condition ^ 1, ptr - tmpptr);
                    RA_RestoreState(&ra_snap);
                    ptr = EMIT_ExceptionUnitExitTail(ptr, ftrapcc_pop_loc);
                }
#endif
            }
            /* F_CC_F: never traps, and the PC has already been advanced. */
        }
    }
    /* FScc */
    else if ((opcode & 0xffc0) == 0xf240 && (opcode2 & 0xffc0) == 0)
    {
        uint8_t fpsr = RA_GetFPSR(&ptr);
        uint8_t predicate = opcode2 & 0x3f;
        uint8_t success_condition = 0;

        ptr = EMIT_FPUPredicate(ptr, fpsr, predicate, &success_condition);

        if ((opcode & 0x38) == 0)
        {
            /* FScc Dx case */
            uint8_t dest = RA_MapM68kRegister(&ptr, opcode & 7);
            RA_SetDirtyM68kRegister(&ptr, opcode & 7);

#ifdef __aarch64__
            uint8_t tmp = RA_AllocARMRegister(&ptr);
            EMIT_csetm(&ptr, tmp, success_condition);
            *ptr++ = bfi(dest, tmp, 0, 8);
            RA_FreeARMRegister(&ptr, tmp);
#else
            *ptr++ = orr_cc_immed(success_condition, dest, dest, 0xff);
            *ptr++ = bfc_cc(success_condition ^ 1, dest, 0, 8);
#endif
        }
        else
        {
            uint8_t dest = RA_AllocARMRegister(&ptr);
            uint8_t tmp = RA_AllocARMRegister(&ptr);

#ifdef __aarch64__
            EMIT_csetm(&ptr, tmp, success_condition);
#else
            *ptr++ = mov_cc_immed_u8(success_condition ^ 1, tmp, 0);
            *ptr++ = mvn_cc_immed_u8(success_condition, tmp, 0);
#endif
            *ptr++ = mov_reg(dest, tmp);
            ptr = EMIT_StoreToEffectiveAddress(ptr, 1, &dest, opcode & 0x3f, *m68k_ptr, &ext_count);

            RA_FreeARMRegister(&ptr, tmp);
            RA_FreeARMRegister(&ptr, dest);
        }

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;
    }
#ifndef __aarch64__

    /* FGETEXP - the unbiased binary exponent, as a double */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x001e)
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;
        uint8_t hi = RA_AllocARMRegister(&ptr);
        uint8_t lo = RA_AllocARMRegister(&ptr);
        uint8_t eb = RA_AllocARMRegister(&ptr);
        uint8_t k  = RA_AllocARMRegister(&ptr);
        uint8_t vt = RA_AllocFPURegister(&ptr);

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        *ptr++ = fmrrd(hi, lo, fp_src);
        *ptr++ = ubfx(eb, hi, 20, 11);          /* the biased exponent field */

        *ptr++ = _bic_val(k, hi, 0x80000000);
        *ptr++ = orrs_reg(k, k, lo, 0);

        *ptr++ = movw_immed_u16(k, 1023);       /* 1023 is not an A32 imm12 - materialise it */
        *ptr++ = sub_reg(eb, eb, k, LSL, 0);
        *ptr++ = fmsr(vt * 2, eb);
        *ptr++ = fsitod(fp_dst, vt * 2);
        *ptr++ = fsubd_cc(ARM_CC_EQ, fp_dst, fp_dst, fp_dst);   /* zero -> +0.0 */

        RA_FreeFPURegister(&ptr, vt);
        RA_FreeARMRegister(&ptr, k);
        RA_FreeARMRegister(&ptr, eb);
        RA_FreeARMRegister(&ptr, lo);
        RA_FreeARMRegister(&ptr, hi);
        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);
            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }
    }
    /* FGETMAN - the mantissa in [1.0, 2.0), carrying the source's sign */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x001f)
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;
        uint8_t hi = RA_AllocARMRegister(&ptr);
        uint8_t lo = RA_AllocARMRegister(&ptr);
        uint8_t k  = RA_AllocARMRegister(&ptr);

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        *ptr++ = fmrrd(hi, lo, fp_src);
        *ptr++ = _bic_val(k, hi, 0x80000000);
        *ptr++ = orrs_reg(k, k, lo, 0);

        *ptr++ = movw_immed_u16(k, 0x3ff);      /* force the exponent to the bias -> [1,2) */
        *ptr++ = bfi(hi, k, 20, 11);
        *ptr++ = fmdrr(fp_dst, hi, lo);
        *ptr++ = fcpyd_cc(ARM_CC_EQ, fp_dst, fp_src);   /* zero -> the source, so +/-0 keeps its sign */

        RA_FreeARMRegister(&ptr, k);
        RA_FreeARMRegister(&ptr, lo);
        RA_FreeARMRegister(&ptr, hi);
        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);
            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }
    }
    else if ((opcode & 0xffc0) == 0xf200 &&
             ((opcode2 & 0xa07f) == 0x0021 || (opcode2 & 0xa07f) == 0x0025))
    {
        union { uint64_t u64; uint32_t u32[2]; } u;
        int is_rem = ((opcode2 & 0x007f) == 0x0025);
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegister(&ptr, fp_dst);       /* read-modify-write */

        u.u64 = (uintptr_t)(is_rem ? (void *)remquo : (void *)fmod);

        *ptr++ = fcpyd(0, fp_dst);
        *ptr++ = fcpyd(1, fp_src);

        EMIT_stp64_preindex(&ptr, 31, 0, 1, -80);
        if (is_rem)
        {
            *ptr++ = sub_immed(13, 13, 8);
            *ptr++ = mov_reg(0, 13);
        }
        *ptr++ = adr(30, 20);
        *ptr++ = ldr64_pcrel(12, 2);
        *ptr++ = br(12);
        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];
        *ptr++ = fcpyd(fp_dst, 0);
        if (is_rem)
            *ptr++ = add_immed(13, 13, 8);              /* give the quotient slot back */
        EMIT_ldp64_postindex(&ptr, 31, 0, 1, 80);

        RA_SetDirtyFPURegister(&ptr, fp_dst);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);
            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }
    }
    /* FSCALE - dst *= 2^trunc(src), through scalbn() */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x0026)
    {
        union { uint64_t u64; uint32_t u32[2]; } u;
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;
        uint8_t vt;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegister(&ptr, fp_dst);   /* read-modify-write, not ForWrite */
        vt = RA_AllocFPURegister(&ptr);

        u.u64 = (uintptr_t)scalbn;

        *ptr++ = fcpyd(0, fp_dst);
        *ptr++ = ftosidrz(vt * 2, fp_src);      /* the scale factor, truncated toward zero */

        EMIT_stp64_preindex(&ptr, 31, 0, 1, -80);
        *ptr++ = fmrs(0, vt * 2);
        *ptr++ = adr(30, 20);
        *ptr++ = ldr64_pcrel(12, 2);
        *ptr++ = br(12);
        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];
        *ptr++ = fcpyd(fp_dst, 0);
        EMIT_ldp64_postindex(&ptr, 31, 0, 1, 80);

        RA_SetDirtyFPURegister(&ptr, fp_dst);

        RA_FreeFPURegister(&ptr, vt);
        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);
            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }
    }
#endif /* !__aarch64__ */
    /* FSQRT */
    else if ((opcode & 0xffc0) == 0xf200 && ((opcode2 & 0xa07f) == 0x0004 || (opcode2 & 0xa07b) == 0x0041))
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;
        uint8_t precision = 0;

        if (opcode2 & 0x0040) {
            if (opcode2 & 0x0004)
                precision = 8;
            else
                precision = 4;
        }

        (void)precision;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

#ifdef __aarch64__
        *ptr++ = fsqrtd(fp_dst, fp_src);
#else
        if (Features.ARM_SUPPORTS_SQRT)
        {
            *ptr++ = fsqrtd(fp_dst, fp_src);
        }
        else
        {
            /* Missing... */
        }
#endif
        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        ptr = EMIT_RoundToPrec(ptr, fp_dst);
        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }
    }
    /* FSUB */
    else if ((opcode & 0xffc0) == 0xf200 && ((opcode2 & 0xa07f) == 0x0028 || (opcode2 & 0xa07b) == 0x0068))
    {
        uint8_t fp_src = 0xff;
        uint8_t fp_dst = (opcode2 >> 7) & 7;
        uint8_t precision = 0;

        if (opcode2 & 0x0040) {
            if (opcode2 & 0x0004)
                precision = 8;
            else
                precision = 4;
        }

        (void)precision;

        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);
        fp_dst = RA_MapFPURegister(&ptr, fp_dst);

        *ptr++ = fsubd(fp_dst, fp_dst, fp_src);

        RA_SetDirtyFPURegister(&ptr, fp_dst);

        RA_FreeFPURegister(&ptr, fp_src);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        ptr = EMIT_RoundToPrec(ptr, fp_dst);
        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }
    }
    /* FSIN */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x000e)
    {
        uint8_t base_reg = RA_AllocARMRegister(&ptr);
        uint8_t cmp_num = RA_AllocARMRegister(&ptr);
        uint8_t sign    = RA_AllocARMRegister(&ptr);

        uint8_t fp_tmp1 = RA_AllocFPURegister(&ptr);
        uint8_t fp_tmp2 = RA_AllocFPURegister(&ptr);

        uint8_t fp_dst = (opcode2 >> 7) & 7;
        uint8_t fp_src = 0xff;

        uint32_t *adr_sin;
        uint32_t *adr_cos;
        uint32_t *ref_ptr;
        uint32_t *exit_1;
        uint32_t *exit_2;
        uint32_t *exit_3;
#ifdef __aarch64__
        union {
            uint64_t u64;
            uint32_t u32[2];
        } u;

        /* Fetch source */
        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);

        /* Alloc destination FP register for write */
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        EMIT_stp64_preindex(&ptr, 31, 0, 30, -16);

        *ptr++ = ldr64_pcrel(base_reg, 2);
        *ptr++ = b(7);
        u.u64 = (uintptr_t)(&constants[0]);
        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];
        adr_sin = ptr;
        u.u64 = (uintptr_t)(PolySine);
        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];
        adr_cos = ptr;
        u.u64 = (uintptr_t)(PolyCosine);
        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];

        /* sin(x)=-sin(-x) -> tmp1 = |x| */
        *ptr++ = fabsd(fp_tmp1, fp_src);

        /* Divide x by 2*Pi -> result into d0 */
        *ptr++ = fldd_pimm(fp_tmp2, base_reg, C_1_2PI);
        *ptr++ = fmuld(0, fp_tmp1, fp_tmp2);

        /* Trim result to 0..2 range */
        EMIT_frint64z(&ptr, fp_tmp2, 0);
        *ptr++ = fsubd(fp_tmp2, 0, fp_tmp2);
        *ptr++ = faddd(0, fp_tmp2, fp_tmp2);

        *ptr++ = fmov_f64(fp_tmp1, 120); // 1.5 == fldd_pimm(fp_tmp1, base_reg, C_1_5);
        *ptr++ = fcmpd(0, fp_tmp1);
        *ptr++ = b_cc(A64_CC_MI, 6);

        *ptr++ = fsubd(0, 0, fp_tmp1);
        ref_ptr = ptr;
        *ptr++ = ldr64_pcrel(0, adr_cos - ref_ptr);
        *ptr++ = blr(0);
        *ptr++ = fnegd(0, 0);
        exit_1 = ptr;
        *ptr++ = b(0);

        *ptr++ = fmov_f64(fp_tmp1, 112); // 1.0 == fldd_pimm(fp_tmp1, base_reg, C_10P0);
        *ptr++ = fcmpd(0, fp_tmp1);
        *ptr++ = b_cc(A64_CC_MI, 6);

        *ptr++ = fsubd(0, 0, fp_tmp1);
        ref_ptr = ptr;
        *ptr++ = ldr64_pcrel(0, adr_sin - ref_ptr);
        *ptr++ = blr(0);
        *ptr++ = fnegd(0, 0);
        exit_2 = ptr;
        *ptr++ = b(0);

        *ptr++ = fmov_f64(fp_tmp1, 96); // 0.5 == fldd_pimm(fp_tmp1, base_reg, C_0_5);
        *ptr++ = fcmpd(0, fp_tmp1);
        *ptr++ = b_cc(A64_CC_MI, 5);

        *ptr++ = fsubd(0, 0, fp_tmp1);
        ref_ptr = ptr;
        *ptr++ = ldr64_pcrel(0, adr_cos - ref_ptr);
        *ptr++ = blr(0);
        exit_3 = ptr;
        *ptr++ = b(0);

        ref_ptr = ptr;
        *ptr++ = ldr64_pcrel(0, adr_sin - ref_ptr);
        *ptr++ = blr(0);

        *exit_1 = b(ptr - exit_1);
        *exit_2 = b(ptr - exit_2);
        *exit_3 = b(ptr - exit_3);

        *ptr++ = fcmpzd(fp_src);
        *ptr++ = b_cc(A64_CC_PL, 2);
        *ptr++ = fnegd(0, 0);
        *ptr++ = fcpyd(fp_dst, 0);

        EMIT_ldp64_postindex(&ptr, 31, 0, 30, 16);
#else
        uint8_t top_half = RA_AllocARMRegister(&ptr);

        uint32_t *tmp_ptr;
        uint32_t *adr_trim;

        *ptr++ = push(1 << 12);

        /* Fetch source */
        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);

        /* Alloc destination FP register for write */
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        /*
            Load pointer to constants into base register, then load the value from table into
            destination VFP register, finally skip the base address (which is not an ARM INSN)
        */
        *ptr++ = ldr_offset(15, base_reg, 0);
        *ptr++ = b_cc(ARM_CC_AL, 5);
        *ptr++ = BE32((uint32_t)(&constants[0]));
        adr_sin = ptr;
        *ptr++ = BE32((uint32_t)PolySine);
        adr_cos = ptr;
        *ptr++ = BE32((uint32_t)PolyCosine);
        adr_trim = ptr;
        *ptr++ = BE32((uint32_t)&TrimDoubleRange);

        *ptr++ = movw_immed_u16(cmp_num, 0);

        /* Get sign (topmost bit) to separate arm register */
        *ptr++ = INSN_TO_LE(0xee300b10 | (sign << 12) | (fp_src << 16));

        /* sin(x)=-sin(-x) -> tmp1 = |x| */
        *ptr++ = fabsd(fp_tmp1, fp_src);

        /* Divide x by Pi -> result into d0 */
        *ptr++ = fldd(fp_tmp2, base_reg, C_1_PI * 2);
        *ptr++ = fmuld(0, fp_tmp1, fp_tmp2);

        /* Trim range to 0..2 */
        (void)adr_trim;
        *ptr++ = fmov_imm(fp_tmp1, 96);                          /* 0.5 */
        *ptr++ = fmuld(fp_tmp1, 0, fp_tmp1);                     /* x/2 */
        ptr = EMIT_RoundToIntegral(ptr, fp_tmp2, fp_tmp1, 1);
        *ptr++ = faddd(fp_tmp2, fp_tmp2, fp_tmp2);               /* 2*trunc(x/2) */
        *ptr++ = fsubd(0, 0, fp_tmp2);                           /* x mod 2 */

        /*
            At this point d0 contains range trimmed to 0..2 corresponding to 0..2Pi.
            Load top half of the d0 register into ARM register
        */
        *ptr++ = INSN_TO_LE(0xee300b10 | (top_half << 12));

        /* Range 1: 0..0.5Pi - sin(x) uses sin(x) table */
        *ptr++ = movt_immed_u16(cmp_num, 0x3fe0);
        *ptr++ = cmp_reg(top_half, cmp_num, LSL, 0);
        tmp_ptr = ptr;
        *ptr++ = b_cc(ARM_CC_GT, 0);

        ref_ptr = ptr+2;
        *ptr++ = ldr_offset(15, 12, (intptr_t)adr_sin - (intptr_t)ref_ptr);
        *ptr++ = blx_cc_reg(ARM_CC_AL, 12);
        *ptr++ = fcpyd(fp_dst, 0);
        exit_1 = ptr;
        *ptr++ = b_cc(ARM_CC_AL, 0);
        *tmp_ptr = b_cc(ARM_CC_GT, ptr - tmp_ptr);

        /* Range 2: 0.5Pi..1, sin(x) = cos(x-0.5Pi) */
        *ptr++ = movt_immed_u16(cmp_num, 0x3ff0);
        *ptr++ = cmp_reg(top_half, cmp_num, LSL, 0);
        tmp_ptr = ptr;
        *ptr++ = b_cc(ARM_CC_GT, 0);
        *ptr++ = fldd(fp_tmp1, base_reg, C_0_5 * 2);
        *ptr++ = fsubd(0, 0, fp_tmp1);

        ref_ptr = ptr+2;
        *ptr++ = ldr_offset(15, 12, (intptr_t)adr_cos - (intptr_t)ref_ptr);
        *ptr++ = blx_cc_reg(ARM_CC_AL, 12);
        *ptr++ = fcpyd(fp_dst, 0);
        exit_2 = ptr;
        *ptr++ = b_cc(ARM_CC_AL, 0);
        *tmp_ptr = b_cc(ARM_CC_GT, ptr - tmp_ptr);

        /* Range 3: 1..1.5Pi, sin(x) = -sin(x-Pi) */
        *ptr++ = movt_immed_u16(cmp_num, 0x3ff8);
        *ptr++ = cmp_reg(top_half, cmp_num, LSL, 0);
        tmp_ptr = ptr;
        *ptr++ = b_cc(ARM_CC_GT, 0);
        *ptr++ = fldd(fp_tmp1, base_reg, C_10P0 * 2);
        *ptr++ = fsubd(0, 0, fp_tmp1);

        ref_ptr = ptr+2;
        *ptr++ = ldr_offset(15, 12, (intptr_t)adr_sin - (intptr_t)ref_ptr);
        *ptr++ = blx_cc_reg(ARM_CC_AL, 12);
        *ptr++ = fnegd(fp_dst, 0);
        exit_3 = ptr;
        *ptr++ = b_cc(ARM_CC_AL, 0);
        *tmp_ptr = b_cc(ARM_CC_GT, ptr - tmp_ptr);

        /* Range 4: 1.5Pi..2Pi, sin(x) = -cos(x-1.5Pi) */
        *ptr++ = fldd(fp_tmp1, base_reg, C_1_5 * 2);
        *ptr++ = fsubd(0, 0, fp_tmp1);

        ref_ptr = ptr+2;
        *ptr++ = ldr_offset(15, 12, (intptr_t)adr_cos - (intptr_t)ref_ptr);
        *ptr++ = blx_cc_reg(ARM_CC_AL, 12);
        *ptr++ = fnegd(fp_dst, 0);

        *exit_1 = b_cc(ARM_CC_AL, ptr - exit_1);
        *exit_2 = b_cc(ARM_CC_AL, ptr - exit_2);
        *exit_3 = b_cc(ARM_CC_AL, ptr - exit_3);

        *ptr++ = _tst_val(sign, 0x80000000);
        *ptr++ = fnegd_cc(ARM_CC_MI, fp_dst, fp_dst);

        *ptr++ = pop(1 << 12);



        RA_FreeARMRegister(&ptr, top_half);
#endif
        RA_FreeFPURegister(&ptr, fp_src);
        RA_FreeFPURegister(&ptr, fp_tmp1);
        RA_FreeFPURegister(&ptr, fp_tmp2);

        RA_FreeARMRegister(&ptr, sign);
        RA_FreeARMRegister(&ptr, cmp_num);
        RA_FreeARMRegister(&ptr, base_reg);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        ptr = EMIT_RoundToPrec(ptr, fp_dst);
        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }

        *ptr++ = INSN_TO_LE(0xfffffff0);
    }
    /* FCOS */
    else if ((opcode & 0xffc0) == 0xf200 && (opcode2 & 0xa07f) == 0x001d)
    {
        uint8_t base_reg = RA_AllocARMRegister(&ptr);
        uint8_t cmp_num = RA_AllocARMRegister(&ptr);

        uint8_t fp_tmp1 = RA_AllocFPURegister(&ptr);
        uint8_t fp_tmp2 = RA_AllocFPURegister(&ptr);

        uint8_t fp_dst = (opcode2 >> 7) & 7;
        uint8_t fp_src = 0xff;

        uint32_t *adr_sin;
        uint32_t *adr_cos;
        uint32_t *ref_ptr;
        uint32_t *exit_1;
        uint32_t *exit_2;
        uint32_t *exit_3;
#ifdef __aarch64__
        union {
            uint64_t u64;
            uint32_t u32[2];
        } u;

        /* Fetch source */
        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);

        /* Alloc destination FP register for write */
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        EMIT_stp64_preindex(&ptr, 31, 0, 30, -16);

        *ptr++ = ldr64_pcrel(base_reg, 2);
        *ptr++ = b(7);
        u.u64 = (uintptr_t)(&constants[0]);
        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];
        adr_sin = ptr;
        u.u64 = (uintptr_t)(PolySine);
        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];
        adr_cos = ptr;
        u.u64 = (uintptr_t)(PolyCosine);
        *ptr++ = u.u32[0];
        *ptr++ = u.u32[1];

        /* cos(x)=-cos(x) -> tmp1 = |x| */
        *ptr++ = fabsd(fp_tmp1, fp_src);

        /* Divide x by Pi -> result into d0 */
        *ptr++ = fldd_pimm(fp_tmp2, base_reg, C_1_2PI);
        *ptr++ = fmuld(fp_tmp1, fp_tmp1, fp_tmp2);

        /* Trim result to 0..2 range */
        EMIT_frint64z(&ptr, fp_tmp2, fp_tmp1);
        *ptr++ = fsubd(fp_tmp2, fp_tmp1, fp_tmp2);
        *ptr++ = faddd(0, fp_tmp2, fp_tmp2);

        *ptr++ = fmov_f64(fp_tmp1, 120); // 1.5 == fldd_pimm(fp_tmp1, base_reg, C_1_5);
        *ptr++ = fcmpd(0, fp_tmp1);
        *ptr++ = b_cc(A64_CC_MI, 5);

        *ptr++ = fsubd(0, 0, fp_tmp1);
        ref_ptr = ptr;
        *ptr++ = ldr64_pcrel(0, adr_sin - ref_ptr);
        *ptr++ = blr(0);
        exit_1 = ptr;
        *ptr++ = b(0);

        *ptr++ = fmov_f64(fp_tmp1, 112); // 1.0 == fldd_pimm(fp_tmp1, base_reg, C_10P0);
        *ptr++ = fcmpd(0, fp_tmp1);
        *ptr++ = b_cc(A64_CC_MI, 6);

        *ptr++ = fsubd(0, 0, fp_tmp1);
        ref_ptr = ptr;
        *ptr++ = ldr64_pcrel(0, adr_cos - ref_ptr);
        *ptr++ = blr(0);
        *ptr++ = fnegd(0, 0);
        exit_2 = ptr;
        *ptr++ = b(0);

        *ptr++ = fmov_f64(fp_tmp1, 96); // 0.5 == fldd_pimm(fp_tmp1, base_reg, C_0_5);
        *ptr++ = fcmpd(0, fp_tmp1);
        *ptr++ = b_cc(A64_CC_MI, 6);

        *ptr++ = fsubd(0, 0, fp_tmp1);
        ref_ptr = ptr;
        *ptr++ = ldr64_pcrel(0, adr_sin - ref_ptr);
        *ptr++ = blr(0);
        *ptr++ = fnegd(0, 0);
        exit_3 = ptr;
        *ptr++ = b(0);

        ref_ptr = ptr;
        *ptr++ = ldr64_pcrel(0, adr_cos - ref_ptr);
        *ptr++ = blr(0);

        *exit_1 = b(ptr - exit_1);
        *exit_2 = b(ptr - exit_2);
        *exit_3 = b(ptr - exit_3);

        *ptr++ = fcpyd(fp_dst, 0);

        EMIT_ldp64_postindex(&ptr, 31, 0, 30, 16);
#else
        uint8_t top_half = RA_AllocARMRegister(&ptr);
        uint32_t *tmp_ptr;
        uint32_t *adr_trim;

        *ptr++ = push(1 << 12);

        /* Fetch source */
        ptr = FPU_FetchData(ptr, m68k_ptr, &fp_src, opcode, opcode2, &ext_count);

        /* Alloc destination FP register for write */
        fp_dst = RA_MapFPURegisterForWrite(&ptr, fp_dst);

        /*
            Load pointer to constants into base register, then load the value from table into
            destination VFP register, finally skip the base address (which is not an ARM INSN)
        */
        *ptr++ = ldr_offset(15, base_reg, 0);
        *ptr++ = b_cc(ARM_CC_AL, 5);
        *ptr++ = BE32((uint32_t)(&constants[0]));
        adr_sin = ptr;
        *ptr++ = BE32((uint32_t)PolySine);
        adr_cos = ptr;
        *ptr++ = BE32((uint32_t)PolyCosine);
        adr_trim = ptr;
        *ptr++ = BE32((uint32_t)&TrimDoubleRange);

        *ptr++ = movw_immed_u16(cmp_num, 0);

        /* cos(x)=cos(-x) -> tmp1 = |x| */
        *ptr++ = fabsd(fp_tmp1, fp_src);

        /* Divide x by Pi -> result into d0 */
        *ptr++ = fldd(fp_tmp2, base_reg, C_1_PI * 2);
        *ptr++ = fmuld(0, fp_tmp1, fp_tmp2);

        /* Trim range to 0..2 */
        (void)adr_trim;
        *ptr++ = fmov_imm(fp_tmp1, 96);                          /* 0.5 */
        *ptr++ = fmuld(fp_tmp1, 0, fp_tmp1);                     /* x/2 */
        ptr = EMIT_RoundToIntegral(ptr, fp_tmp2, fp_tmp1, 1);
        *ptr++ = faddd(fp_tmp2, fp_tmp2, fp_tmp2);               /* 2*trunc(x/2) */
        *ptr++ = fsubd(0, 0, fp_tmp2);                           /* x mod 2 */

        /*
            At this point d0 contains range trimmed to 0..2 corresponding to 0..2Pi.
            Load top half of the d0 register into ARM register
        */
        *ptr++ = INSN_TO_LE(0xee300b10 | (top_half << 12));

        /* Range 1: 0..0.5Pi - cos(x) uses cos(x) table */
        *ptr++ = movt_immed_u16(cmp_num, 0x3fe0);
        *ptr++ = cmp_reg(top_half, cmp_num, LSL, 0);
        tmp_ptr = ptr;
        *ptr++ = b_cc(ARM_CC_GT, 0);

        ref_ptr = ptr+2;
        *ptr++ = ldr_offset(15, 12, (intptr_t)adr_cos - (intptr_t)ref_ptr);
        *ptr++ = blx_cc_reg(ARM_CC_AL, 12);
        *ptr++ = fcpyd(fp_dst, 0);
        exit_1 = ptr;
        *ptr++ = b_cc(ARM_CC_AL, 0);
        *tmp_ptr = b_cc(ARM_CC_GT, ptr - tmp_ptr);

        /* Range 2: 0.5Pi..1, cos(x) = -sin(x-0.5Pi) */
        *ptr++ = movt_immed_u16(cmp_num, 0x3ff0);
        *ptr++ = cmp_reg(top_half, cmp_num, LSL, 0);
        tmp_ptr = ptr;
        *ptr++ = b_cc(ARM_CC_GT, 0);
        *ptr++ = fldd(fp_tmp1, base_reg, C_0_5 * 2);
        *ptr++ = fsubd(0, 0, fp_tmp1);

        ref_ptr = ptr+2;
        *ptr++ = ldr_offset(15, 12, (intptr_t)adr_sin - (intptr_t)ref_ptr);
        *ptr++ = blx_cc_reg(ARM_CC_AL, 12);
        *ptr++ = fnegd(fp_dst, 0);
        exit_2 = ptr;
        *ptr++ = b_cc(ARM_CC_AL, 0);
        *tmp_ptr = b_cc(ARM_CC_GT, ptr - tmp_ptr);

        /* Range 3: 1..1.5Pi, cos(x) = -cos(x-Pi) */
        *ptr++ = movt_immed_u16(cmp_num, 0x3ff8);
        *ptr++ = cmp_reg(top_half, cmp_num, LSL, 0);
        tmp_ptr = ptr;
        *ptr++ = b_cc(ARM_CC_GT, 0);
        *ptr++ = fldd(fp_tmp1, base_reg, C_10P0 * 2);
        *ptr++ = fsubd(0, 0, fp_tmp1);

        ref_ptr = ptr+2;
        *ptr++ = ldr_offset(15, 12, (intptr_t)adr_cos - (intptr_t)ref_ptr);
        *ptr++ = blx_cc_reg(ARM_CC_AL, 12);
        *ptr++ = fnegd(fp_dst, 0);
        exit_3 = ptr;
        *ptr++ = b_cc(ARM_CC_AL, 0);
        *tmp_ptr = b_cc(ARM_CC_GT, ptr - tmp_ptr);

        /* Range 4: 1.5Pi..2Pi, cos(x) = sin(x-1.5Pi) */
        *ptr++ = fldd(fp_tmp1, base_reg, C_1_5 * 2);
        *ptr++ = fsubd(0, 0, fp_tmp1);

        ref_ptr = ptr+2;
        *ptr++ = ldr_offset(15, 12, (intptr_t)adr_sin - (intptr_t)ref_ptr);
        *ptr++ = blx_cc_reg(ARM_CC_AL, 12);
        *ptr++ = fcpyd(fp_dst, 0);

        *exit_1 = b_cc(ARM_CC_AL, ptr - exit_1);
        *exit_2 = b_cc(ARM_CC_AL, ptr - exit_2);
        *exit_3 = b_cc(ARM_CC_AL, ptr - exit_3);

        *ptr++ = pop(1 << 12);

        RA_FreeARMRegister(&ptr, top_half);
#endif
        RA_FreeFPURegister(&ptr, fp_src);
        RA_FreeFPURegister(&ptr, fp_tmp1);
        RA_FreeFPURegister(&ptr, fp_tmp2);
        RA_FreeARMRegister(&ptr, base_reg);
        RA_FreeARMRegister(&ptr, cmp_num);
        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;

        if (FPSR_Update_Needed(m68k_ptr))
        {
            uint8_t fpsr = RA_ModifyFPSR(&ptr);

            ptr = EMIT_GetFPUFlagsFromD(ptr, fpsr, fp_dst);
        }

        *ptr++ = INSN_TO_LE(0xfffffff0);

    }
    /* FNOP */
    else if (opcode == 0xf280 && opcode2 == 0)
    {
        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;
        ptr = EMIT_FlushPC(ptr);
    }
    /* FRESTORE */
    else if ((opcode & ~0x3f) == 0xf340 && 
             (opcode & 0x30) != 0x00 && 
             (opcode & 0x38) != 0x20 &&
             (opcode & 0x3f) <= 0x3b)
    {
        uint8_t tmp = -1;
        ext_count = 0;

        ptr = EMIT_LoadFromEffectiveAddress(ptr, 4, &tmp, opcode & 0x3f, *m68k_ptr, &ext_count, 0, NULL);

#ifdef EMU68_CPU040
        if (CPUMODEL_IS_060(CpuModel) && (opcode & 0x38) == 0x18)
        {
            uint8_t An = RA_MapM68kRegister(&ptr, 8 + (opcode & 7));
            RA_SetDirtyM68kRegister(&ptr, 8 + (opcode & 7));
            *ptr++ = add_immed(An, An, 8);
        }
        else
#endif
        if ((opcode & 0x38) == 0x18)
        {
            uint8_t An = RA_MapM68kRegister(&ptr, 8 + (opcode & 7));
            *ptr++ = tst_immed(tmp, 8, 8);          /* version byte == 0 -> null frame, 4 bytes  */
            *ptr++ = b_cc(A64_CC_EQ, 8);            /*   -> done                                 */
            *ptr++ = ubfx(tmp, tmp, 16, 8);
            *ptr++ = cmp_immed(tmp, 0x18);          /* 68881 idle                                */
            *ptr++ = b_cc(A64_CC_NE, 2);
            *ptr++ = add_immed(An, An, 28 - 4);     /*   28 total                                */
            *ptr++ = cmp_immed(tmp, 0x38);
            *ptr++ = b_cc(A64_CC_NE, 2);
            *ptr++ = add_immed(An, An, 60 - 4);     /*   60 total                                */
        }

        RA_FreeARMRegister(&ptr, tmp);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;
        ptr = EMIT_FlushPC(ptr);
    }
    /* FSAVE */
    else if ((opcode & ~0x3f) == 0xf300 && 
             (opcode & 0x30) != 0x00 && 
             (opcode & 0x38) != 0x18 &&
             (opcode & 0x3f) <= 0x39)
    {
        uint8_t tmp = RA_AllocARMRegister(&ptr);
        
        ext_count = 0;

#ifdef EMU68_CPU040
        if (CPUMODEL_IS_060(CpuModel))
        {
            uint8_t zero = RA_AllocARMRegister(&ptr);
            *ptr++ = mov_immed_u16(tmp, 0x6000, 0);   /* BE8: stores as 00 00 60 00 */
            *ptr++ = mov_immed_u8(zero, 0);
            if ((opcode & 0x38) == 0x20)              /* -(An): predecrement by the FULL 12 */
            {
                uint8_t An = RA_MapM68kRegister(&ptr, 8 + (opcode & 7));
                RA_SetDirtyM68kRegister(&ptr, 8 + (opcode & 7));
                *ptr++ = sub_immed(An, An, 12);
                *ptr++ = str_offset(An, tmp,  0);
                *ptr++ = str_offset(An, zero, 4);
                *ptr++ = str_offset(An, zero, 8);
            }
            else                                      /* (An), (d16,An), (d8,An,Xn), abs */
            {
                uint8_t addr = 0xff;
                ptr = EMIT_LoadFromEffectiveAddress(ptr, 0, &addr, opcode & 0x3f, *m68k_ptr, &ext_count, 0, NULL);
                *ptr++ = str_offset(addr, tmp,  0);
                *ptr++ = str_offset(addr, zero, 4);
                *ptr++ = str_offset(addr, zero, 8);
                RA_FreeARMRegister(&ptr, addr);
            }
            RA_FreeARMRegister(&ptr, zero);
        }
        else
#endif
        {
            *ptr++ = mov_immed_u16(tmp, 0x4100, 1);
            ptr = EMIT_StoreToEffectiveAddress(ptr, 4, &tmp, opcode & 0x3f, *m68k_ptr, &ext_count);
        }

        RA_FreeARMRegister(&ptr, tmp);

        ptr = EMIT_AdvancePC(ptr, 2 * (ext_count + 1));
        (*m68k_ptr) += ext_count;
    }
    else
    {
        ptr = EMIT_FlushPC(ptr);
        ptr = EMIT_InjectDebugString(ptr, "[JIT] opcode %04x:%04x at %08x not implemented\n", opcode, opcode2, *m68k_ptr - 1);
        ptr = EMIT_Exception(ptr, VECTOR_LINE_F, 0);
        *ptr++ = INSN_TO_LE(0xffffffff);
    }

    return ptr;
}

int DisableFPU = 0;

#ifdef EMU68_CPU040
#endif

uint32_t *EMIT_lineF(uint32_t *ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed)
{
    uint16_t opcode = BE16((*m68k_ptr)[0]);
    uint16_t opcode2 = BE16((*m68k_ptr)[1]);

    /* Check destination coprocessor - if it is FPU go to separate function */
    if (DisableFPU == 0 && (opcode & 0x0e00) == 0x0200)
    {
        return EMIT_FPU(ptr, m68k_ptr, insn_consumed);
    }
    /* MOVE16 (Ax)+, (Ay)+ */
    else if ((opcode & 0xfff8) == 0xf620) // && (opcode2 & 0x8fff) == 0x8000) <- don't test! Real m68k ignores that bit!
    {
        uint8_t aligned_src = RA_AllocARMRegister_NoFP(&ptr);
        uint8_t aligned_dst = RA_AllocARMRegister(&ptr);
        uint8_t buf1 = RA_AllocARMRegister(&ptr);
        uint8_t buf2 = RA_AllocARMRegister(&ptr);
#ifndef __aarch64__
        uint8_t buf3 = RA_AllocARMRegister(&ptr);
        uint8_t buf4 = RA_AllocARMRegister(&ptr);
#endif
        uint8_t src = RA_MapM68kRegister(&ptr, 8 + (opcode & 7));
        uint8_t dst = RA_MapM68kRegister(&ptr, 8 + ((opcode2 >> 12) & 7));

#ifdef __aarch64__
        *ptr++ = bic_immed(aligned_src, src, 4, 0);
        *ptr++ = bic_immed(aligned_dst, dst, 4, 0);
        EMIT_ldp64(&ptr, aligned_src, buf1, buf2, 0);
        EMIT_stp64(&ptr, aligned_dst, buf1, buf2, 0);
#else
        *ptr++ = _bic_val(aligned_src, src, 0x0f);
        *ptr++ = _bic_val(aligned_dst, dst, 0x0f);
        {
        *ptr++ = ldr_offset(aligned_src, buf1, 0);
        *ptr++ = ldr_offset(aligned_src, buf2, 4);
        *ptr++ = ldr_offset(aligned_src, buf3, 8);
        *ptr++ = ldr_offset(aligned_src, buf4, 12);
        *ptr++ = str_offset(aligned_dst, buf1, 0);
        *ptr++ = str_offset(aligned_dst, buf2, 4);
        *ptr++ = str_offset(aligned_dst, buf3, 8);
        *ptr++ = str_offset(aligned_dst, buf4, 12);
        }
#endif
        *ptr++ = add_immed(src, src, 16);
        // Update dst only if it is not the same as src!
        if (dst != src) {
            *ptr++ = add_immed(dst, dst, 16);
        }

        RA_SetDirtyM68kRegister(&ptr, 8 + (opcode & 7));
        RA_SetDirtyM68kRegister(&ptr, 8 + ((opcode2 >> 12) & 7));

        RA_FreeARMRegister(&ptr, aligned_src);
        RA_FreeARMRegister(&ptr, aligned_dst);
        RA_FreeARMRegister(&ptr, buf1);
        RA_FreeARMRegister(&ptr, buf2);
#ifndef __aarch64__
        RA_FreeARMRegister(&ptr, buf3);
        RA_FreeARMRegister(&ptr, buf4);
#endif
        (*m68k_ptr)+=2;
        *insn_consumed = 1;
        ptr = EMIT_AdvancePC(ptr, 4);
    }
    /* MOVE16 other variations */
    else if ((opcode & 0xffe0) == 0xf600)
    {
        uint8_t aligned_reg = RA_AllocARMRegister(&ptr);
        uint8_t aligned_mem = RA_AllocARMRegister(&ptr);
        uint8_t buf1 = RA_AllocARMRegister(&ptr);
        uint8_t buf2 = RA_AllocARMRegister(&ptr);
#ifndef __aarch64__
        uint8_t buf3 = RA_AllocARMRegister(&ptr);
        uint8_t buf4 = RA_AllocARMRegister(&ptr);
#endif
        uint8_t reg = RA_MapM68kRegister(&ptr, 8 + (opcode & 7));
        uint32_t mem = (BE16((*m68k_ptr)[1]) << 16) | BE16((*m68k_ptr)[2]);

        /* Align memory pointer */
        mem &= 0xfffffff0;
        *ptr++ = movw_immed_u16(aligned_mem, mem & 0xffff);
        if (mem & 0xffff0000)
            *ptr++ = movt_immed_u16(aligned_mem, mem >> 16);
#ifdef __aarch64__
        *ptr++ = bic_immed(aligned_reg, reg, 4, 0);
        if (opcode & 8) {
            EMIT_ldp64(&ptr, aligned_mem, buf1, buf2, 0);
            EMIT_stp64(&ptr, aligned_reg, buf1, buf2, 0);
        }
        else {
            EMIT_ldp64(&ptr, aligned_reg, buf1, buf2, 0);
            EMIT_stp64(&ptr, aligned_mem, buf1, buf2, 0);
        }
#else
        *ptr++ = _bic_val(aligned_reg, reg, 0x0f);
        {
            uint8_t from = (opcode & 8) ? aligned_mem : aligned_reg;
            uint8_t to   = (opcode & 8) ? aligned_reg : aligned_mem;
            *ptr++ = ldr_offset(from, buf1, 0);
            *ptr++ = ldr_offset(from, buf2, 4);
            *ptr++ = ldr_offset(from, buf3, 8);
            *ptr++ = ldr_offset(from, buf4, 12);
            *ptr++ = str_offset(to, buf1, 0);
            *ptr++ = str_offset(to, buf2, 4);
            *ptr++ = str_offset(to, buf3, 8);
            *ptr++ = str_offset(to, buf4, 12);
        }
#endif
        if (!(opcode & 0x10))
        {
            *ptr++ = add_immed(reg, reg, 16);
            RA_SetDirtyM68kRegister(&ptr, 8 + (opcode & 7));
        }

        RA_FreeARMRegister(&ptr, aligned_reg);
        RA_FreeARMRegister(&ptr, aligned_mem);
        RA_FreeARMRegister(&ptr, buf1);
        RA_FreeARMRegister(&ptr, buf2);
#ifndef __aarch64__
        RA_FreeARMRegister(&ptr, buf3);
        RA_FreeARMRegister(&ptr, buf4);
#endif
        (*m68k_ptr)+=3;
        *insn_consumed = 1;
        ptr = EMIT_AdvancePC(ptr, 6);
    }
#ifdef EMU68_OMMX
    else if (EnableOMMX && (opcode & 0xfc00) == 0xfc00)
    {
        return EMIT_OMMX(ptr, m68k_ptr, insn_consumed);
    }
#endif
    /* CINV */
    else if ((opcode & 0xff20) == 0xf400 && (opcode & 0x0018) != 0)
    {
        uint8_t tmp = 0xff;
        uint8_t tmp2 = 0xff;
        uint8_t tmp3 = 0xff;
        uint8_t tmp4 = 0xff;
        RA_FlushOMMXRegs(&ptr);     /* OMMX: this path blx-es a trampoline; d16-d31 are caller-saved */

        ptr = EMIT_FlushPC(ptr);

        /* Invalidating data cache? */
        if (opcode & 0x40) {
#ifndef __aarch64__
            (void)0;
#else

            /* Get the scope */
            switch (opcode & 0x18) {
                case 0x08:  /* Line */
                    tmp = RA_CopyFromM68kRegister(&ptr, 8 + (opcode & 7));
#ifdef __aarch64__
                    tmp2 = RA_AllocARMRegister(&ptr);
                    tmp3 = RA_AllocARMRegister(&ptr);
                    *ptr++ = mov_immed_u8(tmp3, 4);
                    *ptr++ = mrs(tmp2, 3, 3, 0, 0, 1); // Get CTR_EL0
                    *ptr++ = ubfx(tmp2, tmp2, 16, 4);
                    *ptr++ = lslv(tmp2, tmp3, tmp2);
                    *ptr++ = sub_immed(tmp2, tmp2, 1);
                    *ptr++ = dsb_sy();
                    *ptr++ = and_reg(tmp, tmp, tmp2, LSL, 0);
                    *ptr++ = dc_ivac(tmp);
                    *ptr++ = dsb_sy();
                    RA_FreeARMRegister(&ptr, tmp2);
                    RA_FreeARMRegister(&ptr, tmp3);
#else
                    *ptr++ = _bic_val(tmp, tmp, 0x1f);
                    *ptr++ = mcr(15, 0, tmp, 7, 6, 1); /* clean and invalidate data cache line */
                    *ptr++ = mov_immed_u8(tmp, 0);
                    *ptr++ = mcr(15, 0, tmp, 7, 10, 4); /* dsb */
#endif
                    RA_FreeARMRegister(&ptr, tmp);
                    break;
                case 0x10:  /* Page */
                    tmp = RA_CopyFromM68kRegister(&ptr, 8 + (opcode & 7));
                    tmp2 = RA_AllocARMRegister(&ptr);
#ifdef __aarch64__
                    tmp3 = RA_AllocARMRegister(&ptr);
                    tmp4 = RA_AllocARMRegister(&ptr);
                    *ptr++ = mrs(tmp3, 3, 3, 0, 0, 1); // Get CTR_EL0
                    *ptr++ = ubfx(tmp3, tmp3, 16, 4);
                    *ptr++ = mov_immed_u16(tmp2, 1024, 0);
                    *ptr++ = lsrv(tmp2, tmp2, tmp3);
                    *ptr++ = bic_immed(tmp, tmp, 12, 0);
                    *ptr++ = mov_immed_u8(tmp4, 4);
                    *ptr++ = lslv(tmp4, tmp4, tmp3);
                    *ptr++ = dc_ivac(tmp);
                    *ptr++ = add_reg(tmp, tmp, tmp4, LSL, 0);
                    *ptr++ = subs_immed(tmp2, tmp2, 1);
                    *ptr++ = b_cc(A64_CC_NE, -3);
                    *ptr++ = dsb_sy();
                    RA_FreeARMRegister(&ptr, tmp3);
                    RA_FreeARMRegister(&ptr, tmp4);
#else
                    *ptr++ = _bic_val(tmp, tmp, 0x0ff);
                    *ptr++ = _bic_val(tmp, tmp, 0xf00);
                    *ptr++ = mov_immed_u8(tmp2, 128);
                    *ptr++ = mcr(15, 0, tmp, 7, 6, 1); /* clean and invalidate data cache line */
                    *ptr++ = add_immed(tmp, tmp, 32);
                    *ptr++ = subs_immed(tmp2, tmp2, 1);
                    *ptr++ = b_cc(ARM_CC_NE, -3);
                    *ptr++ = mcr(15, 0, tmp2, 7, 10, 4); /* dsb */
#endif
                    RA_FreeARMRegister(&ptr, tmp);
                    RA_FreeARMRegister(&ptr, tmp2);
                    break;
                case 0x18:  /* All */
#ifdef __aarch64__
                    {
                        union {
                            uint64_t u64;
                            uint32_t u32[2];
                        } u;

                        u.u64 = (uintptr_t)invalidate_entire_dcache;

                        EMIT_stp64_preindex(&ptr, 31, 0, 30, -16);
                        *ptr++ = ldr64_pcrel(0, 4);
                        *ptr++ = blr(0);
                        EMIT_ldp64_postindex(&ptr, 31, 0, 30, 16);
                        *ptr++ = b(3);
                        *ptr++ = u.u32[0];
                        *ptr++ = u.u32[1];
                    }
#else
                    *ptr++ = push(0x0f | (1 << 12));
                    *ptr++ = ldr_offset(15, 12, 8);
                    *ptr++ = blx_cc_reg(ARM_CC_AL, 12);
                    *ptr++ = pop(0x0f | (1 << 12));
                    *ptr++ = b_cc(ARM_CC_AL, 2);
                    *ptr++ = BE32((uint32_t)invalidate_entire_dcache);
#endif
                    break;
            }
        
#endif
        }
        /* Invalidating instruction cache? */
        if (opcode & 0x80) {
            int8_t off = 0;
            ptr = EMIT_GetOffsetPC(ptr, &off);
#ifdef __aarch64__
            union {
                uint64_t u64;
                uint32_t u32[2];
            } u;
            u.u64 = (uintptr_t)trampoline_icache_invalidate;

            EMIT_stp64_preindex(&ptr, 31, 0, 1, -176);
            for (int i=2; i < 20; i+=2)
                EMIT_stp64(&ptr, 31, i, i + 1, i * 8);
            EMIT_stp64(&ptr, 31, 29, 30, 160);
            if ((opcode & 0x18) == 0x08 || (opcode & 0x18) == 0x10)
            {
                uint8_t tmp = RA_MapM68kRegister(&ptr, 8 + (opcode & 7));
                *ptr++ = mov_reg(0, tmp);
            }
            if (off >= 0)
                *ptr++ = add_immed(1, REG_PC, off);
            else
                *ptr++ = sub_immed(1, REG_PC, -off);

            *ptr++ = adr(2, 4*6);
            *ptr++ = ldr64_pcrel(3, 3);
            *ptr++ = br(3);
            *ptr++ = b(3);
            *ptr++ = u.u32[0];
            *ptr++ = u.u32[1];

            for (int i=2; i < 20; i+=2)
                EMIT_ldp64(&ptr, 31, i, i + 1, i * 8);
            EMIT_ldp64(&ptr, 31, 29, 30, 160);
            EMIT_ldp64_postindex(&ptr, 31, 0, 1, 176);
#else
            if ((opcode & 0x18) == 0x08 || (opcode & 0x18) == 0x10)
            {
                uint8_t tmp = RA_MapM68kRegister(&ptr, 8 + (opcode & 7));
                *ptr++ = push(0x0f | (1 << 12));
                if (tmp != 0)
                    *ptr++ = mov_reg(0, tmp);
            }
            else
            {
                *ptr++ = push(0x0f | (1 << 12));
            }
            if (off >= 0)
                *ptr++ = add_immed(1, REG_PC, off);
            else
                *ptr++ = sub_immed(1, REG_PC, -off);
            *ptr++ = add_immed(2, 15, 4);
            *ptr++ = ldr_offset(15, 12, 8);
            *ptr++ = blx_cc_reg(ARM_CC_AL, 12);
            *ptr++ = pop(0x0f | (1 << 12));
            *ptr++ = b_cc(ARM_CC_AL, 2);
            *ptr++ = BE32((uint32_t)trampoline_icache_invalidate);
#endif
        }

        (*m68k_ptr)++;
        *insn_consumed = 1;

        *ptr++ = add_immed(REG_PC, REG_PC, 2);

        /* Cache flushing is context synchronizing. Stop translating code here */
        *ptr++ = INSN_TO_LE(0xffffffff);
        *ptr++ = INSN_TO_LE(0xfffffff0);
    }
    /* CPUSH */
    else if ((opcode & 0xff20) == 0xf420 && (opcode & 0x0018) != 0)
    {
        uint8_t tmp = 0xff;
        uint8_t tmp2 = 0xff;
        uint8_t tmp3 = 0xff;
        uint8_t tmp4 = 0xff;
        RA_FlushOMMXRegs(&ptr);     /* OMMX: this path blx-es a trampoline; d16-d31 are caller-saved */

        ptr = EMIT_FlushPC(ptr);

        /* Flush data cache? */
        if (opcode & 0x40) {
#ifndef __aarch64__
            (void)0;
#else

            /* Get the scope */
            switch (opcode & 0x18) {
                case 0x08:  /* Line */
                    tmp = RA_CopyFromM68kRegister(&ptr, 8 + (opcode & 7));
#ifdef __aarch64__
                    tmp2 = RA_AllocARMRegister(&ptr);
                    tmp3 = RA_AllocARMRegister(&ptr);
                    *ptr++ = mov_immed_u8(tmp3, 4);
                    *ptr++ = mrs(tmp2, 3, 3, 0, 0, 1); // Get CTR_EL0
                    *ptr++ = ubfx(tmp2, tmp2, 16, 4);
                    *ptr++ = lslv(tmp2, tmp3, tmp2);
                    *ptr++ = sub_immed(tmp2, tmp2, 1);
                    *ptr++ = dsb_sy();
                    *ptr++ = and_reg(tmp, tmp, tmp2, LSL, 0);
                    *ptr++ = dc_civac(tmp);
                    *ptr++ = dsb_sy();
                    RA_FreeARMRegister(&ptr, tmp2);
                    RA_FreeARMRegister(&ptr, tmp3);
#else
                    *ptr++ = _bic_val(tmp, tmp, 0x1f);
                    *ptr++ = mcr(15, 0, tmp, 7, 14, 1); /* clean and invalidate data cache line */
                    *ptr++ = mov_immed_u8(tmp, 0);
                    *ptr++ = mcr(15, 0, tmp, 7, 10, 4); /* dsb */
#endif
                    RA_FreeARMRegister(&ptr, tmp);
                    break;
                case 0x10:  /* Page */
                    tmp = RA_CopyFromM68kRegister(&ptr, 8 + (opcode & 7));
                    tmp2 = RA_AllocARMRegister(&ptr);
#ifdef __aarch64__
                    tmp3 = RA_AllocARMRegister(&ptr);
                    tmp4 = RA_AllocARMRegister(&ptr);
                    *ptr++ = mrs(tmp3, 3, 3, 0, 0, 1); // Get CTR_EL0
                    *ptr++ = ubfx(tmp3, tmp3, 16, 4);
                    *ptr++ = mov_immed_u16(tmp2, 1024, 0);
                    *ptr++ = lsrv(tmp2, tmp2, tmp3);
                    *ptr++ = bic_immed(tmp, tmp, 12, 0);
                    *ptr++ = mov_immed_u8(tmp4, 4);
                    *ptr++ = lslv(tmp4, tmp4, tmp3);
                    *ptr++ = dc_civac(tmp);
                    *ptr++ = add_reg(tmp, tmp, tmp4, LSL, 0);
                    *ptr++ = subs_immed(tmp2, tmp2, 1);
                    *ptr++ = b_cc(A64_CC_NE, -3);
                    *ptr++ = dsb_sy();
                    RA_FreeARMRegister(&ptr, tmp3);
                    RA_FreeARMRegister(&ptr, tmp4);
#else
                    *ptr++ = _bic_val(tmp, tmp, 0x0ff);
                    *ptr++ = _bic_val(tmp, tmp, 0xf00);
                    *ptr++ = mov_immed_u8(tmp2, 128);
                    *ptr++ = mcr(15, 0, tmp, 7, 14, 1); /* clean and invalidate data cache line */
                    *ptr++ = add_immed(tmp, tmp, 32);
                    *ptr++ = subs_immed(tmp2, tmp2, 1);
                    *ptr++ = b_cc(ARM_CC_NE, -3);
                    *ptr++ = mcr(15, 0, tmp2, 7, 10, 4); /* dsb */
#endif
                    RA_FreeARMRegister(&ptr, tmp);
                    RA_FreeARMRegister(&ptr, tmp2);
                    break;
                case 0x18:  /* All */
#ifdef __aarch64__
                    {
                        union {
                            uint64_t u64;
                            uint32_t u32[2];
                        } u;

                        u.u64 = (uintptr_t)clear_entire_dcache;

                        EMIT_stp64_preindex(&ptr, 31, 0, 30, -16);
                        *ptr++ = ldr64_pcrel(0, 4);
                        *ptr++ = blr(0);
                        EMIT_ldp64_postindex(&ptr, 31, 0, 30, 16);
                        *ptr++ = b(3);
                        *ptr++ = u.u32[0];
                        *ptr++ = u.u32[1];
                    }
#else
                    *ptr++ = push(0x0f | (1 << 12));
                    *ptr++ = ldr_offset(15, 12, 8);
                    *ptr++ = blx_cc_reg(ARM_CC_AL, 12);
                    *ptr++ = pop(0x0f | (1 << 12));
                    *ptr++ = b_cc(ARM_CC_AL, 2);
                    *ptr++ = BE32((uint32_t)clear_entire_dcache);
#endif
                    break;
            }
        
#endif
        }
        /* Invalidating instruction cache? */
        if (opcode & 0x80) {
            int8_t off = 0;
            ptr = EMIT_GetOffsetPC(ptr, &off);
#ifdef __aarch64__
            union {
                uint64_t u64;
                uint32_t u32[2];
            } u;
            u.u64 = (uintptr_t)trampoline_icache_invalidate;

            EMIT_stp64_preindex(&ptr, 31, 0, 1, -176);
            for (int i=2; i < 20; i+=2)
                EMIT_stp64(&ptr, 31, i, i + 1, i * 8);
            EMIT_stp64(&ptr, 31, 29, 30, 160);
            if ((opcode & 0x18) == 0x08 || (opcode & 0x18) == 0x10)
            {
                uint8_t tmp = RA_MapM68kRegister(&ptr, 8 + (opcode & 7));
                *ptr++ = mov_reg(0, tmp);
            }
            if (off >= 0)
                *ptr++ = add_immed(1, REG_PC, off);
            else
                *ptr++ = sub_immed(1, REG_PC, -off);

            *ptr++ = adr(2, 4*6);
            *ptr++ = ldr64_pcrel(3, 3);
            *ptr++ = br(3);
            *ptr++ = b(3);
            *ptr++ = u.u32[0];
            *ptr++ = u.u32[1];

            for (int i=2; i < 20; i+=2)
                EMIT_ldp64(&ptr, 31, i, i + 1, i * 8);
            EMIT_ldp64(&ptr, 31, 29, 30, 160);
            EMIT_ldp64_postindex(&ptr, 31, 0, 1, 176);
#else
            if ((opcode & 0x18) == 0x08 || (opcode & 0x18) == 0x10)
            {
                uint8_t tmp = RA_MapM68kRegister(&ptr, 8 + (opcode & 7));
                *ptr++ = push(0x0f | (1 << 12));
                if (tmp != 0)
                    *ptr++ = mov_reg(0, tmp);
            }
            else
            {
                *ptr++ = push(0x0f | (1 << 12));
            }
            if (off >= 0)
                *ptr++ = add_immed(1, REG_PC, off);
            else
                *ptr++ = sub_immed(1, REG_PC, -off);
            *ptr++ = add_immed(2, 15, 4);
            *ptr++ = ldr_offset(15, 12, 8);
            *ptr++ = blx_cc_reg(ARM_CC_AL, 12);
            *ptr++ = pop(0x0f | (1 << 12));
            *ptr++ = b_cc(ARM_CC_AL, 2);
            *ptr++ = BE32((uint32_t)trampoline_icache_invalidate);
#endif
        }

        (*m68k_ptr)++;
        *insn_consumed = 1;

        *ptr++ = add_immed(REG_PC, REG_PC, 2);

        /* Cache is context synchronizing. Break up here! */
        *ptr++ = INSN_TO_LE(0xffffffff);
        *ptr++ = INSN_TO_LE(0xfffffff0);
    }
#if defined(EMU68_CPU040) && !defined(__aarch64__)
    else if (CPUMODEL_HAS_040_REGS(CpuModel) &&
             ((opcode & 0xffe0) == 0xf500 || (opcode & 0xffd8) == 0xf548))
    {
        if ((opcode & 0xffd8) == 0xf548)
        {
            uint8_t ctx = RA_GetCTX(&ptr);
            uint8_t tmp = RA_AllocARMRegister(&ptr);
            *ptr++ = mov_immed_u8(tmp, 0);
            *ptr++ = str_offset(ctx, tmp, __builtin_offsetof(struct M68KState, MMUSR));
            RA_FreeARMRegister(&ptr, tmp);
        }
        ptr = EMIT_AdvancePC(ptr, 2);
        (*m68k_ptr)++;
        *insn_consumed = 1;
    }
    else if (CPUMODEL_IS_060(CpuModel) &&
             ((opcode & 0xfff8) == 0xf588 || (opcode & 0xfff8) == 0xf5c8))
    {
        ptr = EMIT_AdvancePC(ptr, 2);
        (*m68k_ptr)++;
        *insn_consumed = 1;
    }
    else if (CPUMODEL_IS_060(CpuModel) && opcode == 0xf800)
    {
        uint16_t newsr = BE16((*m68k_ptr)[2]);
        if (opcode2 != 0x01c0)
        {
            ptr = EMIT_FlushPC(ptr);
            ptr = EMIT_Exception(ptr, VECTOR_LINE_F, 0);
            *ptr++ = INSN_TO_LE(0xffffffff);
            (*m68k_ptr) += 3;
            *insn_consumed = 1;
        }
        else
        {
            uint8_t cc  = RA_GetCC(&ptr);
            uint8_t ctx = RA_GetCTX(&ptr);
            uint8_t tmp = RA_AllocARMRegister(&ptr);
            uint32_t *lpstop_pop_loc = NULL;

            ptr = EMIT_FlushPC(ptr);
            {
                struct RA_Snapshot ra_snap;
                RA_SaveState(&ra_snap);
                *ptr++ = tst_immed(cc, 1, 31 & (32 - SRB_S));    /* supervisor? */
                uint32_t *lp_skip = ptr;
                *ptr++ = b_cc(ARM_CC_NE, 2);         /* placeholder; back-patched */
                ptr = EMIT_Exception(ptr, VECTOR_PRIVILEGE_VIOLATION, 0);
                ptr = EMIT_ExceptionUnitExit(ptr, &lpstop_pop_loc);
                *lp_skip = b_cc(ARM_CC_NE, (int32_t)(ptr - lp_skip));
                RA_RestoreState(&ra_snap);
            }
            if (!(newsr & SR_S))
            {
                /* Supervisor, but the immediate would drop out of it: vector 8 unconditionally. */
                ptr = EMIT_Exception(ptr, VECTOR_PRIVILEGE_VIOLATION, 0);
                *ptr++ = INSN_TO_LE(0xffffffff);
            }
            else
            {
                *ptr++ = movw_immed_u16(tmp, newsr & 0xf71f);
                *ptr++ = str_offset(ctx, tmp, __builtin_offsetof(struct M68KState, MOVE2SR_VAL));
                *ptr++ = mov_immed_u8(tmp, 1);
                *ptr++ = str_offset(ctx, tmp, __builtin_offsetof(struct M68KState, MOVE2SR_REQ));
                *ptr++ = str_offset(ctx, tmp, __builtin_offsetof(struct M68KState, STOP_REQ));
                ptr = EMIT_AdvancePC(ptr, 6);
                *ptr++ = 0;
                *ptr++ = 0;                       /* branch target (unused) */
                *ptr++ = INSN_TO_LE(0xfffffffe);  /* block epilogue: flush regs/SR/PC */
                *ptr++ = INSN_TO_LE(0xffffffff);  /* hard block end */
            }
            RA_FreeARMRegister(&ptr, tmp);
            ptr = EMIT_ExceptionUnitExitTail(ptr, lpstop_pop_loc);
            (*m68k_ptr) += 3;
            *insn_consumed = 1;
        }
    }
#endif
    else
    {
        ptr = EMIT_FlushPC(ptr);
        ptr = EMIT_InjectDebugString(ptr, "[JIT] opcode %04x at %08x not implemented\n", opcode, *m68k_ptr - 1);
        ptr = EMIT_Exception(ptr, VECTOR_LINE_F, 0);
        *ptr++ = INSN_TO_LE(0xffffffff);
    }

    return ptr;
}
