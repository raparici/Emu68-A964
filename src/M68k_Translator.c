#include "Emu68_ARM_emit.h"
/*
    Copyright © 2019 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#define _GNU_SOURCE 1

#include "support.h"
#include "EmuFeatures.h"
#include "M68k.h"
#include "RegisterAllocator.h"
#include "lists.h"
#include "tlsf.h"
#include "config.h"
#include "DuffCopy.h"
#include "disasm.h"
#include "jit_levers.h"
#include "OMMX.h"

#ifdef EMU68_CPU040
int CpuModel = EMU68_CPU040_DEFAULT;   /* build default; FEAT_DESC overrides it on the board */
#endif

#if SET_FEATURES_AT_RUNTIME
features_t Features;
#endif

#if SET_OPTIONS_AT_RUNTIME
options_t Options = {
    EMU68_M68K_INSN_DEPTH,
    1,
    EMU68_CCR_SCAN_DEPTH,
    EMU68_BRANCH_INLINE_DISTANCE,
    EMU68_MAX_LOOP_COUNT,
};
#endif

int disasm = 0;
int debug = 0;
uint32_t g_emit_dump_lo = 0, g_emit_dump_hi = 0;
const int debug_cnt = 0;

struct List *ICache;
struct List LRU;
static uint32_t *temporary_arm_code;

static uintptr_t temporary_arm_code_nominal;   /* what the emitter may use */
#define EMU68_XLAT_WORST_UNIT  2048
#define TEMP_CODE_GUARD        4096            /* dead space so a violation reports, not corrupts */
uintptr_t g_xlat_max_len;      /* longest unit emitted so far, in BYTES */
uintptr_t g_xlat_budget;
uint32_t  g_xlat_over_count;   /* units that exceeded it - MUST stay 0 */
uintptr_t g_xlat_max_insn;     /* longest SINGLE m68k instruction, in BYTES - vs the 2048 reserve */
uint16_t  g_xlat_max_insn_op;  /* ...and which opcode word it was */
uintptr_t g_xlat_reserve = EMU68_XLAT_WORST_UNIT;
uint32_t  g_xlat_over_worst;   /* worst overshoot, in bytes */
static struct M68KLocalState *local_state;

static inline void jit_icache_all(void) { }

int32_t _pc_rel = 0;


#ifndef __aarch64__
uint16_t *M68K_ActiveInsn = NULL;

#define AERR_POP_FIXUP_MAX  16
#if defined(__aarch64__)
static uint32_t *aerr_pop_fixup[AERR_POP_FIXUP_MAX];
#else
static uint32_t *aerr_pop_fixup[AERR_POP_FIXUP_MAX] __attribute__((used));   /* read through g_r10c_statics */
#endif
static unsigned  aerr_pop_fixup_cnt = 0;

void M68K_RegisterPopFixup(uint32_t *loc)
{
    if (loc == NULL)
        return;
    if (aerr_pop_fixup_cnt >= AERR_POP_FIXUP_MAX) {
        kprintf("[JIT] address-error pop fix-up list overflow\n");
        return;
    }
    aerr_pop_fixup[aerr_pop_fixup_cnt++] = loc;
}
#endif

uint32_t *EMIT_GetOffsetPC(uint32_t *ptr, int8_t *offset)
{
    // Calculate new PC relative offset
    int new_offset = _pc_rel + *offset;

    // If overflow would occur then compute PC and get new offset
    if (new_offset > 127 || new_offset < -127)
    {
        if (_pc_rel > 0)
            *ptr++ = add_immed(REG_PC, REG_PC, _pc_rel);
        else
            *ptr++ = sub_immed(REG_PC, REG_PC, -_pc_rel);

        _pc_rel = 0;
        new_offset = *offset;
    }

    *offset = new_offset;

    return ptr;
}

uint32_t *EMIT_AdvancePC(uint32_t *ptr, uint8_t offset)
{
//if (debug)    kprintf("Emit_AdvancePC(pc_rel=%d, off=%d)\n", _pc_rel, (int)offset);
    // Calculate new PC relative offset
    _pc_rel += (int)offset;

    // If overflow would occur then compute PC and get new offset
    if (_pc_rel > 120 || _pc_rel < -120)
    {
        if (_pc_rel > 0)
            *ptr++ = add_immed(REG_PC, REG_PC, _pc_rel);
        else
            *ptr++ = sub_immed(REG_PC, REG_PC, -_pc_rel);

        _pc_rel = 0;
    }

    return ptr;
}

uint32_t *EMIT_FlushPC(uint32_t *ptr)
{
    if (_pc_rel > 0)
            *ptr++ = add_immed(REG_PC, REG_PC, _pc_rel);
    else if (_pc_rel < 0)
            *ptr++ = sub_immed(REG_PC, REG_PC, -_pc_rel);

    _pc_rel = 0;

    return ptr;
}

uint32_t *EMIT_ResetOffsetPC(uint32_t *ptr)
{
    _pc_rel = 0;
    return ptr;
}

uint32_t *EMIT_lineA(uint32_t *arm_ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed)
{
    uint16_t opcode = BE16((*m68k_ptr)[0]);
    (*m68k_ptr)++;

    (*insn_consumed)++;

    arm_ptr = EMIT_FlushPC(arm_ptr);
    if (debug)
        arm_ptr = EMIT_InjectDebugString(arm_ptr, "[JIT] LINE A exception (opcode %04x) at %08x not implemented\n", opcode, *m68k_ptr - 1);
    arm_ptr = EMIT_Exception(arm_ptr, VECTOR_LINE_A, 0);
    *arm_ptr++ = INSN_TO_LE(0xffffffff);

    return arm_ptr;
}

static uint32_t * (*line_array[16])(uint32_t *arm_ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed) = {
    EMIT_line0,
    EMIT_move,
    EMIT_move,
    EMIT_move,
    EMIT_line4,
    EMIT_line5,
    EMIT_line6,
    EMIT_moveq,
    EMIT_line8,
    EMIT_line9,
    EMIT_lineA,
    EMIT_lineB,
    EMIT_lineC,
    EMIT_lineD,
    EMIT_lineE,
    EMIT_lineF
};

int ChipSlowdown = EMU68_CHIP_SLOWDOWN_DEFAULT;
int DbfSlowdown  = EMU68_DBF_SLOWDOWN_DEFAULT;
uint32_t insn_seen = 0;

static inline uint32_t *EmitINSN(uint32_t *arm_ptr, uint16_t **m68k_ptr, uint16_t *insn_consumed)
{
    uint32_t *ptr = arm_ptr;
    uint16_t opcode = BE16((*m68k_ptr)[0]);
    uint8_t group = opcode >> 12;

#ifdef __aarch64__
    if (debug > 2)
    {
        *ptr++ = hint(0);
        *ptr++ = movw_immed_u16(31, opcode);
        *ptr++ = movk_immed_u16(31, ((uintptr_t)*(m68k_ptr)) >> 16, 1);
        *ptr++ = movk_immed_u16(31, ((uintptr_t)*(m68k_ptr)), 0);
    }
    if (debug > 1)
        *ptr++ = hint(1);
    if (debug_cnt & 1)
    {
        uint8_t reg = RA_AllocARMRegister(&ptr);
        *ptr++ = mov_immed_u16(reg, 1, 0);
        *ptr++ = msr(reg, 3, 3, 9, 12, 4);
        RA_FreeARMRegister(&ptr, reg);
    }
#endif

    if (ChipSlowdown && (uintptr_t)*m68k_ptr < EMU68_SLOWDOWN_ADDR_MAX)
    {
        if ((insn_seen % (uint32_t)ChipSlowdown) == 0)
        {
            uint32_t *slow_start = ptr;
            uint8_t reg = RA_AllocARMRegister(&ptr);
            int8_t off = 0;
            ptr = EMIT_GetOffsetPC(ptr, &off);
            *ptr++ = ldrh_offset(REG_PC, reg, off);      /* reg := [REG_PC + off]  (BASE, TARGET) */
            RA_FreeARMRegister(&ptr, reg);
            (void)slow_start;
        }
    }
    insn_seen++;

    ptr = line_array[group](ptr, m68k_ptr, insn_consumed);

    return ptr;
}

