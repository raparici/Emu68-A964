/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: firmware definitions forced into the GL core's compilation. */

#ifndef A3D_MGLC_FW_H
#define A3D_MGLC_FW_H
#include <string.h>
#define memcpy(d, s, n)  __builtin_memcpy((d), (s), (n))
#define memset(p, v, n)  __builtin_memset((p), (v), (n))
#define memmove(d, s, n) __builtin_memmove((d), (s), (n))
#endif
