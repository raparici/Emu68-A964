/*
    Copyright © 2019 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#ifndef _CONFIG_H
#define _CONFIG_H

#define ARM_FEATURE_HAS_DIV     1
#define ARM_FEATURE_HAS_BITFLD  1
#define ARM_FEATURE_HAS_BITCNT  1
#define ARM_FEATURE_HAS_SWP     1
#define ARM_FEATURE_HAS_VDIV    1
#define ARM_FEATURE_HAS_SQRT    1

#ifndef SET_FEATURES_AT_RUNTIME
#define SET_FEATURES_AT_RUNTIME 1
#endif

#ifndef SET_OPTIONS_AT_RUNTIME
#define SET_OPTIONS_AT_RUNTIME  1
#endif

#define EMU68_ARM_CACHE_SIZE    (4*1024*1024)
#ifndef EMU68_M68K_INSN_DEPTH
#define EMU68_M68K_INSN_DEPTH   256
#endif
#ifndef EMU68_CCR_BREAK_AT_UNIT_END
#define EMU68_CCR_BREAK_AT_UNIT_END 1
#endif
#ifndef EMU68_CCR_SCAN_DEPTH
#define EMU68_CCR_SCAN_DEPTH    20
#endif
#ifndef EMU68_SLOWDOWN_ADDR_MAX
#define EMU68_SLOWDOWN_ADDR_MAX   0x00200000u
#endif
#ifndef EMU68_CHIP_SLOWDOWN_DEFAULT
#define EMU68_CHIP_SLOWDOWN_DEFAULT 0
#endif
#ifndef EMU68_DBF_SLOWDOWN_DEFAULT
#define EMU68_DBF_SLOWDOWN_DEFAULT  0
#endif
#define CPUMODEL_68EC020        0
#define CPUMODEL_68040          1
#define CPUMODEL_68060          2       /* "68060+" - 060 presentation, nothing deprecated */
#define CPUMODEL_IS_020(m)      ((m) == CPUMODEL_68EC020)
#define CPUMODEL_IS_040(m)      ((m) == CPUMODEL_68040)
#define CPUMODEL_IS_060(m)      ((m) == CPUMODEL_68060)
#define CPUMODEL_HAS_040_REGS(m) ((m) != CPUMODEL_68EC020)
#ifndef EMU68_060_ANSWER_PCR
#define EMU68_060_ANSWER_PCR    1
#endif
#define CPUMODEL_NAME(m)        (CPUMODEL_IS_060(m) ? "68060+" \
                                 : (CPUMODEL_IS_040(m) ? "68040" : "68EC020"))
#ifndef EMU68_CPU040_DEFAULT
#define EMU68_CPU040_DEFAULT    CPUMODEL_68EC020
#endif

#ifndef EMU68_CPU_HZ
#define EMU68_CPU_HZ            800000000u
#endif
#define EMU68_HOST_BIG_ENDIAN   1
#define EMU68_HAS_SETEND        1
#define EMU68_DEF_BRANCH_TAKEN  0
#define EMU68_DEF_BRANCH_AUTO   1
#define EMU68_DEF_BRANCH_AUTO_RANGE 16
#define EMU68_INSN_COUNTER      1
#ifndef EMU68_MAX_LOOP_COUNT
#define EMU68_MAX_LOOP_COUNT    8
#endif
#ifndef EMU68_BRANCH_INLINE_DISTANCE
#define EMU68_BRANCH_INLINE_DISTANCE 8191
#endif
#define EMU68_INLINE_MIN_ADDRESS 0x01000000
#define EMU68_INLINE_ROM_LO 0x00f80000u
#define EMU68_INLINE_ROM_HI 0x00ffffffu
#define EMU68_INLINE_ALLOWED(a) \
    (((uintptr_t)(a) >= EMU68_INLINE_MIN_ADDRESS) || \
     (((uintptr_t)(a) >= EMU68_INLINE_ROM_LO) && ((uintptr_t)(a) <= EMU68_INLINE_ROM_HI)))
#define EMU68_USE_RETURN_STACK  1
#define EMU68_WEAK_CFLUSH       1
#define EMU68_WEAK_CFLUSH_LIMIT 0x7FFFFFFF

#ifndef VERSION_STRING_DATE
#define VERSION_STRING_DATE ""
#endif

#define KERNEL_SYS_PAGES        16
#define KERNEL_JIT_PAGES        32
#define KERNEL_RSRVD_PAGES      ((KERNEL_JIT_PAGES) + (KERNEL_SYS_PAGES))

#define EMU68_LOG_FETCHES       0
#define EMU68_LOG_USES          0

#endif /* _CONFIG_H */