void __clear_cache(void *begin, void *end)
{
    arm_flush_cache((uintptr_t)begin, (uintptr_t)end - (uintptr_t)begin);
    arm_icache_invalidate((uintptr_t)begin, (uintptr_t)end - (uintptr_t)begin);
}

#define RTSTACK_SIZE    32
uint16_t *ReturnStack[RTSTACK_SIZE];
uint16_t ReturnStackDepth = 0;

void M68K_PushReturnAddress(uint16_t *ret_addr)
{
    if (ReturnStackDepth >= RTSTACK_SIZE) {
        for (int i=1; i < RTSTACK_SIZE; i++) {
            ReturnStack[i-1] = ReturnStack[i];
        }
        ReturnStackDepth--;
    }

    ReturnStack[ReturnStackDepth++] = ret_addr;
}

uint16_t *M68K_PopReturnAddress(uint8_t *success)
{
    uint16_t *ptr;

    if (EMU68_USE_RETURN_STACK && ReturnStackDepth > 0)
    {
        ptr = ReturnStack[--ReturnStackDepth];

        if (success)
            *success = 1;
    }
    else
    {
        ptr = (uint16_t *)0xffffffff;
        if (success)
            *success = 0;
    }

    return ptr;
}

void M68K_ResetReturnStack()
{
    ReturnStackDepth = 0;
}

uint16_t *m68k_high;
uint16_t *m68k_low;
uint32_t insn_count;
int g_trace_step = 0;
#if defined(EMU68_INNER_LOOP) && !defined(__aarch64__)
uint16_t *g_xlat_unit_start = 0;
int       g_xlat_backedge_ok = 0;
#endif
int JitStatsOff = 0;
uint32_t prologue_size = 0;
uint32_t epilogue_size = 0;
uint32_t conditionals_count = 0;

extern struct M68KState *__m68k_state;
void M68K_PrintContext(void *);



#if defined(EMU68_INNER_LOOP) && !defined(__aarch64__)
#if defined(__aarch64__)
static uint32_t *EMIT_LoopBackEdge(uint32_t *end, uint32_t *loop_entry, uint32_t n_insn, uint32_t copies)
{
#if EMU68_INSN_COUNTER
    if (!JitStatsOff)
    {
        uint32_t left = n_insn;
        *end++ = ldr_offset(REG_CTX, 9, __builtin_offsetof(struct M68KState, INSN_COUNT));   /* r9 := [fp+INSN_COUNT] */
        while (left) {
            uint32_t c = left > 255 ? 255 : left;
            *end++ = add_immed(9, 9, c);
            left -= c;
        }
        *end++ = str_offset(REG_CTX, 9, __builtin_offsetof(struct M68KState, INSN_COUNT));   /* [fp+INSN_COUNT] := r9 */
    }
#else
    (void)n_insn;
#endif
    *end++ = ldr_offset(REG_CTX, 9, __builtin_offsetof(struct M68KState, LOOP_BUDGET));      /* r9 := [fp+LOOP_BUDGET] */
    *end++ = subs_immed(9, 9, copies);
    *end++ = str_offset(REG_CTX, 9, __builtin_offsetof(struct M68KState, LOOP_BUDGET));      /* [fp+LOOP_BUDGET] := r9 */
    *end = b_cc(ARM_CC_GT, (int32_t)(loop_entry - end));                                      /* budget left: next pass */
    end++;
    {
        /* exhausted: make the dispatcher poll the IPL on this very exit (r9 <= 0 here) */
        uint32_t a = (uint32_t)(uintptr_t)&ipl_poll_countdown;
        *end++ = movw_immed_u16(REG_PC, a & 0xffffu);
        *end++ = movt_immed_u16(REG_PC, a >> 16);
        *end++ = str_offset(REG_PC, 9, 0);                                                    /* [ip] := r9 */
    }
    return end;
}
#endif
#endif

#if defined(EMU68_INNER_LOOP) && defined(EMU68_BCC_INLINE) && !defined(__aarch64__)
#if defined(__aarch64__)
static unsigned b104_unroll_want(uint32_t body_insns, uintptr_t used_bytes, uintptr_t prologue_bytes, int may_close,
                                 int back_inside)
{
    if (!may_close || !g_xlat_backedge_ok || body_insns == 0u)
        return 1u;
    uint32_t n = Options.M68K_LOOP_COUNT;          /* m68k_do_jitctl and the feats2 apply keep it in 1..15 */
    if (n < 1u)  n = 1u;
    if (n > 15u) n = 15u;
    {   /* upstream's capacity rule: every copy must fit the unit depth ... */
        uint32_t cap = Options.M68K_TRANSLATION_DEPTH / body_insns;
        if (back_inside)
            cap = (Options.M68K_TRANSLATION_DEPTH + 1u > body_insns)
                  ? (Options.M68K_TRANSLATION_DEPTH + 1u - body_insns) / body_insns : 0u;
        if (n > cap) n = cap;
    }
    if (n > 1u) {
        uintptr_t per  = used_bytes - prologue_bytes;
        per += per / 4u + 64u;
        uintptr_t room = (used_bytes + EMU68_XLAT_WORST_UNIT < temporary_arm_code_nominal)
                         ? temporary_arm_code_nominal - EMU68_XLAT_WORST_UNIT - used_bytes : 0u;
        uintptr_t fit  = 1u + room / per;
        if ((uintptr_t)n > fit) n = (uint32_t)fit;
    }
    return n ? n : 1u;
}
#endif
#endif

