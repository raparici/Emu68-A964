/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: GPL-3.0-or-later
*/

/* ZZ9000: scalar row primitives. */

#include "gfx_rows.h"

#if GFX_ROWS_NEON_ACTIVE

#define ZZROW_LINKAGE
#define ZZROW_NAME(n) n##_scalar
#include "gfx_rows_scalar.h"
#undef ZZROW_LINKAGE
#undef ZZROW_NAME

#else

#endif
