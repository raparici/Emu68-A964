/*
    Copyright © 2019 Michal Schulz <michal.schulz@gmx.de>
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* The A64 encoder interface of include/A64.h, implemented in A32 (searched first on the ARMv7 build). */

#ifndef _A64_H
#define _A64_H

#include "ARM.h"                        // forward-ported A32 encoders + INSN_TO_LE + REG_* + fwd-alu + A64_compat
#include "../shims/Emu68_ARM_shims.h"   // the A64-only single-word names (mov_immed_u16, *64, ...)
#include "../emit/Emu68_ARM_emit.h"     // the multi-word EMIT_* helpers (bfxil/csel/cbnz/...)
#include "gaps.h"                       // forward-ported A64.h encoders that were missing (sees ARM.h INSN_TO_LE/REG_*/EMIT_*)
#include "fp-gaps.h"
#include "gaps64.h"

#endif // _A64_H