#if defined(__aarch64__)
static inline uintptr_t M68K_Translate(uint16_t *m68kcodeptr)
{
    uint16_t *orig_m68kcodeptr = m68kcodeptr;
    uintptr_t hash = (uintptr_t)m68kcodeptr;
#define POP_UPDATE_MAX  (4 * EMU68_M68K_INSN_DEPTH + 4)
    uint32_t *pop_update_loc[POP_UPDATE_MAX];
    uint32_t pop_cnt=0;

    uint16_t *last_rev_jump = (uint16_t *)0xffffffff;

    if (RA_GetTempAllocMask())
        kprintf("[ICache] Temporary register alloc mask on translate start is non-zero %x\n", RA_GetTempAllocMask());

    if (disasm) {
        disasm_open_b();
    }

    for (int i=0; i < POP_UPDATE_MAX; i++)
        pop_update_loc[i] = (uint32_t *)0;

    M68K_ResetReturnStack();
    RA_ResetM68kAllocator();
    RA_ResetOMMXAllocator();   /* same for the OMMX fast bank - d16-d31 are caller-saved */
    OMMX_UnitStart();
#if !defined(__aarch64__)
    aerr_pop_fixup_cnt = 0;    /* no carry-over from an abandoned translation */
    M68K_ActiveInsn    = NULL;
#endif

    if (debug) {
        kprintf("[ICache] Creating new translation unit with hash %04x (m68k code @ %p)\n", (hash ^ (hash >> 16)) & 0xffff, (void*)m68kcodeptr);
        if (debug > 1)
            M68K_PrintContext(__m68k_state);
    }

    int lr_is_saved = 0;
#if defined(EMU68_INNER_LOOP) && !defined(__aarch64__)
    uint32_t *loop_entry = NULL;
#endif
    prologue_size = 0;
    epilogue_size = 0;
    conditionals_count = 0;

    insn_count = 0;
    uint32_t *arm_code = temporary_arm_code;
    uint32_t *end = arm_code;

    (void)prologue_size;
    (void)lr_is_saved;

    RA_ClearChangedMask();

    uint32_t *tmpptr = end;
    pop_update_loc[pop_cnt++] = end;

#ifndef __aarch64__
    *end++ = push(0); // Space for register saving, aarch32 only
#if defined(EMU68_INNER_LOOP) && !defined(__aarch64__)
    loop_entry = end;
#endif

#if !(EMU68_HOST_BIG_ENDIAN) && EMU68_HAS_SETEND
    *end++ = setend_be();
#endif

    *end++ = ldr_offset(REG_CTX, REG_PC, __builtin_offsetof(struct M68KState, PC));
#else
    if (debug_cnt & 2)
    {
        uint8_t reg = RA_AllocARMRegister(&end);
        *end++ = mov_immed_u16(reg, 4, 0);
        *end++ = msr(reg, 3, 3, 9, 12, 4);
        RA_FreeARMRegister(&end, reg);
    }
#endif

    prologue_size = end - tmpptr;

#if defined(EMU68_INNER_LOOP) && !defined(__aarch64__)
    g_xlat_unit_start  = orig_m68kcodeptr;
    g_xlat_backedge_ok = !g_trace_step && jit_backedge_allowed((uint32_t)(uintptr_t)orig_m68kcodeptr);
    int       b104_entry_clean = RA_AllStateFlushed();
    uintptr_t b104_lo = (uintptr_t)orig_m68kcodeptr;
    unsigned  b104_copies = 0, b104_want = 1;
    int       b104_tail_dead = 0;
    int       b104_back_inside = 0;   /* the first copy jumped backward to a target other than the unit start */
    (void)b104_want; (void)b104_tail_dead; (void)b104_back_inside;
#endif

    int break_loop = FALSE;
    int inner_loop = FALSE;
    int soft_break = FALSE;
    int max_rev_jumps = 0;
    insn_seen = 0;

    while (break_loop == FALSE && soft_break == FALSE && insn_count < Options.M68K_TRANSLATION_DEPTH
           && (uintptr_t)((uintptr_t)end - (uintptr_t)arm_code) + EMU68_XLAT_WORST_UNIT
              <= temporary_arm_code_nominal)
    {
        uint16_t insn_consumed;
        uint16_t *in_code = m68kcodeptr;
        uint32_t *out_code = end;

#if !defined(__aarch64__)
        if ((uintptr_t)m68kcodeptr & 1)
        {
            uint8_t areg;
            uint32_t odd_pc = (uint32_t)(uintptr_t)m68kcodeptr;

            end = EMIT_ResetOffsetPC(end);   /* REG_PC is overwritten below: no need to emit the add */
            areg = RA_AllocARMRegister(&end);
            *end++ = movw_immed_u16(areg, odd_pc & 0xffff);
            *end++ = movt_immed_u16(areg, odd_pc >> 16);
            *end++ = mov_reg(REG_PC, areg);
            end = EMIT_AddressError(end, areg,
                                    AERR_SSW_RW | AERR_SSW_FC_PROG,  /* read, program space */
                                    0 /* IR: the word at an odd address was never fetched */);
            RA_FreeARMRegister(&end, areg);
            break_loop = TRUE;
            break;
        }
        M68K_ActiveInsn = m68kcodeptr;
#endif

        if (insn_count && ((uintptr_t)m68kcodeptr < (uintptr_t)local_state[insn_count-1].mls_M68kPtr))
        {
            int found = -1;
//kprintf("going backwards... %p -> %p\n", local_state[insn_count-1].mls_M68kPtr, m68kcodeptr);
            for (int i=insn_count - 1; i >= 0; --i)
            {
                if (local_state[i].mls_M68kPtr == m68kcodeptr)
                {
//                        kprintf("PC match at i=%d, %d instructions\n", i, insn_count - i - 1);
                    found = i;
                    break;
                }
            }

            if (found > 0)
            {
                if ((insn_count - found - 1) > (Options.M68K_TRANSLATION_DEPTH - insn_count))
                {
//                        kprintf("not enough place for completion of the loop\n");
                    break;
                }
            }
        }


        if (m68kcodeptr < m68k_low)
            m68k_low = m68kcodeptr;
        if (m68kcodeptr + 16 > m68k_high)
            m68k_high = m68kcodeptr + 16;
#if defined(EMU68_INNER_LOOP) && !defined(__aarch64__)
        if ((uintptr_t)m68kcodeptr < b104_lo)
            b104_lo = (uintptr_t)m68kcodeptr;
#endif

        local_state[insn_count].mls_ARMOffset = end - arm_code;
        local_state[insn_count].mls_M68kPtr = m68kcodeptr;
        local_state[insn_count].mls_PCRel = _pc_rel;
#ifndef __aarch64__
        for (int r=0; r < 16; r++)
            local_state[insn_count].mls_RegMap[r] = RA_GetMappedARMRegister(r);
#endif
        end = EmitINSN(end, &m68kcodeptr, &insn_consumed);
        insn_count+=insn_consumed;
#if !defined(__aarch64__)
        while (aerr_pop_fixup_cnt) {
            uint32_t *loc = aerr_pop_fixup[--aerr_pop_fixup_cnt];
            if (pop_cnt < POP_UPDATE_MAX - 1)
                pop_update_loc[pop_cnt++] = loc;
            else
                kprintf("[JIT] pop_update_loc overflow (address-error exit)\n");
        }
        M68K_ActiveInsn = NULL;
#endif
        if (end[-1] == INSN_TO_LE(0xfffffff0))
        {
            lr_is_saved = 1;
            end--;
        }
#ifndef __aarch64__
        while (end[-1] == INSN_TO_LE(0xfffffff2))
        {
            end--;
            pop_update_loc[pop_cnt++] = (uint32_t *)(uintptr_t)*--end;
        }
#endif
        if (end[-1] == INSN_TO_LE(0xffffffff))
        {
            end--;
            break_loop = TRUE;
        }
        if (end[-1] == INSN_TO_LE(0xfffffff1))
        {
            end--;
            soft_break = TRUE;
        }
        if (end[-1] == INSN_TO_LE(0xfffffffe))
        {
            uint32_t *tmpptr;
            uint32_t *branch_mod[10];
            uint32_t branch_cnt;
            int local_branch_done = 0;
            end--;
            end--;  /* Remove branch target (unused!) */
            branch_cnt = *--end;
#if defined(EMU68_INNER_LOOP) && !defined(__aarch64__)
            if (branch_cnt == 0u)
                b104_tail_dead = 1;
#endif

            for (unsigned i=0; i < branch_cnt; i++)
            {
                uintptr_t ptr = *(uint32_t *)--end;
#ifdef __aarch64__
                ptr |= (uintptr_t)end & 0xffffffff00000000;
#endif
                branch_mod[i] = (uint32_t *)ptr;
            }

            tmpptr = end;

            conditionals_count++;

            if (!local_branch_done)
            {
                RA_StoreDirtyFPURegs(&end);
                RA_StoreDirtyOMMXRegs(&end);     /* OMMX fast bank, same rule as the FPU */
                RA_StoreDirtyM68kRegs(&end);
                end = EMIT_FlushPC(end);
#ifndef __aarch64__
                RA_StoreCC(&end);
                RA_StoreFPCR(&end);
                RA_StoreFPSR(&end);

                *end++ = str_offset(REG_CTX, REG_PC, __builtin_offsetof(struct M68KState, PC));
#if !(EMU68_HOST_BIG_ENDIAN) && EMU68_HAS_SETEND
                *end++ = setend_le();
#endif

#else
                RA_StoreCC(&end);
                RA_StoreFPCR(&end);
                RA_StoreFPSR(&end);
#endif
#ifndef __aarch64__
#if EMU68_INSN_COUNTER
                if (!JitStatsOff)
                {
#if defined(EMU68_BCC_INLINE)
                    uint8_t cnt_tmp = 9;
#else
                    uint8_t cnt_tmp = RA_AllocARMRegister(&end);
#endif
                    uint8_t cnt_ctx = RA_GetCTX(&end);
                    *end++ = ldr_offset(cnt_ctx, cnt_tmp, __builtin_offsetof(struct M68KState, INSN_COUNT));
                    {
                        uint32_t cnt_left = insn_count;
                        while (cnt_left) {
                            uint32_t c = cnt_left > 255 ? 255 : cnt_left;
                            *end++ = add_immed(cnt_tmp, cnt_tmp, c);
                            cnt_left -= c;
                        }
                    }
                    *end++ = str_offset(cnt_ctx, cnt_tmp, __builtin_offsetof(struct M68KState, INSN_COUNT));
#if !defined(EMU68_BCC_INLINE)
                    RA_FreeARMRegister(&end, cnt_tmp);
#endif
                }
#endif
                pop_update_loc[pop_cnt++] = end;
                *end++ = pop((1 << REG_SR));// | (1 << REG_CTX));
                if (!lr_is_saved)
                    *end++ = bx_lr();
#else

#if EMU68_INSN_COUNTER
                if (!JitStatsOff) {
                uint8_t ctx_free = 0;
                uint8_t ctx = RA_TryCTX(&end);
                uint8_t tmp = RA_AllocARMRegister(&end);
                if (ctx == 0xff)
                {
                    ctx = RA_AllocARMRegister(&end);
                    *end++ = mrs(ctx, 3, 3, 13, 0, 3);
                    ctx_free = 1;
                }
                EMIT_ldr64_offset(&end, ctx, tmp, __builtin_offsetof(struct M68KState, INSN_COUNT));
                EMIT_add64_immed(&end, tmp, tmp, insn_count & 0xfff);
                if (insn_count & 0xfff000)
                    *end++ = adds64_immed_lsl12(tmp, tmp, insn_count >> 12);
                EMIT_str64_offset(&end, ctx, tmp, __builtin_offsetof(struct M68KState, INSN_COUNT));

                RA_FreeARMRegister(&end, tmp);
                if (ctx_free)
                    RA_FreeARMRegister(&end, ctx);
                }
#endif
                pop_update_loc[pop_cnt++] = end;
                *end++ = bx_lr();
#endif
            }
            int distance = end - tmpptr;

            for (unsigned i=0; i < branch_cnt; i++) {
                //kprintf("[ICache] Branch modification at %p : distance increase by %d\n", (void*) branch_mod[i], distance);
#ifdef __aarch64__
                *(branch_mod[i]) = INSN_TO_LE((INSN_TO_LE(*(branch_mod[i])) + (distance << 5)));
#elif defined(EMU68_BCC_INLINE)
                {
                    uint32_t w   = INSN_TO_LE(*(branch_mod[i]));
                    int32_t  imm = ((int32_t)(w << 8)) >> 8;        /* sign-extend imm24 */
                    imm += distance;
                    if (imm >= (1 << 23) || imm < -(1 << 23))
                        kprintf("[JIT] skip patch out of range (%d words) at %p\n", imm, (void *)branch_mod[i]);
                    *(branch_mod[i]) = INSN_TO_LE((w & 0xff000000u) | ((uint32_t)imm & 0x00ffffffu));
                }
#else
                *(branch_mod[i]) = INSN_TO_LE((INSN_TO_LE(*(branch_mod[i])) + distance));
#endif
            }
            epilogue_size += distance;
        }

        {
            uintptr_t insn_bytes = (uintptr_t)end - (uintptr_t)out_code;
            if (insn_bytes > g_xlat_max_insn) {
                g_xlat_max_insn    = insn_bytes;
                g_xlat_max_insn_op = BE16(in_code[0]);
            }
        }

        if (disasm)
            disasm_print_b(in_code, insn_consumed, out_code, 4*(end - out_code), temporary_arm_code);

#if defined(EMU68_INNER_LOOP) && !defined(__aarch64__)
        if (!break_loop && !g_trace_step && orig_m68kcodeptr == m68kcodeptr)
            b104_copies++;
        else if (in_code > m68kcodeptr && b104_copies == 0u)
            b104_back_inside = 1;        /* an interior backward jump in the first copy (see b104_unroll_want) */
#endif
        if (in_code > m68kcodeptr)
        {
            if (debug)
                kprintf("[ICache]   Going backwards to location %08x\n", m68kcodeptr);
            if (last_rev_jump == m68kcodeptr) {
                if (--max_rev_jumps <= 0) {
                    if (debug)
                        kprintf("[ICache] Going backwards to the same location oft enough. Loop candidate. Breaking here\n");
                    break;
                }
            }
            else {
                last_rev_jump = m68kcodeptr;
                max_rev_jumps = (int)Options.M68K_LOOP_COUNT - 1;
            }
            if (max_rev_jumps <= 0 && !(!break_loop && !g_trace_step && orig_m68kcodeptr == m68kcodeptr)) {
                if (debug)
                    kprintf("[ICache] LCNT leaves no backward jump to follow. Breaking here\n");
                break;
            }
        }

        #if 1
        if (!break_loop && !g_trace_step && (orig_m68kcodeptr == m68kcodeptr))
        {
            if (debug)
                kprintf("[ICache]   Creating loop within translation unit\n");
            
            inner_loop = TRUE;
#if defined(EMU68_INNER_LOOP) && defined(EMU68_BCC_INLINE) && !defined(__aarch64__)
            if (b104_copies == 1u)
                b104_want = b104_unroll_want(insn_count, (uintptr_t)end - (uintptr_t)arm_code, 4u * prologue_size,
                                             b104_entry_clean && !b104_tail_dead
                                             && jit_backedge_allowed((uint32_t)b104_lo), b104_back_inside);
            if (b104_copies < b104_want && !soft_break) {
                last_rev_jump = (uint16_t *)0xffffffff;
                max_rev_jumps = 0;
                continue;
            }
#endif
            break;
        }
        #else
        (void)orig_m68kcodeptr;
        #endif

    }
#if defined(EMU68_INNER_LOOP) && !defined(__aarch64__)
    if (inner_loop && m68kcodeptr != orig_m68kcodeptr) {
        inner_loop = FALSE;
        g_b104_unroll_cut++;
    }
#endif
    uint32_t *out_code = end;
    tmpptr = end;
    RA_FlushFPURegs(&end);
    RA_FlushOMMXRegs(&end);              /* OMMX fast bank: store + forget, like the FPU */
    RA_FlushM68kRegs(&end);
    end = EMIT_FlushPC(end);
#ifndef __aarch64__
    RA_FlushCC(&end);
    RA_FlushFPCR(&end);
    RA_FlushFPSR(&end);
    *end++ = str_offset(REG_CTX, REG_PC, __builtin_offsetof(struct M68KState, PC));
    uint16_t mask = RA_GetChangedMask() & 0xfff0;
    if (RA_IsCCLoaded())
        mask |= 1 << REG_SR;
#if defined(EMU68_INNER_LOOP) && !defined(__aarch64__)
    if (inner_loop && !break_loop && !soft_break)
    {
        if (!g_xlat_backedge_ok || !jit_backedge_allowed((uint32_t)b104_lo))
            g_b104_refused_gate++;
        else if (!b104_entry_clean || !RA_AllStateFlushed())
            g_b104_refused_state++;
        else {
            end = EMIT_LoopBackEdge(end, loop_entry, insn_count, b104_copies ? b104_copies : 1u);
            g_b104_units++;
            if (b104_copies > 1u)
                g_b104_unrolled++;
        }
    }
#endif
#if !(EMU68_HOST_BIG_ENDIAN) && EMU68_HAS_SETEND
    *end++ = setend_le();
#endif
    if (!mask && !lr_is_saved)
#else
    RA_FlushCC(&end);
    RA_FlushFPCR(&end);
    RA_FlushFPSR(&end);
#endif
    {
#ifndef __aarch64__
        *pop_update_loc[0] = INSN_TO_LE(0xE320F000);
#endif
        int i=1;

        while (pop_update_loc[i]) {
#ifdef __aarch64__
            i++;
#else
            *pop_update_loc[i++] = bx_lr();
#endif
        }
    }
#ifndef __aarch64__
    if (lr_is_saved)
    {
        *end++ = pop(mask /*| (1 << REG_SR)*/ /*| (1 << REG_CTX)*/ | (1 << 15));
    }
    else if (mask)
    {
        *end++ = pop(mask /*| (1 << REG_SR)*/ /*| (1 << REG_CTX)*/);
    }

    if (mask || lr_is_saved)
    {
        int i=1;
        if (lr_is_saved)
            *pop_update_loc[0] = push(mask /*| (1 << REG_SR)*/ /*| (1 << REG_CTX)*/ | (1 << 14));
        else
            *pop_update_loc[0] = push(mask /*| (1 << REG_SR)*/ /*| (1 << REG_CTX)*/);
        while (pop_update_loc[i]) {
            if (lr_is_saved)
                *pop_update_loc[i++] = pop(mask /*| (1 << REG_SR)*/ /*| (1 << REG_CTX)*/ | (1 << 15));
            else
                *pop_update_loc[i++] = pop(mask /*| (1 << REG_SR)*/ /*| (1 << REG_CTX)*/);
        }
    }
    if (!lr_is_saved)
        *end++ = bx_lr();
#else
    uint8_t ctx = RA_GetCTX(&end);
    uint8_t tmp = RA_AllocARMRegister(&end);
    uint8_t tmp2 = RA_AllocARMRegister(&end);
    if (inner_loop)
    {
#ifdef PISTORM
        //*end++ = mov_immed_u16(tmp2, 0xf220, 1);
        //*end++ = ldr_offset(tmp2, tmp2, 0x34);
        *end++ = ldr_offset(ctx, tmp2, __builtin_offsetof(struct M68KState, IPL0));
#else
        *end++ = ldr_offset(ctx, tmp2, __builtin_offsetof(struct M68KState, PINT));
#endif
    }
#if EMU68_INSN_COUNTER
    if (!JitStatsOff)
    {
        EMIT_ldr64_offset(&end, ctx, tmp, __builtin_offsetof(struct M68KState, INSN_COUNT));
        EMIT_add64_immed(&end, tmp, tmp, insn_count & 0xfff);
        if (insn_count & 0xfff000)
            *end++ = adds64_immed_lsl12(tmp, tmp, insn_count >> 12);
        EMIT_str64_offset(&end, ctx, tmp, __builtin_offsetof(struct M68KState, INSN_COUNT));
    }
#endif
    if (inner_loop)
    {
        *end++ = ldr_offset(ctx, tmp, __builtin_offsetof(struct M68KState, LOOP_BUDGET));
        *end++ = subs_immed(tmp, tmp, 1);
        *end++ = str_offset(ctx, tmp, __builtin_offsetof(struct M68KState, LOOP_BUDGET));
        *end++ = b_cc(ARM_CC_LE, 3);        /* exhausted: fall through to bx_lr */
        uint32_t *tmpptr = end;
#ifdef PISTORM
        EMIT_cbnz(&end, tmp2, arm_code - tmpptr);
#else
        EMIT_cbz(&end, tmp2, arm_code - tmpptr);
#endif
    }
    *end++ = bx_lr();
    
    uint32_t *_tmpptr = end;
    RA_FreeARMRegister(&end, tmp2);
    RA_FreeARMRegister(&end, tmp);
    RA_FlushCTX(&end);
    end = _tmpptr;
    
#endif
    epilogue_size += end - tmpptr;

    if (disasm) {
        disasm_print_b((uint16_t *)0, 0, out_code, 4*(end - out_code), temporary_arm_code);
        disasm_close();
    }

    // Put a marker at the end of translation unit
    *end++ = 0xffffffff;

    if (debug && insn_count)   /* insn_count == 0 for an odd-PC address-error unit */
    {
        kprintf("[ICache]   Translated %d M68k instructions to %d ARM instructions\n", insn_count, (int)(end - arm_code));
        kprintf("[ICache]   Prologue size: %d, Epilogue size: %d, Conditionals: %d\n",
            prologue_size, epilogue_size, conditionals_count);
        kprintf("[ICache]   Mean epilogue size pro exit point: %d\n", epilogue_size / (1 + conditionals_count));
        uint32_t mean = 100 * (end - arm_code - (prologue_size + epilogue_size));
        mean = mean / insn_count;
        uint32_t mean_n = mean / 100;
        uint32_t mean_f = mean % 100;
        kprintf("[ICache]   Mean ARM instructions per m68k instruction: %d.%02d\n", mean_n, mean_f);
    }

    return (uintptr_t)end - (uintptr_t)arm_code;
}
#endif

