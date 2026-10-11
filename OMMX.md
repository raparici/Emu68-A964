# OMMX

OMMX (Open MMX) is an open SIMD extension of the 68k instruction set, implemented by the Emu68-A964 JIT
(Michal Schulz's Emu68, ported to the Cortex-A9 of the DE10-Nano for the BigMig Amiga core). It
contains Apollo Computer's AMMX, the vector unit of the Apollo 68080, as a subset: AMMX programs
run unchanged in AMMX's own opcode block, and every AMMX instruction has an OMMX twin one bit apart.
OMMX adds what AMMX does not cover (32-bit lane arithmetic with multiply-accumulate, rounding and
truncating vector shifts, a capability probe) and `ONCALL`, one instruction that calls ARM-native
code from 68k code. OMMX says nothing about the chipset: BigMig never claims to be a 68080, sets no
`AttnFlags` bit and provides no `vampire.resource`. AMMX is the Apollo Team's design, documented in
their *AC68080 Programmer's Reference Manual* (PRM); OMMX follows it wherever the two overlap.

## 1. Enabling it

| Switch | Where | Effect | As shipped |
|---|---|---|---|
| `EMU68_OMMX` | CMake option (`CMakeLists.txt`) | compiles the decoder and emitters (`src/M68k_OMMX.c`) and the per-task register banks (`src/ommx_banks.c`) | ON for `TARGET=de10nano` |
| `EMU68_A3D` | CMake option, requires `EMU68_OMMX` | compiles `ONCALL` and the 3D engine behind it | ON for `TARGET=de10nano` |
| `EnableOMMX` | run time, from the feature descriptor (`'FTR1'`, word 1 bit 0, `FEAT_OMMX_DECODE` in `src/pistorm/amiga_memmap.h`) written by the MiSTer Main | when 0, every word in `$FC00-$FFFF` raises Line-F exactly as on a plain 68k | firmware alone: OFF; with the Main: ON |
| OSD row `OMMX : ON (Default) / OFF` | Main, Emu68-A9 page, saved per configuration | sets the descriptor bit; applies after the next Amiga reset | ON |

The firmware reads the descriptor at boot and at every 68k reset (`feat_desc_apply()` in
`src/de10nano/start_de10nano.c`) and flushes the translation cache only when the bit changes.
A missing or invalid descriptor means OMMX off. The descriptor's bit 1 (`FEAT_OMMX_ANNOUNCE`) is
reserved and not implemented.

**Compatibility note.** Some Vampire software detects a 68080 by executing an AMMX instruction
under a trap handler. With OMMX on, that test succeeds, and such a program may go on to use
Vampire-only hardware (SAGA registers, ROM remapping) that BigMig does not have. Run that software
with OMMX OFF in its configuration.

## 2. Encoding

Two Line-F blocks share one decoder. Coprocessor ID 7 (`$FE00-$FFFF`) is Apollo's AMMX block; OMMX
uses ID 6 (`$FC00-$FDFF`). Neither collides with what Emu68 already decodes in Line-F (FPU, MOVE16,
CINV/CPUSH). Definitions: `include/OMMX.h`.

```
word 1   15 14 13 12 11 10  9 | 8 | 7 | 6 | 5 4 3 2 1 0
AMMX      1  1  1  1  1  1  1 | A | B | D |    VEA         $FE00-$FFFF  (cpID 7)
OMMX      1  1  1  1  1  1  0 | A | B | D |    VEA         $FC00-$FDFF  (cpID 6)

word 2   15 14 13 12 | 11 10  9  8 | 7 6 5 4 3 2 1 0
           REG-B     |    REG-D    |       Op
```

- `Op $00-$3F` mean the same in both blocks, so an AMMX instruction and its OMMX twin differ only
  in bit 9 of word 1. `Op $40-$FF` are OMMX-native and decode only in the OMMX block.
- `B` and `D` are the bank bits of `REG-B` and `REG-D`. `A` is the bank bit of a register-direct
  `VEA` and selects the short-immediate form.
- Length: 2 words plus the `VEA` extension words. `#imm.Q` (`A`=0) is four words; `#imm.W`
  (`A`=1) is one word, splatted to the four 16-bit lanes. `VPERM` is 4 words.
- Special forms: `VPERM` has `VEA` = `111111`, `REG-A` in word-2 bits 3-0 (bank from `A`) and a
  32-bit selector. `TRANS*` and `MINTERM` take a group of four source registers from word-1
  bits 3-2. `LOADI` (`LOAD` with `REG-B`=1) and `STOREI` (`STORE` with `REG-D`=1) are distinct forms.
- Where the PRM's diagrams and its examples disagree, the encodings follow vasm's 68080 backend
  (`MACHINE ac68080`) and the encodings that already run in RiVA. AMMX source can therefore be
  assembled with vasm; clearing bit 9 of word 1 gives the OMMX-block twin. Example: `c2p e0,e1` is
  `$FE08 $0928` in the AMMX block and `$FC08 $0928` in the OMMX block.

### Register file (32 x 64-bit, AMMX's)

| Registers | Selected by | Residency on the Cortex-A9 |
|---|---|---|
| `D0-D7` | bank 0, field `0000-0111`; `VEA` mode 000 with `A`=0 | bits 31:0 are the integer `Dn` (an ARM register); bits 63:32 live in the context (`M68KState.D_hi[]`) |
| `E0-E7` | bank 0, field `1000-1111`; `VEA` mode 001 with `A`=0 | context memory (`M68KState.E[0..7]`) |
| `E8-E23` | bank 1, field `0000-1111`; `VEA` modes 000/001 with `A`=1 | NEON `d16-d31`, one to one, cached across a translation unit with load/dirty tracking, like `FP0-FP7` in `d8-d15` (`src/armhf/RegisterAllocator.c`) |

- An integer write to `Dn` leaves bits 63:32 unchanged. This is implementation-defined; do not rely on it.
- Lane order is memory order: element 0 is at the lowest address, the most significant end of the
  register. The A9 build is big-endian (BE8), so a 64-bit load gives the AMMX layout directly.
  Operations that care about lane order (`VPERM`, `TRANS*`, packs, `C2P`, `BFLY*` pairs) absorb the
  NEON lane mirror at translation time, at no run-time cost.
- Guest memory is accessed with ARM integer loads and stores (two 32-bit words per 64-bit access),
  never with VFP/NEON transfers. `(An)+` and `-(An)` step by 8.
- Native code reached in the middle of a translated unit must not lose `d16-d31`: the data-abort
  paths that call C board models go through a wrapper that saves them (`src/armhf/vectors.c`), and
  the JIT flushes the fast bank before its own helper calls (FPU, divide, cache maintenance) and
  before `ONCALL`.

### Exceptions and condition codes

- No OMMX or AMMX data instruction changes the CCR.
- Anything not implemented (unassigned `Op`, unsupported size or form, the `Bn` address-register
  modes, OMMX-native opcodes in the AMMX block) raises Line-F (vector `$2C`) with a format-0 frame
  whose PC is the instruction's first word: the same frame a plain 68k produces. Trap handlers that
  skip or emulate the instruction keep working.

## 3. Instruction set

### 3.1 The AMMX subset (`Op $00-$3F`, both blocks)

`a` = the `VEA` operand, `b` = `REG-B`, `d` = `REG-D`. Sizes: B = 8 x 8-bit, W = 4 x 16-bit.

| Op | Mnemonic | Operation | Status |
|---|---|---|---|
| `$00` | `VPERM #sel,a,b,d` | byte i of d = byte n_i of the 16 bytes a:b, n_i = nibble i of sel (high nibble first) | implemented |
| `$01` | `LOAD <vea>,d` | 64-bit load; `#imm.W` splat; register copy | implemented; `LOADI` traps |
| `$02/$03` | `TRANSHI/TRANSLO a0-a3,d:d+1` | 4 x 4 word transpose, columns 0-1 / 2-3 | implemented |
| `$04` | `STORE b,<vea>` | 64-bit store | implemented; `STOREI` traps |
| `$05` | `STOREM b,mask,<vea>` | byte i stored when bit 7-i of the mask's low byte is set | implemented |
| `$06` | `PACKUSWB b,d,<vea>` | 2 x 4 signed words to 8 unsigned saturated bytes, b first | implemented |
| `$07` | `PACK3216 b,d,<vea>` | 2 x 2 ARGB32 to 4 RGB565, b first | implemented |
| `$08-$0B` | `PAND POR PEOR PANDN` | bitwise; `PANDN`: d = ~a & b | implemented |
| `$0C` | `PAVGB` | (a + b + 1) >> 1, unsigned bytes | implemented |
| `$10-$13` | `PADDB PADDW PSUBB PSUBW` | modular; `PSUB*`: d = b - a | implemented |
| `$14-$17` | `PADDUSB/W PSUBUSB/W` | unsigned saturating | implemented |
| `$18-$1B` | `PMUL88 PMULA PMULH PMULL` | signed (a*b)>>8 / alpha blend / signed high 16 / low 16 | implemented |
| `$1C/$1D` | `BFLYB/BFLYW <vea>,b,d:d+1` | d = b + a, d+1 = b - a | implemented |
| `$1E` | `UNPACK1632` | RGB565 to ARGB32 | traps (sources disagree on the alpha byte) |
| `$20/$21 $22/$23 $2C/$2D $2E/$2F` | `PCMPEQ PCMPHI PCMPGE PCMPGT` (B/W) | all-ones / all-zeros per lane; HI unsigned, GE/GT signed | implemented |
| `$24` | `STOREC b,count,<vea>` | first `count` bytes (low 32 bits of register d, signed) | implemented |
| `$25` | `STOREILM b,mask,<vea>` | byte i stored when bit 0 of mask byte i is clear | implemented |
| `$26` | `STOREM3 b,#mode,<vea>` | mask from the data: 0 long msb set, 1 byte non-zero, 2 word not `$F81F`, 3 word msb clear | implemented |
| `$28` | `C2P <vea>,d` | 8 x 8 bit transpose (chunky to planar) | implemented |
| `$29` | `BSEL <vea>,mask,d` | d = (d & ~mask) \| (a & mask) | implemented |
| `$2A` | `MINTERM a0-a3,d` | 3-input bitwise function, minterm code in a3's low byte | implemented |
| `$30-$37` | `PMIN/PMAX` S/U x B/W | per-lane minimum / maximum | implemented |
| `$38/$39` | `LSLQ/LSRQ <vea>,b,d` | 64-bit shift of b by a mod 64 | implemented |
| `$3E` | `TEX` | | traps (the PRM marks it as not working on current cores) |

Masked and counted stores are byte-exact: one conditional byte store per selected byte, never a
read-modify-write, so unselected bytes are never touched.

### 3.2 OMMX-native (`Op $40-$FF`, OMMX block only)

Native integer opcodes come in families of four, `Op = family*4 + size` (`00`=B `01`=W `10`=L
`11`=Q). Every size is specified; the current implementation provides the `.L` member (2 x 32-bit
lanes) and the other sizes raise Line-F. No saturation, CCR untouched.

| Ops | Mnemonic | Operation (per lane) | Implemented |
|---|---|---|---|
| `$40-$43` | `OADD.z <vea>,Eb,Ed` | d = b + a, modular | `.L` (`$42`) |
| `$44-$47` | `OSUB.z <vea>,Eb,Ed` | d = b - a, modular | `.L` (`$46`) |
| `$48-$4B` | `OMUL.z <vea>,Eb,Ed` | d = low(b * a), non-widening | `.L` (`$4A`) |
| `$4C-$4F` | `OMLA.z <vea>,Eb,Ed` | d += low(b * a); d is read and written | `.L` (`$4E`) |
| `$50-$53` | `OSRAR.z #n,Eb,Ed` | (b + 2^(n-1)) >> n, arithmetic, rounded; `A`=1 immediate form, extension word = n | `.L` (`$52`) |
| `$54-$57` | `OPADD.z <vea>,Eb,Ed` | pairwise add (NEON `VPADD` shape): the `VEA` operand's pair in the high long, b's pair in the low long | `.L` (`$56`) |
| `$58-$5B` | `OSRA.z #n,Eb,Ed` | b >> n, arithmetic, truncating; same form as `OSRAR` | `.L` (`$5A`) |
| `$E0` | `OIDENT Dn` | capability word into `Dn` (`$FC00+n`, `$00E0`) | yes |
| `$E1` | `ONCALL #id` | call ARM-native code (section 5) | when the 3D engine is built and running |

`$5C-$DF` and `$E2-$FF` are unassigned.

### 3.3 How the set grew

| Driver | What it needed | Result |
|---|---|---|
| RiVA (MPEG-1 video player, GPL) | 17 AMMX opcodes for the IDCT, motion compensation and colour conversion | AMMX subset first, then the rest of the AMMX table; RiVA builds for either block, and the BigMig build ships with `OIDENT` detection and its 68k code as the fallback |
| MPEG audio (`mpega.library`: Stéphane Tavenard's API, Sigbjørn Skjæret's libmad-based implementation) | non-widening 32-bit multiply-accumulate, pairwise reduction, rounding and truncating right shifts: shapes AMMX cannot express | the native `.L` families `$40-$5B`; the shipped library's polyphase synthesis window uses them, bit-exact with its scalar fixed-point code, which it runs instead when OMMX is absent |
| 3D engine (Warp3D and MiniGL libraries) | coarse calls into ARM-native rasterisation and state handling | `ONCALL` (section 5) |
| MHI (MP3/MP2 decoding for players such as AmigaAMP) | nothing from OMMX | uses a mailbox in the ZZ9000 card's register window, independent of OMMX (section 5.4) |

### 3.4 Detection

`OIDENT Dn` returns `version(31:24) | register count(23:16) | features(7:0)`:

| Bit | Meaning | Today |
|---|---|---|
| 0 | AMMX block decoded | 1 |
| 1 | OMMX block decoded | 1 |
| 2 | float32 group | 0 (reserved) |
| 3 | fused multiply-accumulate | 0 (reserved) |
| 4 | SIMD divide / square root | 0 (reserved) |
| 5 | `ONCALL` available | 1 when the 3D engine is up |
| 6 | 128-bit operations | 0 (reserved) |

So `OIDENT` reads `$01200003`, or `$01200023` with `ONCALL`. The recommended idiom, used by the
shipped RiVA and `mpega.library`: execute `OIDENT` in user mode with a Line-F catcher in
`tc_TrapCode`; treat a trap as "no OMMX"; require version >= 1, bit 1 and 32 registers; then run a
short known-answer test of the instructions the program uses, because native opcodes have no
individual feature bits and an unimplemented one raises Line-F. AMMX-only programs may keep the
classic Apollo probe instead (`VPERM #$807C5476,D0,D0,D0` on `$DEADBEEF` gives `$ADDEEFBE`).

## 4. Per-task register banks (`src/ommx_banks.c`)

Exec does not know about `E0-E23` or the upper halves of `D0-D7`, so the firmware keeps one bank
per exec task:

- **Switch points.** The `RTE` with which exec enters a task (serviced in C, after the translation
  unit has written back `d16-d31`), and an SR write that leaves supervisor mode. The current task
  is `ExecBase->ThisTask`; ExecBase is validated (even, `ChkBase` = ~ExecBase) and cached.
- **Lazy.** The outgoing bank is saved only if an OMMX instruction ran since that task was
  switched in (one store per translation unit that contains one, `M68KState.OMMX_TOUCHED`). The
  incoming bank is loaded only if the task has one. Programs that never use OMMX pay a test per
  `RTE`.
- **Limits.** 64 slots, least-recently-switched eviction: the 65th task with OMMX state evicts one,
  whose state is lost. OMMX in interrupt code uses the interrupted task's bank. Without a valid
  ExecBase (an OS-less game or demo) there is a single bank.
- **Switch.** On by default. Bit 5 of the descriptor's second feature word (`FEAT2_NO_OMMX_BANKS`)
  or bit 16 of the JIT's second control register returns to one global bank.

## 5. ONCALL: calling ARM-native code

### 5.1 Instruction

`ONCALL #id` = `$FD3C $00E1 id`: OMMX block, `A`=1, `VEA` = `#imm.W`, `Op $E1`, 16-bit id in the
extension word (3 words; `OMMX_ONCALL_W1/W2` in `include/OMMX.h`, `A3D_ONCALL_W1/W2` in
`src/de10nano/agent3d/a3d_abi.h`). It raises Line-F when the firmware is built without
`EMU68_A3D`, when OMMX is off, or when the native service is not running; `OIDENT` bit 5 says
whether it is available.

### 5.2 Calling convention

| | |
|---|---|
| In | `D0`, `D1` scalars; `A0` context or object; `A1` argument block |
| Out | `D0` result or error code, `D1` secondary result |
| Scratch | `D0`, `D1`, `A0`, `A1` (the AmigaOS convention); all other integer registers preserved; do not rely on the CCR |
| Vector state | the JIT writes back its cached `E8-E23` before the call; the guest's OMMX registers survive it |
| Argument blocks | big-endian, in guest memory, read in place by the native side (the A9 is big-endian too), not retained after return; the 3D engine checks guest pointers against a table of guest memory ranges before use |
| Timing | synchronous: the guest resumes at the next instruction when the native function returns, and takes no interrupt meanwhile, so entries are kept short |
| Unknown id | an error code in `D0`, never a trap |

### 5.3 Two paths, same results

- **JIT path (default).** The id is an immediate, so the translator emits a direct call: it stores
  `D0/D1/A0/A1` in a 16-byte block on the native stack, passes the block and the id in `r0/r1`,
  calls the native dispatcher (`a3d_oncall_jit()` in `src/de10nano/agent3d/a3d_fw.c`) and copies
  `D0/D1` back. No translation-unit boundary, no dispatcher lookup at run time.
- **Trap path.** The translated code stores the id and a request flag in the context
  (`M68KState.ONCALL_ID/ONCALL_REQ`) and ends the unit; the dispatch loop serves the call between
  units from the saved registers (`a3d_oncall_trap()`). A boot option selects it; the 3D engine's
  `HELLO` reports which path served the call.

The native code runs on the same Cortex-A9 core as the JIT, with the full NEON register file.

### 5.4 Id ranges in use

| Range | Service | Ids |
|---|---|---|
| `$0300-$033F` | BigMig 3Dx2 (the guest's Warp3D and MiniGL libraries) | `$0300` HELLO, `$0301` CTX_CREATE, `$0302` CTX_DESTROY, `$0303` TARGET, `$0304` TEX_ALLOC, `$0305` TEX_UPDATE, `$0306` TEX_FREE, `$0307` DRAW, `$0308` CLEAR, `$0309` FLUSH, `$030A` WAIT, `$030B` FRAME, `$030C` ZSPAN, `$030D` STATS, `$030E` UNLOCK |
| | the GL core inside it | `$0310` MGL_TRIS, `$0311` MGLC_HELLO, `$0312` MGLC_CTX, `$0313` MGLC_SUBMIT, `$0314` MGLC_QUERY |

Every other id is unassigned. Discovery inside the 3D range: `HELLO` with `D0` = the caller's ABI
version (`A3D_ABI_VERSION` = 1) returns the engine version in `D0` (0 = absent or mismatch) and a
capability word in `D1` (backend, raster units, heap size, trap path, MiniGL core); `MGLC_HELLO`
returns `D1` = the GL core's version.

MHI does not use `ONCALL`. `mhibigmig.library` talks to the firmware through a 256-byte mailbox at
offset `$1C00` of the ZZ9000 card's register window (`src/de10nano/mhi/mhi_mailbox.h`: magic
`'MHI1'`, protocol version 2, commands ALLOC/FREE/QUEUE/PLAY/STOP/PAUSE/SETVOL/SETPAN, a 16-entry
buffer ring). Decoding runs natively on the ARM side, with OMMX on or off.

## 6. Status against the original plan

| Phase | Plan | Status |
|---|---|---|
| 0 | prove the big-endian lane model | done |
| 1 | NEON encoders (including `d16-d31` for the VFP forms), context state, fast-bank cache, decode skeleton, compile switch | done |
| 2 | executable reference model of the semantics, anchored on the PRM's worked examples and RiVA's probe | done |
| 3 | the opcodes RiVA uses | done |
| 4 | RiVA on hardware, AMMX- and OMMX-block builds | done; faster than its 68k build |
| 5 | the rest of the AMMX table; `OIDENT`; the first native addendum | AMMX table done except `LOADI/STOREI`, `TEX`, `UNPACK1632` and the `Bn` modes; `OIDENT` done; native: the `.L` members of `$40-$5B` |
| 6 | run-time switch in the OSD; macro headers `ommx.i` / `ommx.h` / `ammx_compat.i` | switch done; headers **planned** |
| 7 | AArch64 back end (Cortex-A72/A76) | **planned** (only stubs today) |
| - | per-task register banks (not in the first plan) | done |
| - | `ONCALL` (reserved in the first plan) | done, for the 3D engine |

**Planned (proposal; no opcode numbers assigned yet):**

| Group | Candidates |
|---|---|
| integer | the remaining sizes of `$40-$5B`; signed saturating add/sub; `.L` min/max/compare; `OMADD` (pairwise multiply-add W to L); `OPACKSS` (signed saturating pack) |
| shifts | per-lane shifts by immediate and by vector, narrowing shifts, shift-and-insert (AMMX has only the 64-bit `LSLQ/LSRQ`) |
| video | absolute difference and sum of absolute differences (`OABD`, `OSADA`) |
| rearrange | `OZIP`, `OUZP`, `OREV`; load-and-splat `OLDR` |
| float32 | 2 x f32 add/sub/mul, multiply-accumulate unfused by definition, min/max, compares, reciprocal and square-root estimates with refinement steps, int/float conversion; ARMv7 Advanced SIMD model (flush-to-zero, round-to-nearest, default NaN); fused MAC and divide/square root only behind `OIDENT` bits 3 and 4 |
| width | 128-bit operations on aligned register pairs (`OIDENT` bit 6) |

Opcode numbers for the planned groups are not fixed yet and are open to agreement with other
implementations. What a binary may rely on today: the encodings, the semantics above,
`OIDENT`, Line-F for anything unimplemented, and per-task register state within the limits of
section 4.

## 7. Proposal: a shared ARM-call interface

**This section is a proposal, not an implemented feature.** Several Amiga accelerators now let 68k
code reach ARM code: BigMig with `ONCALL`; the ZZ9000 card, where XACP (eXtended ARM Coprocessor
Protocol, by Xanxi) has the 68k request services from the card's Cortex-A9 through card registers
and shared memory; and the Z3660 by shanshe (Double H Tech), a Zynq-7020 (dual Cortex-A9)
accelerator for the A3000/A4000. A common interface would let one guest library serve all of them.

| Element | Proposal |
|---|---|
| Id space | 16-bit service ids allocated in ranges per service family through a public registry; a small range reserved for the interface itself (discovery, versions); BigMig's `$0300-$033F` kept as the 3D range |
| Discovery | `OIDENT` bit 5 on a JIT that implements `ONCALL`; per service, a `HELLO` entry at the start of its range (`D0` = caller's ABI version; returns implementation version, 0 = absent, and a capability word in `D1`), as the 3D range already does; unknown ids answer with an error, never a trap |
| Convention | as section 5.2: `D0/D1/A0/A1` in, `D0/D1` out, the rest preserved; big-endian argument blocks at `A1`, never retained; bounded, synchronous entries |
| Versioning | one ABI version per service; additive changes only within a version; a new range or version for anything else |
| Transport | `ONCALL` where the CPU is a JIT on the ARM (BigMig, and any ARM-hosted JIT that adopts it); on a card or with a real 68k, a guest-side Line-F handler can forward the same id and registers to the card's register or mailbox interface, so one binary runs on both |

Nothing here changes the current encoding; it would standardise the id space and the discovery
and versioning rules around it.
