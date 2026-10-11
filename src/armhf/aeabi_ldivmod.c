/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* The 64-bit EABI divide helpers for the big-endian ARMv7 firmware (no hardware divide on the Cortex-A9). */

#include <stdint.h>
#include "support.h"

uint64_t emu68_udivmod64(uint64_t n, uint64_t d, uint64_t *rem)
{
    if (d == 0) {                           /* undefined in C: libgcc's values, no trap */
        *rem = 0;
        return n ? ~(uint64_t)0 : 0;
    }
    struct Result64 res = uldiv(n, d);
    *rem = res.r;
    return res.q;
}

int64_t emu68_sdivmod64(int64_t n, int64_t d, int64_t *rem)
{
    if (d == 0) {                           /* undefined in C: libgcc's values, no trap */
        *rem = 0;
        return n > 0 ? INT64_MAX : (n < 0 ? INT64_MIN : 0);
    }
    /* magnitudes as UNSIGNED values: 0 - (uint64_t)INT64_MIN is 2^63, no overflow anywhere */
    const uint64_t un = n < 0 ? 0 - (uint64_t)n : (uint64_t)n;
    const uint64_t ud = d < 0 ? 0 - (uint64_t)d : (uint64_t)d;
    struct Result64 res = uldiv(un, ud);
    /* C11 6.5.5: the quotient truncates toward zero, the remainder takes the dividend's sign */
    const uint64_t q = ((n < 0) != (d < 0)) ? 0 - res.q : res.q;
    const uint64_t r = (n < 0) ? 0 - res.r : res.r;
    *rem = (int64_t)r;
    return (int64_t)q;                      /* INT64_MIN / -1 (undefined in C): INT64_MIN, as libgcc */
}

#if defined(__arm__)
#if defined(__thumb__)
#error "aeabi_ldivmod.c: the shims below are A32 code -- build this file in ARM state"
#endif

asm(
"       .pushsection .text                         \n"
"       .align 2                                   \n"
"       .globl __aeabi_uldivmod                    \n"
"       .type __aeabi_uldivmod,%function           \n"
"__aeabi_uldivmod:                                 \n"
"       push    {r4, lr}                           \n"
"       sub     sp, sp, #16                        \n"
"       add     r4, sp, #8                         \n"
"       str     r4, [sp]                           \n"   /* 3rd argument: &remainder              */
"       bl      emu68_udivmod64                    \n"   /* r0-r3 untouched: n and d as received  */
"       ldr     r2, [sp, #8]                       \n"   /* remainder, the AAPCS pair order: the  */
"       ldr     r3, [sp, #12]                      \n"   /* lower address into the lower register */
"       add     sp, sp, #16                        \n"
"       pop     {r4, pc}                           \n"
"       .size __aeabi_uldivmod, .-__aeabi_uldivmod \n"

"       .align 2                                   \n"
"       .globl __aeabi_ldivmod                     \n"
"       .type __aeabi_ldivmod,%function            \n"
"__aeabi_ldivmod:                                  \n"
"       push    {r4, lr}                           \n"
"       sub     sp, sp, #16                        \n"
"       add     r4, sp, #8                         \n"
"       str     r4, [sp]                           \n"
"       bl      emu68_sdivmod64                    \n"
"       ldr     r2, [sp, #8]                       \n"
"       ldr     r3, [sp, #12]                      \n"
"       add     sp, sp, #16                        \n"
"       pop     {r4, pc}                           \n"
"       .size __aeabi_ldivmod, .-__aeabi_ldivmod   \n"
"       .popsection                                \n"
);
#endif /* __arm__ */