/*
    Translate portion of m68k code into ARM. No new unit is created, instead
    a raw pointer to ARM code is returned and instruction cache on host side is
    invalidated
*/
#ifndef EMU68_SMC_CRC_WINDOW
#define EMU68_SMC_CRC_WINDOW 16   /* bytes of unit source covered by the staleness check */
#endif
int g_phase_nb = 0;
int g_jit_udf = 0;   /* set by TranslateNoCache when the block contains a UDF (unimplemented opcode) */
uint16_t g_jit_udf_op = 0;  /* ...and WHICH m68k opcode it was (the udf's imm16) */
uint32_t g_jit_selfb = 0;
uint32_t g_jit_sysop = 0;

uintptr_t M68K_Translate_sel(uint16_t *m68kcodeptr);
void *M68K_TranslateNoCache(uint16_t *m68kcodeptr)
{
    uintptr_t line_length = M68K_Translate_sel(m68kcodeptr);
    if (line_length > g_xlat_max_len) g_xlat_max_len = line_length;
    if (line_length > temporary_arm_code_nominal) {
        g_xlat_over_count++;
        if (line_length - temporary_arm_code_nominal > g_xlat_over_worst)
            g_xlat_over_worst = line_length - temporary_arm_code_nominal;
        kprintf("[XLAT] ** EMIT OVERRUN ** m68k @%p emitted %u bytes, budget %u (over by %u)\n",
                (void *)m68kcodeptr, (unsigned)line_length,
                (unsigned)temporary_arm_code_nominal,
                (unsigned)(line_length - temporary_arm_code_nominal));
    }
    void *entry_point = (void*)temporary_arm_code;


#ifdef __aarch64__
    entry_point = (void *)((uintptr_t)entry_point | 0x0000001000000000);
#endif

    arm_flush_cache((uintptr_t)entry_point, line_length);
    arm_icache_invalidate((intptr_t)entry_point, line_length);
    jit_icache_all();   /* KS_JITCOHERE: global belt (TranslateNoCache temp-buffer path) */

    return entry_point;
}

