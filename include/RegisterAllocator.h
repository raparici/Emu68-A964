/*
    Copyright © 2019 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#ifndef _REGISTER_ALLOCATOR_H
#define _REGISTER_ALLOCATOR_H

#include <stdint.h>

void RA_TouchM68kRegister(uint32_t **arm_stream, uint8_t m68k_reg);
void RA_SetDirtyM68kRegister(uint32_t **arm_stream, uint8_t m68k_reg);
void RA_InsertM68kRegister(uint32_t **arm_stream, uint8_t m68k_reg);
void RA_RemoveM68kRegister(uint32_t **arm_stream, uint8_t m68k_reg);
void RA_DiscardM68kRegister(uint32_t **arm_stream, uint8_t m68k_reg);
void RA_FlushM68kRegs(uint32_t **arm_stream);
void RA_StoreDirtyM68kRegs(uint32_t **arm_stream);

uint16_t RA_GetChangedMask();
void RA_ClearChangedMask();

uint8_t RA_AllocARMRegister(uint32_t **arm_stream);
void RA_FreeARMRegister(uint32_t **arm_stream, uint8_t arm_reg);

uint8_t RA_GetMappedARMRegister(uint8_t m68k_reg);
uint8_t RA_IsARMRegisterMapped(uint8_t arm_reg);
void RA_AssignM68kRegister(uint32_t **arm_stream, uint8_t m68k_reg, uint8_t arm_reg);
void RA_ResetM68kAllocator(void);
uint8_t RA_MapM68kRegister(uint32_t **arm_stream, uint8_t m68k_reg);
uint8_t RA_MapM68kRegisterForWrite(uint32_t **arm_stream, uint8_t m68k_reg);
void RA_UnmapM68kRegister(uint32_t **arm_stream, uint8_t m68k_reg);
int RA_IsM68kRegister(uint8_t m68k_reg);
uint8_t RA_CopyFromM68kRegister(uint32_t **arm_stream, uint8_t m68k_reg);
uint16_t RA_GetTempAllocMask();

void RA_ResetFPUAllocator();
uint8_t RA_AllocFPURegister(uint32_t **arm_stream);
uint8_t RA_AllocFPURegisterPair(uint32_t **arm_stream);   /* OMMX: even-aligned d2:d3/d4:d5/d6:d7 */
void RA_FreeFPURegister(uint32_t **arm_stream, uint8_t arm_reg);
uint8_t RA_MapFPURegister(uint32_t **arm_stream, uint8_t fpu_reg);
uint8_t RA_MapFPURegisterForWrite(uint32_t **arm_stream, uint8_t fpu_reg);
void RA_SetDirtyFPURegister(uint32_t **arm_stream, uint8_t fpu_reg);
void RA_FlushFPURegs(uint32_t **arm_stream);
void RA_StoreDirtyFPURegs(uint32_t **arm_stream);

uint8_t RA_MapOMMXRegister(uint32_t **arm_stream, uint8_t e_reg);
uint8_t RA_MapOMMXRegisterForWrite(uint32_t **arm_stream, uint8_t e_reg);
void RA_SetDirtyOMMXRegister(uint32_t **arm_stream, uint8_t e_reg);
void RA_StoreDirtyOMMXRegs(uint32_t **arm_stream);
void RA_FlushOMMXRegs(uint32_t **arm_stream);
void RA_ResetOMMXAllocator(void);
int  RA_OMMXBankLive(void);

uint8_t RA_TryCTX(uint32_t **ptr);
uint8_t RA_GetCTX(uint32_t **ptr);
void RA_FlushCTX(uint32_t **ptr);
int RA_IsCCLoaded();
int RA_IsCCModified();
uint8_t RA_GetCC(uint32_t **ptr);
uint8_t RA_ModifyCC(uint32_t **ptr);
void RA_FlushCC(uint32_t **ptr);
void RA_StoreCC(uint32_t **ptr);
uint8_t RA_GetFPCR(uint32_t **ptr);
uint8_t RA_ModifyFPCR(uint32_t **ptr);
void RA_FlushFPCR(uint32_t **ptr);
void RA_StoreFPCR(uint32_t **ptr);
uint8_t RA_GetFPSR(uint32_t **ptr);
uint8_t RA_ModifyFPSR(uint32_t **ptr);
void RA_FlushFPSR(uint32_t **ptr);
void RA_StoreFPSR(uint32_t **ptr);

#ifndef __aarch64__   /* armhf only: the aarch64 RA does not define these */
struct RA_Snapshot {
    uint8_t  arm[16];
    uint8_t  dirty[16];
    int8_t   lru[8];
    uint16_t pool;
    uint8_t  got_cc, mod_cc;
    uint8_t  reg_fpcr, mod_fpcr, reg_fpsr, mod_fpsr;
    uint8_t  fpu_alloc;
    uint8_t  fpu_state[8];
    uint8_t  ommx_state[16];    /* OMMX fast-bank cache state (EMU68_OMMX) */
};
uint8_t RA_AllocARMRegister_NoFP(uint32_t **arm_stream);
uint8_t RA_MapM68kRegister_NoFP(uint32_t **arm_stream, uint8_t m68k_reg);
void RA_SaveState(struct RA_Snapshot *s);
void RA_RestoreState(const struct RA_Snapshot *s);
int RA_AllStateFlushed(void);
#endif

#endif /* _REGISTER_ALLOCATOR_H */
