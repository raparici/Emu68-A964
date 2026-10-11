/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* A64 condition codes and names the m68k handlers use, mapped onto their A32 equivalents. */

#ifndef _A64_COMPAT_H
#define _A64_COMPAT_H

#include "ARM.h"   // ARM_CC_*

#define A64_CC_EQ ARM_CC_EQ
#define A64_CC_NE ARM_CC_NE
#define A64_CC_CS ARM_CC_CS
#define A64_CC_CC ARM_CC_CC
#define A64_CC_MI ARM_CC_MI
#define A64_CC_PL ARM_CC_PL
#define A64_CC_VS ARM_CC_VS
#define A64_CC_VC ARM_CC_VC
#define A64_CC_HI ARM_CC_HI
#define A64_CC_LS ARM_CC_LS
#define A64_CC_GE ARM_CC_GE
#define A64_CC_LT ARM_CC_LT
#define A64_CC_GT ARM_CC_GT
#define A64_CC_LE ARM_CC_LE
#define A64_CC_AL ARM_CC_AL
#define A64_CC_NV 0x0f

#endif // _A64_COMPAT_H