/*
    Verify if the translated code has changed since the unit was created. In order
    to do this MD5 sum of the block is compared with the previousy calculated one.

    If th sums are not same, the block is removed form LRU cache and hashtable and memory
    is released.

    The function returns poitner to verified unit or NULL if the unit changed
*/

void jit_evict_flush(struct M68KTranslationUnit *u)   /* also called from M68k_LINEF.c cache-clear paths */
{
    uint32_t len = u->mt_ARMInsnCnt * 4;               /* emitted code byte length */
    arm_flush_cache((uintptr_t)&u->mt_ARMCode, len);   /* DCCIMVAC: clean+invalidate D to PoC */
    arm_icache_invalidate((uintptr_t)u->mt_ARMEntryPoint, len); /* ICIMVAU + BPIALL + DSB + ISB */
    jit_icache_all();   /* KS_JITCOHERE: global belt over the ranged evict maintenance */
}

struct M68KTranslationUnit *M68K_VerifyUnit(struct M68KTranslationUnit *unit)
{
    if (unit)
    {
        uintptr_t vhi = (uintptr_t)unit->mt_M68kLow + EMU68_SMC_CRC_WINDOW;
        if (vhi > (uintptr_t)unit->mt_M68kHigh) vhi = (uintptr_t)unit->mt_M68kHigh;
        uint32_t crc = CalcCRC32(unit->mt_M68kLow, (uint16_t *)vhi);

        if (crc != unit->mt_CRC32)
        {
            REMOVE(&unit->mt_LRUNode);
            REMOVE(&unit->mt_HashNode);
            jit_evict_flush(unit);          /* drop stale I/D copy before tlsf reuses the DDR */
            a1_tlsf_free(jit_tlsf, unit);

            __m68k_state->JIT_UNIT_COUNT--;
            __m68k_state->JIT_CACHE_FREE = tlsf_get_free_size(jit_tlsf);
            if (__m68k_state->JIT_CACHE_TOTAL == 0)
                __m68k_state->JIT_CACHE_TOTAL = tlsf_get_total_size(jit_tlsf);

            unit = NULL;
        }
    }

