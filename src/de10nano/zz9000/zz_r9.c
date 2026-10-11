/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* ZZ9000 and BigMig 3Dx2: settings of the GPU's band scheduling. */

#include <stdint.h>

#ifdef EMU68_A3D

#if defined(__arm__) && !defined(__APPLE__)
#define ZZ_R9_TEXT __attribute__((section(".r9_tail_text"), noinline))
#define ZZ_R9_BSS  __attribute__((section(".bss.~r9")))
#else
#define ZZ_R9_TEXT __attribute__((noinline))
#define ZZ_R9_BSS
#endif

#endif /* EMU68_A3D */