    return unit;
}

int jit_soft_verify(struct M68KTranslationUnit *u)
{
    uint32_t crc = CalcCRC32(u->mt_M68kLow, u->mt_M68kHigh);
    if (crc == u->mt_CRC32Full) {
        jit_unit_unmark(u);
        return 1;
    }
    REMOVE(&u->mt_LRUNode);
    REMOVE(&u->mt_HashNode);
    jit_evict_flush(u);
    a1_tlsf_free(jit_tlsf, u);
    __m68k_state->JIT_UNIT_COUNT--;
    __m68k_state->JIT_CACHE_FREE = tlsf_get_free_size(jit_tlsf);
    return 0;
}

void M68K_InvalidateAllUnits(void)
{
    struct Node *n;

    while ((n = REMHEAD(&LRU)))
    {
        struct M68KTranslationUnit *u =
            (struct M68KTranslationUnit *)((intptr_t)n - __builtin_offsetof(struct M68KTranslationUnit, mt_LRUNode));
        REMOVE(&u->mt_HashNode);
        jit_evict_flush(u);            /* drop the stale I/D copy before tlsf reuses the DDR */
        a1_tlsf_free(jit_tlsf, u);
        __m68k_state->JIT_UNIT_COUNT--;
        __m68k_state->JIT_CACHE_FREE = tlsf_get_free_size(jit_tlsf);
    }
}

/*
    Get M68K code unit from the instruction cache. Return NULL if code was not found and needs to be
    translated first.

    If the code was found, update its position in the LRU cache.
*/
struct M68KTranslationUnit *M68K_GetTranslationUnit(uint16_t *m68kcodeptr)
{
    struct M68KTranslationUnit *unit = NULL, *n;
    uintptr_t hash = (uintptr_t)m68kcodeptr;
    uint16_t *orig_m68kcodeptr = m68kcodeptr;
    
    m68k_low = m68kcodeptr;
    m68k_high = m68kcodeptr;


    /* Get 16-bit has from the pointer to m68k code */
    hash = (hash ^ (hash >> 16)) & 0xffff;

    if (debug > 2)
        kprintf("[ICache] GetTranslationUnit(%08x)\n[ICache] Hash: 0x%04x\n", (void*)m68kcodeptr, (int)hash);

    /* Find entry with correct address */
    ForeachNode(&ICache[hash], n)
    {
        uintptr_t key = (uintptr_t)n->mt_M68kAddress;
        if ((key & ~(uintptr_t)1u) == (uintptr_t)m68kcodeptr)
        {
            if (__builtin_expect((key & 1u) != 0u, 0) && !jit_soft_verify(n))
                break;
            /* Unit found? Move it to the front of LRU list */
            unit = n;

            struct Node *this = &unit->mt_LRUNode;

#ifdef __aarch64__
            /* Correct unit found. Preload ICache */
            //asm volatile ("prfm plil1keep, [%0]"::"r"(unit->mt_ARMEntryPoint));
#endif
            if (1)
            {
                // Update LRU for least *frequently* used strategy
                if (this->ln_Pred->ln_Pred) {
                    struct Node *pred = this->ln_Pred;
                    struct Node *succ = this->ln_Succ;

                    this->ln_Pred = pred->ln_Pred;
                    this->ln_Succ = pred;
                    this->ln_Pred->ln_Succ = this;
                    pred->ln_Pred = this;
                    pred->ln_Succ = succ;
                    succ->ln_Pred = pred;
                }
            }
            else
            {
                // Update LRU for least *recently* used strategy
                REMOVE(&unit->mt_LRUNode);
                ADDHEAD(&LRU, &unit->mt_LRUNode);
            }

            return unit;
        }
    }

    if (unit == NULL)
    {
        uintptr_t line_length = M68K_Translate_sel(m68kcodeptr);
        uintptr_t arm_insn_count = line_length/4 - 1;
        if (line_length > g_xlat_max_len) g_xlat_max_len = line_length;
        if (line_length > temporary_arm_code_nominal) {   /* same budget, same guard */
            g_xlat_over_count++;
            if (line_length - temporary_arm_code_nominal > g_xlat_over_worst)
                g_xlat_over_worst = line_length - temporary_arm_code_nominal;
            kprintf("[XLAT] ** EMIT OVERRUN ** m68k @%p emitted %u bytes, budget %u (over by %u)\n",
                    (void *)m68kcodeptr, (unsigned)line_length,
                    (unsigned)temporary_arm_code_nominal,
                    (unsigned)(line_length - temporary_arm_code_nominal));
        }

#ifdef __aarch64__
        uintptr_t unit_length = (line_length + 63 + sizeof(struct M68KTranslationUnit)) & ~63;
#else
        uintptr_t unit_length = (line_length + 31 + sizeof(struct M68KTranslationUnit)) & ~31;
#endif
        do {
#ifdef __aarch64__
            unit = tlsf_malloc_aligned(jit_tlsf, unit_length, 64);
#else
            unit = tlsf_malloc_aligned(jit_tlsf, unit_length, 32);
#endif
            __m68k_state->JIT_CACHE_FREE = tlsf_get_free_size(jit_tlsf);
            if (__m68k_state->JIT_CACHE_TOTAL == 0)
                __m68k_state->JIT_CACHE_TOTAL = tlsf_get_total_size(jit_tlsf);

            if (unit == NULL)
            {
                #ifndef __aarch64__
                extern uint32_t last_PC;
                #endif
                if (debug > 0) {
                    kprintf("[ICache] Requested block was %d bytes long\n", unit_length);
                }

                for (int i=0; i < 8; i++) {
                    struct Node *n = REMTAIL(&LRU);

                    if (n == NULL)
                        break;

                    void *ptr = (char *)n - __builtin_offsetof(struct M68KTranslationUnit, mt_LRUNode);
                    REMOVE((struct Node *)ptr);
                    if (debug > 0)
                    {
                        kprintf("[ICache] Run out of cache. Removing least recently used cache line node @ %p\n", ptr);
                    }
                    jit_evict_flush((struct M68KTranslationUnit *)ptr);
                    a1_tlsf_free(jit_tlsf, ptr);
                    __m68k_state->JIT_UNIT_COUNT--;
                }
                __m68k_state->JIT_CACHE_FREE = tlsf_get_free_size(jit_tlsf);
                #ifdef __aarch64__
                asm volatile("mcr p15, 0, %0, c13, c0, 4"::   "r"(0xffffffff));
                #else
                last_PC = 0xffffffff;
                #endif
            }
        } while(unit == NULL);

        unit->mt_ARMEntryPoint = &unit->mt_ARMCode[0];
#ifdef __aarch64__
        unit->mt_ARMEntryPoint = (void *)((uintptr_t)unit->mt_ARMEntryPoint | 0x0000001000000000);
#endif
        unit->mt_M68kInsnCnt = insn_count;
        unit->mt_ARMInsnCnt = arm_insn_count;
        unit->mt_UseCount = 0;
        unit->mt_FetchCount = 0;
        unit->mt_M68kAddress = orig_m68kcodeptr;
        unit->mt_M68kLow = m68k_low;
        unit->mt_M68kHigh = m68k_high;
        {
            uintptr_t chi = (uintptr_t)m68k_low + EMU68_SMC_CRC_WINDOW;
            if (chi > (uintptr_t)m68k_high) chi = (uintptr_t)m68k_high;
            unit->mt_CRC32 = CalcCRC32(m68k_low, (uint16_t *)chi);
        }
        unit->mt_CRC32Full = CalcCRC32(m68k_low, m68k_high);
        unit->mt_PrologueSize = prologue_size;
        unit->mt_EpilogueSize = epilogue_size;
        unit->mt_Conditionals = conditionals_count;
        DuffCopy(&unit->mt_ARMCode[0], temporary_arm_code, line_length/4);

        ADDHEAD(&LRU, &unit->mt_LRUNode);
        ADDHEAD(&ICache[hash], &unit->mt_HashNode);

        __m68k_state->JIT_UNIT_COUNT++;
        if (!JitStatsOff)
            __m68k_state->JIT_CACHE_MISS++;
        if (__builtin_expect(__m68k_state->DBG_CTRL != 0u, 0)) {
            extern void jit_dbg_unit_w(struct M68KTranslationUnit *u);
            jit_dbg_unit_w(unit);
        }

        if (debug) {
            kprintf("[ICache]   Block checksum: %08x\n", unit->mt_CRC32);
            kprintf("[ICache]   ARM code at %p\n", unit->mt_ARMEntryPoint);
        }

        arm_flush_cache((uintptr_t)&unit->mt_ARMCode, line_length);
        arm_icache_invalidate((intptr_t)unit->mt_ARMEntryPoint, line_length);
        jit_icache_all();   /* KS_JITCOHERE: global belt over the ranged emit maintenance */

        if (debug)
        {
            kprintf("-- ARM Code dump --\n");
            for (uint32_t i=0; i < unit->mt_ARMInsnCnt; i++)
            {
                if ((i % 5) == 0)
                    kprintf("   ");
                uint32_t insn = LE32(unit->mt_ARMCode[i]);
                kprintf(" %02x %02x %02x %02x", insn & 0xff, (insn >> 8) & 0xff, (insn >> 16) & 0xff, (insn >> 24) & 0xff);
                if ((i % 5) == 4)
                    kprintf("\n");
            }
            if (unit->mt_ARMInsnCnt % 5 != 0)
                kprintf("\n");
            if (debug > 3)
            {
                kprintf("\n-- Local State --\n");
                for (unsigned i=0; i < insn_count; i++)
                {
                    kprintf("    %p -> %08x", local_state[i].mls_M68kPtr, local_state[i].mls_ARMOffset);
                    for (int r=0; r < 16; r++) {
                        if (local_state[i].mls_RegMap[r] != 0xff) {
                            kprintf(" %c%d=r%d%s", r < 8 ? 'D' : 'A', r % 8, local_state[i].mls_RegMap[r] & 15,
                            local_state[i].mls_RegMap[r] & 0x80 ? "!":"");
                        }
                    }
                    kprintf(" PC_Rel=%d\n", local_state[i].mls_PCRel);
                }
            }
        }
    }

#ifdef __aarch64__
    //asm volatile ("prfm plil1keep, [%0]"::"r"(unit->mt_ARMEntryPoint));
#endif

    return unit;
}

void M68K_InitializeCache()
{
    kprintf("[ICache] Initializing caches\n");

#ifndef __aarch64__

    if (jit_tlsf == NULL)
        jit_tlsf = tlsf;

#endif

    kprintf("[ICache] Setting up LRU\n");
    NEWLIST(&LRU);

    kprintf("[ICache] Setting up ICache\n");
    ICache = tlsf_malloc(tlsf, sizeof(struct List) * 65536);
    temporary_arm_code_nominal = EMU68_M68K_INSN_DEPTH * 16 * 64 + EMU68_XLAT_WORST_UNIT;
    temporary_arm_code = tlsf_malloc(jit_tlsf, temporary_arm_code_nominal + TEMP_CODE_GUARD);
    __m68k_state->JIT_CACHE_FREE = tlsf_get_free_size(jit_tlsf);
    g_xlat_budget = temporary_arm_code_nominal;
    kprintf("[ICache] Temporary code at %p  (budget %u bytes + %u guard)\n",
            temporary_arm_code, (unsigned)temporary_arm_code_nominal,
            (unsigned)TEMP_CODE_GUARD);
    local_state = tlsf_malloc(tlsf, sizeof(struct M68KLocalState)*EMU68_M68K_INSN_DEPTH*2);
    kprintf("[ICache] ICache array at %p\n", ICache);
    for (int i=0; i < 65536; i++)
        NEWLIST(&ICache[i]);
}

void M68K_FlushCache(void)
{
    struct Node *n;
    unsigned cnt = 0;
    while ((n = REMHEAD(&LRU)) != NULL)
    {
        struct M68KTranslationUnit *unit =
            (void *)((char *)n - __builtin_offsetof(struct M68KTranslationUnit, mt_LRUNode));
        REMOVE(&unit->mt_HashNode);
        jit_evict_flush(unit);          /* drop stale I/D copy before tlsf reuses the DDR */
        a1_tlsf_free(jit_tlsf, unit);
        cnt++;
    }
    if (__m68k_state) {
        __m68k_state->JIT_CACHE_FREE = tlsf_get_free_size(jit_tlsf);
        if (__m68k_state->JIT_UNIT_COUNT >= cnt) __m68k_state->JIT_UNIT_COUNT -= cnt;
        else __m68k_state->JIT_UNIT_COUNT = 0;
    }
    kprintf("[ICache] FlushCache: %u units dropped\n", cnt);
}

void M68K_DumpStats()
{
    struct M68KTranslationUnit *unit = NULL;
    struct Node *n;
    unsigned cnt = 0;
    uintptr_t size = 0;
    unsigned m68k_count = 0;
    unsigned arm_count = 0;
    unsigned total_arm_count = 0;

    if (debug)
        kprintf("[ICache] Listing translation units:\n");
    ForeachNode(&LRU, n)
    {
        cnt++;
        unit = (void *)((char *)n - __builtin_offsetof(struct M68KTranslationUnit, mt_LRUNode));
        if (debug)
            kprintf("[ICache]   Unit %p, mt_UseCount=%lld, mt_FetchCount=%lld, M68K address %08x (range %08x-%08x)\n[ICache]      M68K insn count=%d, ARM insn count=%d\n", 
                (void*)unit, unit->mt_UseCount, unit->mt_FetchCount,
                (void*)unit->mt_M68kAddress, (void*)unit->mt_M68kLow, (void*)unit->mt_M68kHigh, 
                unit->mt_M68kInsnCnt, unit->mt_ARMInsnCnt);

        size = size + (uintptr_t)(&unit->mt_ARMCode[unit->mt_ARMInsnCnt]) - (uintptr_t)unit;
        m68k_count += unit->mt_M68kInsnCnt;
        total_arm_count += unit->mt_ARMInsnCnt;
        arm_count += unit->mt_ARMInsnCnt - (unit->mt_PrologueSize + unit->mt_EpilogueSize);
    }
    kprintf("[ICache] In total %d units (%d bytes) in cache\n", cnt, size);

    uint32_t mean = 100 * (arm_count);
    mean = mean / m68k_count;
    uint32_t mean_n = mean / 100;
    uint32_t mean_f = mean % 100;
    kprintf("[ICache] Mean ARM instructions per m68k instruction: %d.%02d\n", mean_n, mean_f);

    mean = 100 * (total_arm_count);
    mean = mean / m68k_count;
    mean_n = mean / 100;
    mean_f = mean % 100;
    kprintf("[ICache] Mean total ARM instructions per m68k instruction: %d.%02d\n", mean_n, mean_f);
}

uint32_t *EMIT_InjectPrintContext(uint32_t *ptr)
{
    extern void M68K_PrintContext(void*);

#ifdef __aarch64__
    union {
        uint64_t u64;
        uint32_t u32[2];
    } u;

    u.u64 = (uintptr_t)M68K_PrintContext;

    EMIT_stp64_preindex(&ptr, 31, 0, 1, -80);
    EMIT_stp64(&ptr, 31, 2, 3, 16);
    EMIT_stp64(&ptr, 31, 4, 5, 32);
    EMIT_stp64(&ptr, 31, 6, 7, 48);
    EMIT_str64_offset(&ptr, 31, 30, 64);

    *ptr++ = mrs(0, 3, 3, 13, 0, 3);
    *ptr++ = adr(30, 20);
    *ptr++ = ldr64_pcrel(1, 2);
    *ptr++ = br(1);

    *ptr++ = u.u32[0];
    *ptr++ = u.u32[1];

    EMIT_ldp64(&ptr, 31, 2, 3, 16);
    EMIT_ldp64(&ptr, 31, 4, 5, 32);
    EMIT_ldp64(&ptr, 31, 6, 7, 48);
    EMIT_ldr64_offset(&ptr, 31, 30, 64);
    EMIT_ldp64_postindex(&ptr, 31, 0, 1, 80);
#else

#endif
    return ptr;
}


static void put_to_stream(void *d, char c)
{
    char **pptr = (char**)d;
    char *ptr = *pptr;

    *ptr++ = c;

    *pptr = ptr;
}

uint32_t *EMIT_InjectDebugStringV(uint32_t *ptr, const char * restrict format, va_list args)
{
#ifdef __aarch64__
    void *tmp;
    uint32_t *tmpptr;

    union {
        uint64_t u64;
        uint32_t u32[2];
    } u;

    u.u64 = (uintptr_t)kprintf;

    EMIT_stp64_preindex(&ptr, 31, 0, 1, -256);
    for (int i=2; i < 30; i += 2)
        EMIT_stp64(&ptr, 31, i, i+1, i*8);
    EMIT_str64_offset(&ptr, 31, 30, 240);

    tmpptr = ptr;
    *ptr++ = adr(0, 48);
    *ptr++ = adr(30, 20);
    *ptr++ = ldr64_pcrel(1, 2);
    *ptr++ = br(1);

    *ptr++ = u.u32[0];
    *ptr++ = u.u32[1];

    for (int i=2; i < 30; i += 2)
        EMIT_ldp64(&ptr, 31, i, i+1, i*8);
    EMIT_ldr64_offset(&ptr, 31, 30, 240);
    EMIT_ldp64_postindex(&ptr, 31, 0, 1, 256);

    *ptr++ = b(0);
    tmp = ptr;

    *tmpptr = adr(0, (uintptr_t)ptr - (uintptr_t)tmpptr);

    vkprintf_pc(put_to_stream, &tmp, format, args);

    *(char*)tmp = 0;

    tmp = (void*)(((uintptr_t)tmp + 4) & ~3);

    ptr[-1] = b(1 + ((uintptr_t)tmp - (uintptr_t)ptr) / 4);
    
    ptr = (uint32_t *)tmp;
#else

#endif
    return ptr;
}

uint32_t *EMIT_InjectDebugString(uint32_t *ptr, const char * restrict format, ...)
{
    va_list v;
    va_start(v, format);
    ptr = EMIT_InjectDebugStringV(ptr, format, v);
    va_end(v);
    return ptr;
}

#if !defined(__aarch64__)
__asm__(".pushsection .r10c2_tail_rodata,\"a\",%progbits\n"
        "\t.balign 4\n"
        "\t.global g_r10c_statics\n"
        "\t.type g_r10c_statics, %object\n"
        "g_r10c_statics:\n"
        "\t.word temporary_arm_code\n"
        "\t.word temporary_arm_code_nominal\n"
        "\t.word local_state\n"
        "\t.word aerr_pop_fixup\n"
        "\t.word aerr_pop_fixup_cnt\n"
        "\t.size g_r10c_statics, 20\n"
        "\t.popsection\n");
#else
uintptr_t M68K_Translate_sel(uint16_t *m68kcodeptr) { return M68K_Translate(m68kcodeptr); }
#endif
