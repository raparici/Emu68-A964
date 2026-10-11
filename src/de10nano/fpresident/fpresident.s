; Copyright © 2026 Ruben Aparicio (@raparici)
; SPDX-License-Identifier: MPL-2.0
;
; The FPU math resident: math library entry points patched to run on the 68882.

	machine	68040
	fpu	1

LVO_OpenLibrary		equ	-552
LVO_CloseLibrary	equ	-414
LVO_SetFunction		equ	-420
LVO_Forbid		equ	-132
LVO_Permit		equ	-138
LVO_AllocMem		equ	-198
LVO_CacheClearU		equ	-636
LVO_MakeLibrary		equ	-84
LVO_AddLibrary		equ	-396		;   ... and add it to the resident LibList
LVO_AddDevice		equ	-432
LVO_AddResource		equ	-486

; ---- struct Library offsets + node type (standard AmigaOS exec/libraries.i) ----------
LN_TYPE			equ	8		; struct Node.ln_Type (UBYTE)
LN_PRI			equ	9		; struct Node.ln_Pri  (BYTE)
LN_NAME			equ	10		; struct Node.ln_Name (APTR)
LIB_FLAGS		equ	14
LIB_VERSION		equ	20
LIB_REVISION		equ	22
LIB_IDSTRING		equ	24
LIB_OPENCNT		equ	32
LIB_SIZE		equ	34
NT_LIBRARY		equ	9
EXEC_AttnFlags		equ	$128
SHIM_VERSION		equ	45
SHIM_REVISION		equ	1
						;   callable features, so the number only gates the open.

; ---- DiagArea / autoconfig constants ------------------------------------------------
DAC_CONFIGTIME		equ	$40		; run da_DiagPoint at ConfigChain time
DAC_BYTEWIDE		equ	$00		; the mapped ROM is normal memory: byte-wide
DAC_BINDTIME		equ	$80
DAC_WORDWIDE		equ	$10		; KS reads the DiagArea 16-bit-wide (bigmigHD: the Z3 ROM window needs it)
MEMF_PUBLIC		equ	$00000001
MEMF_CLEAR		equ	$00010000

LVO_Atan	equ	-30
LVO_Sin		equ	-36
LVO_Cos		equ	-42
LVO_Tan		equ	-48
LVO_Sinh	equ	-60
LVO_Cosh	equ	-66
LVO_Tanh	equ	-72
LVO_Exp		equ	-78
LVO_Log		equ	-84
LVO_Sqrt	equ	-96
LVO_Asin	equ	-114
LVO_Acos	equ	-120
LVO_Log10	equ	-126
LVO_Sincos	equ	-54			; *trans: sin in D0/D1, cos at (A0)
LVO_Pow		equ	-90			; *trans: parm(D0/D1) ^ power(D2/D3)
LVO_Fix		equ	-30			; *bas: double->long (trunc)
LVO_Flt		equ	-36			; *bas: long->double
LVO_Abs		equ	-54			; *bas
LVO_Neg		equ	-60			; *bas
LVO_Add		equ	-66
LVO_Sub		equ	-72
LVO_Mul		equ	-78
LVO_Div		equ	-84
LVO_Floor	equ	-90			; *bas layout
LVO_Ceil	equ	-96			; *bas layout
LVO_Tieee	equ	-102			; mathtrans: SPTieee - FFP -> IEEE single (== ffp2single)
LVO_Fieee	equ	-108			; mathtrans: SPFieee - IEEE single -> FFP (== single2ffp)

; ---- M68881 opmodes (reg-to-reg, fp0->fp0 => opcode word $F200, ext word = opmode) ----
OP_FSIN		equ	$0E
OP_FCOS		equ	$1D
OP_FTAN		equ	$0F
OP_FATAN	equ	$0A
OP_FASIN	equ	$0C
OP_FACOS	equ	$1C
OP_FETOX	equ	$10
OP_FLOGN	equ	$14
OP_FLOG10	equ	$15
OP_FSQRT	equ	$04
OP_FSINH	equ	$02
OP_FCOSH	equ	$19
OP_FTANH	equ	$09

ROMBASE:
	dc.b	$90,$00, $60,$00, $f0,$00, $d0,$00
	dc.b	$a0,$00, $f0,$00, $f0,$00, $f0,$00	; er_Flags / reserved
	dc.b	$90,$00, $d0,$00, $90,$00, $20,$00	; er_Manufacturer 0x626D ("bm")  ($10..$16)
	dc.b	$b0,$00, $90,$00, $a0,$00, $f0,$00	; er_SerialNumber hi  ($18..$1e)  -> "FP"
	dc.b	$a0,$00, $d0,$00, $a0,$00, $c0,$00	; er_SerialNumber lo  ($20..$26)  -> "RS"
	dc.b	$f0,$00, $f0,$00, $70,$00, $f0,$00
	dc.b	$f0,$00, $f0,$00, $f0,$00, $f0,$00	; $30..$3e
	dc.b	$f0,$00, $f0,$00, $f0,$00, $f0,$00
	dc.b	$f0,$00, $f0,$00, $f0,$00, $f0,$00	; $48..$4e
	dc.b	$f0,$00, $f0,$00, $f0,$00, $f0,$00	; $50..$56
	dc.b	$f0,$00, $f0,$00, $f0,$00, $f0,$00	; $58..$5e
	dcb.b	$20,$00					; $60..$7f padding (all-0 high nibble region)

	dcb.b	$80-(*-ROMBASE),0			; pad to exactly offset $80
DiagArea:
	dc.b	DAC_BINDTIME|DAC_WORDWIDE
	dc.b	0					; da_Flags
	dc.w	DiagCopyEnd-DiagArea			; da_Size - copies DiagArea+DiagPoint+romtag+strings
	dc.w	DiagPoint-DiagArea
	dc.w	BootPoint-DiagArea
	dc.w	ResName-DiagArea			; da_Name
	dc.w	0					; da_Reserved01
	dc.w	0					; da_Reserved02

DiagPoint:
	ifd	FPRES_DIAGPROBE
.dphang:	bra	.dphang
	endc
	movem.l	a1/a4,-(sp)
	lea	RomTag-DiagArea(a2),a1
	move.l	a1,2(a1)
	lea	RomEnd-DiagArea(a2),a4
	move.l	a4,6(a1)
	lea	ResName-DiagArea(a2),a4
	move.l	a4,14(a1)
	lea	ResId-DiagArea(a2),a4
	move.l	a4,18(a1)
	lea	ResInit(a0),a4
	move.l	a4,22(a1)
	movem.l	(sp)+,a1/a4
	moveq	#1,d0				; DiagPoint success
	rts

BootPoint:
	rts

	cnop	0,4
RomTag:
	dc.w	$4AFC
	dc.l	RomTag
	dc.l	RomEnd
	dc.b	$01
	dc.b	1				; rt_Version
	dc.b	$09
	dc.b	0				; rt_Pri
	dc.l	ResName
	dc.l	ResId
	dc.l	ResInit
ResName:	dc.b	"bigmig.fpresident",0
ResId:		dc.b	"bigmig.fpresident 1.8 (2026-10-04)",13,10,0
	cnop	0,2
RomEnd:
DiagCopyEnd:

	cnop	0,4
ResInit:
	ifd	FPRES_RESINITPROBE
.rihang:	bra	.rihang
	endc
	movem.l	d1-d7/a0-a6,-(sp)		; preserve a6 too
	movea.l	$4.w,a6
	ifd	FPRES_NOPATCH
	moveq	#1,d0
	movem.l	(sp)+,d1-d7/a0-a6
	rts
	endc

	lea	PayloadStart(pc),a5
	move.l	#PAYLOAD_ALLOC,d0
	move.l	#MEMF_PUBLIC|MEMF_CLEAR,d1	;   which only exists in RAM (cleared), so the ROM keeps its size
	jsr	LVO_AllocMem(a6)
	tst.l	d0
	beq	.fail
	movea.l	d0,a4
	move.l	a4,a1				; keep for offset math
	move.l	#PAYLOAD_LEN-1,d1
.cpy:	move.b	(a5)+,(a1)+
	dbra	d1,.cpy

	move.l	a6,(P_EXECBASE-PayloadStart)(a4)	; stash ExecBase in the payload (offset 0, not abs label)
	jsr	LVO_CacheClearU(a6)		; flush the just-copied payload code before it runs (defensive:

	ifnd	FPRES_NOHOOK
	movea.l	a6,a1
	move.l	#LVO_OpenLibrary,a0
	lea	HookOpen-PayloadStart(a4),a2
	move.l	a2,d0
	jsr	LVO_SetFunction(a6)
	move.l	d0,(P_ORIGOPEN-PayloadStart)(a4)
	endc

	; Directly patch any target library ALREADY open (ROM libs: mathffp / mathieeesingbas)
	ifnd	FPRES_NOPATCHLOOP
	lea	TargetTable-PayloadStart(a4),a3
.dloop:	move.w	(a3),d2				; name offset (rel PayloadStart); 0 = end
	beq	.done
	lea	0(a4,d2.w),a1
	moveq	#0,d0				; version 0 = any
	jsr	LVO_OpenLibrary(a6)
	tst.l	d0
	beq	.skip
	movea.l	d0,a1
	bsr	PatchCopy
	; keep the library open so the patch persists
.skip:	bsr	AdvanceEntry			; a3 -> next table entry
	bra	.dloop
	endc
.done:
	ifd	FPRES_SELFTEST
STCALL2	macro					; \1=stub \2=hi \3=lo  (IEEE double arg)
	lea	(\1-PayloadStart)(a4),a0
	move.l	#\2,d0
	move.l	#\3,d1
	jsr	(a0)
	endm
	; -- range reduction stress: sin/cos/tan at large args (1e6/1e10/1e15/1e300) --
	STCALL2	dp_sin,$412E8480,$00000000	; 1e6
	STCALL2	dp_sin,$4202A05F,$20000000	; 1e10
	STCALL2	dp_sin,$430C6BF5,$26340000	; 1e15
	STCALL2	dp_sin,$7E37E43C,$8800759C	; 1e300
	STCALL2	dp_cos,$4202A05F,$20000000	; 1e10
	STCALL2	dp_cos,$430C6BF5,$26340000	; 1e15
	STCALL2	dp_cos,$7E37E43C,$8800759C	; 1e300
	STCALL2	dp_tan,$4202A05F,$20000000	; 1e10
	STCALL2	dp_tan,$430C6BF5,$26340000	; 1e15
	STCALL2	dp_tan,$7E37E43C,$8800759C	; 1e300
	; -- overflow stress: exp/sinh/cosh at +-700 --
	STCALL2	dp_exp,$4085E000,$00000000	; 700
	STCALL2	dp_exp,$C085E000,$00000000	; -700
	STCALL2	dp_sinh,$4085E000,$00000000	; 700
	STCALL2	dp_sinh,$C085E000,$00000000	; -700
	STCALL2	dp_cosh,$4085E000,$00000000	; 700
	STCALL2	dp_tanh,$4085E000,$00000000	; 700
	; -- log domain: tiny / huge --
	STCALL2	dp_log,$01A56E1F,$C2F8F359	; 1e-300
	STCALL2	dp_log,$7E37E43C,$8800759C	; 1e300
	STCALL2	dp_log10,$01A56E1F,$C2F8F359	; 1e-300
	STCALL2	dp_log10,$7E37E43C,$8800759C	; 1e300
	; -- inverse trig near +-1 domain edge, atan at large --
	STCALL2	dp_asin,$3FEFFFFF,$FFFFFFFF	; ~ 0.9999999999999999
	STCALL2	dp_acos,$BFEFFFFF,$FFFFFFFF	; ~-0.9999999999999999
	STCALL2	dp_atan,$4202A05F,$20000000	; 1e10
	STCALL2	dp_atan,$C202A05F,$20000000	; -1e10
	; -- zero + negative edges, sqrt huge --
	STCALL2	dp_sin,$00000000,$00000000	; 0.0
	STCALL2	dp_tan,$00000000,$00000000	; 0.0
	STCALL2	dp_exp,$00000000,$00000000	; 0.0
	STCALL2	dp_sqrt,$7E37E43C,$8800759C	; 1e300
	STCALL2	dp_atan,$00000000,$00000000	; 0.0
	; -- re-confirm the whole set still passes at the benign 1.0 --
	STCALL2	dp_sin,$3FF00000,$00000000
	STCALL2	dp_cos,$3FF00000,$00000000
	STCALL2	dp_tan,$3FF00000,$00000000
	STCALL2	dp_atan,$3FF00000,$00000000
	STCALL2	dp_asin,$3FF00000,$00000000
	STCALL2	dp_acos,$3FF00000,$00000000
	STCALL2	dp_exp,$3FF00000,$00000000
	STCALL2	dp_log,$3FF00000,$00000000
	STCALL2	dp_log10,$3FF00000,$00000000
	STCALL2	dp_sqrt,$3FF00000,$00000000
	STCALL2	dp_sinh,$3FF00000,$00000000
	STCALL2	dp_cosh,$3FF00000,$00000000
	STCALL2	dp_tanh,$3FF00000,$00000000
	lea	(dp_pow-PayloadStart)(a4),a0
	move.l	#$40000000,d0			; base 2.0 hi
	moveq	#0,d1
	move.l	#$40240000,d2			; power 10.0 hi
	moveq	#0,d3
	jsr	(a0)
	lea	(dp_sincos-PayloadStart)(a4),a1
	subq.l	#8,sp				; 8-byte cos scratch on stack
	move.l	sp,a0
	move.l	#$3FF00000,d0			; angle 1.0 hi
	moveq	#0,d1
	jsr	(a1)
	addq.l	#8,sp				; drop cos scratch
	lea	(dp_add-PayloadStart)(a4),a0
	move.l	#$3FF00000,d0
	moveq	#0,d1
	move.l	#$40000000,d2
	moveq	#0,d3
	jsr	(a0)
	lea	(dp_div-PayloadStart)(a4),a0
	move.l	#$40240000,d0
	moveq	#0,d1
	move.l	#$40100000,d2
	moveq	#0,d3
	jsr	(a0)
	lea	(dp_fix-PayloadStart)(a4),a0
	move.l	#$400E0000,d0
	moveq	#0,d1
	jsr	(a0)
	lea	(dp_flt-PayloadStart)(a4),a0
	moveq	#5,d0
	jsr	(a0)
	lea	(mff_add-PayloadStart)(a4),a0
	move.l	#$80000041,d0			; FFP 1.0
	move.l	#$80000042,d1			; FFP 2.0
	jsr	(a0)
	lea	(mff_mul-PayloadStart)(a4),a0
	move.l	#$C0000042,d0			; FFP 3.0
	move.l	#$80000043,d1			; FFP 4.0
	jsr	(a0)
	lea	(mff_fix-PayloadStart)(a4),a0
	move.l	#$F0000042,d0			; FFP 3.75
	jsr	(a0)
	lea	(mff_flt-PayloadStart)(a4),a0
	moveq	#5,d0
	jsr	(a0)
	lea	(mt_tieee-PayloadStart)(a4),a0
	move.l	#$80000041,d0			; FFP 1.0
	jsr	(a0)
	lea	(mt_fieee-PayloadStart)(a4),a0
	jsr	(a0)
	cmp.l	#$80000041,d0
	bne	.st_bad
	lea	(mt_sqrt-PayloadStart)(a4),a0
	move.l	#$80000043,d0			; FFP 4.0
	jsr	(a0)
	cmp.l	#$80000042,d0			; FFP 2.0
	bne	.st_bad
	lea	(mt_exp-PayloadStart)(a4),a0
	moveq	#0,d0				; FFP 0.0
	jsr	(a0)
	cmp.l	#$80000041,d0			; FFP 1.0
	bne	.st_bad
	lea	(mt_log-PayloadStart)(a4),a0
	move.l	#$80000041,d0			; FFP 1.0
	jsr	(a0)
	tst.l	d0				; FFP 0.0
	bne	.st_bad
	lea	(mt_sin-PayloadStart)(a4),a0
	moveq	#0,d0				; FFP 0.0
	jsr	(a0)
	tst.l	d0
	bne	.st_bad
	lea	(mt_cos-PayloadStart)(a4),a0
	moveq	#0,d0				; FFP 0.0
	jsr	(a0)
	cmp.l	#$80000041,d0			; FFP 1.0
	bne	.st_bad
	bra	.st_ok				; all mathtrans checks passed -> skip the park
.st_bad:
	bra	.st_bad				; a WRONG FFP result parks HERE (flat HEALTH proves the miss)
.st_ok:
	ifd	FPRES_SELFTEST_PROVE
.sthang:	bra	.sthang
	endc
	endc
	ifnd	FPRES_NOSHIMS
	bsr	InstallShims			; option ii: pre-populate 68040/68060.library
	endc
	moveq	#1,d0				; success
	movem.l	(sp)+,d1-d7/a0-a6
	rts
.fail:
	moveq	#0,d0
	movem.l	(sp)+,d1-d7/a0-a6
	rts

InstallShims:
	movem.l	d2-d7/a2-a3,-(sp)
	; build the absolute MakeLibrary vector table in the PERSISTENT payload (P_VECTBL)
	lea	(P_VECTBL-PayloadStart)(a4),a2
	move.l	a4,d0
	add.l	#libOpen-PayloadStart,d0
	move.l	d0,(a2)+
	move.l	a4,d0
	add.l	#libClose-PayloadStart,d0
	move.l	d0,(a2)+
	move.l	a4,d0
	add.l	#libExpunge-PayloadStart,d0
	move.l	d0,(a2)+
	move.l	a4,d0
	add.l	#libExtFunc-PayloadStart,d0
	move.l	d0,(a2)+
	move.l	#-1,(a2)+			; MakeLibrary vector-table terminator
	; which model(s)?  Read the guest's own AttnFlags - the same signal SetPatch keys on.
	moveq	#0,d7
	move.w	EXEC_AttnFlags(a6),d7
	btst	#3,d7				; AFB_68040 -> install 68040.library
	beq	.n40
	lea	(n_040-PayloadStart)(a4),a1	; name
	lea	(id_040-PayloadStart)(a4),a3	; id string
	bsr	AddShim
.n40:
	movem.l	(sp)+,d2-d7/a2-a3
	rts

AddShim:
	movem.l	d2-d4/a2/a5,-(sp)
	movea.l	a1,a5
	move.l	a3,d4
	moveq	#0,d0				; version 0 = any
	jsr	LVO_OpenLibrary(a6)
	tst.l	d0
	beq	.mk
	movea.l	d0,a1				; already present -> close and leave it be
	jsr	LVO_CloseLibrary(a6)
	bra	.adone
.mk:
	lea	(P_VECTBL-PayloadStart)(a4),a0
	suba.l	a1,a1
	suba.l	a2,a2
	moveq	#LIB_SIZE,d0
	moveq	#0,d1
	jsr	LVO_MakeLibrary(a6)
	tst.l	d0
	beq	.adone
	movea.l	d0,a1
	move.b	#NT_LIBRARY,LN_TYPE(a1)
	clr.b	LN_PRI(a1)
	move.l	a5,LN_NAME(a1)
	move.w	#SHIM_VERSION,LIB_VERSION(a1)
	move.w	#SHIM_REVISION,LIB_REVISION(a1)
	move.l	d4,LIB_IDSTRING(a1)
	jsr	LVO_AddLibrary(a6)
.adone:
	movem.l	(sp)+,d2-d4/a2/a5
	rts

	cnop	0,4
PayloadStart:
P_EXECBASE:	dc.l	0			; +0   ExecBase (filled by DiagPoint)
P_ORIGOPEN:	dc.l	0			; +4   real OpenLibrary (filled by DiagPoint)
P_NCOPIES:	dc.l	0
						;      P_PATCHED lived here -- see PatchCopy for why it had to go
P_VECTBL:	dcb.l	5,0			; +12  runtime-built abs MakeLibrary table [O,C,E,X,-1]
P_STRIPPED:	dc.l	0

HookOpen:
	ifd	FPRES_HOOK_PASSTHRU
	move.l	P_ORIGOPEN(pc),a0
	jmp	(a0)				; (no name check, no patch) - isolates trampoline vs body
	endc
	movem.l	d2/a2-a4,-(sp)
	move.l	P_ORIGOPEN(pc),a0
	jsr	(a0)
	bsr	Strip060
	tst.l	d0
	beq.s	.hout
	movea.l	d0,a2
	move.l	LN_NAME(a2),d2			; its own name
	beq.s	.hret
	lea	PayloadStart(pc),a4
	lea	TargetTable-PayloadStart(a4),a3
.hloop:	move.w	(a3),d0				; name offset; 0 = end of the table
	beq.s	.hret
	lea	0(a4,d0.w),a0
	movea.l	d2,a1
	bsr	StrEq
	tst.l	d0
	bne.s	.hmatch
	bsr	AdvanceEntry
	bra.s	.hloop
.hmatch:
	movea.l	a2,a1
	bsr	PatchCopy			; once per copy; preserves every register
.hret:	move.l	a2,d0
.hout:	movem.l	(sp)+,d2/a2-a4
	rts

PatchCopy:
	movem.l	d0-d1/a0-a2/a6,-(sp)
	movea.l	a1,a2
	jsr	LVO_Forbid(a6)			; one task at a time through the record
	lea	P_COPIES(a4),a0
	move.l	(P_NCOPIES-PayloadStart)(a4),d1
	bra.s	.pcnext
.pcscan:	cmpa.l	(a0)+,a2
	beq.s	.pcdone
.pcnext:	dbf	d1,.pcscan			; a0 ends on the first free slot
	cmp.l	#P_NCOPIES_MAX,(P_NCOPIES-PayloadStart)(a4)
	bhs.s	.pcdone				; the record is full: leave this copy alone
	move.l	a2,(a0)				; record it BEFORE opening it: the copy's own Open may open other
	addq.l	#1,(P_NCOPIES-PayloadStart)(a4)
	movea.l	a2,a6				; open it once more, for good: LIB_OPEN, version 0, under Forbid --
	moveq	#0,d0				;   what exec's OpenLibrary does once it has found the library
	jsr	-6(a6)
	movea.l	(P_EXECBASE-PayloadStart)(a4),a6
	tst.l	d0
	bne.s	.pcheld
	lea	P_COPIES(a4),a0			; the copy refused a second open: it can still go away, so its base
.pcfind:	cmpa.l	(a0)+,a2			;   names nothing -- burn its slot (0 matches no library), patch it anyway
	bne.s	.pcfind
	clr.l	-(a0)
.pcheld:	movea.l	a2,a1
	bsr	PatchOneLib			; SetFunction every vector of the entry (nests its own Forbid)
.pcdone:	jsr	LVO_Permit(a6)
	movem.l	(sp)+,d0-d1/a0-a2/a6
	rts

Strip060:
	ifd	FPRES_NOSTRIP060
	rts
	endc
	movem.l	d0-d3/a0-a3,-(sp)
	lea	PayloadStart(pc),a3
	cmp.l	#4,(P_STRIPPED-PayloadStart)(a3)
	bhs	.sdone
	lea	StripLVOs(pc),a2
.sloop:	move.w	(a2)+,d2
	beq	.sdone
	lea	0(a6,d2.w),a0			; a0 -> the JMP abs.l slot in exec's jump table
	cmp.w	#$4EF9,(a0)
	bne	.sloop
	movea.l	2(a0),a1
	cmp.l	#$2F3AFFFA,(a1)			; move.l -6(pc),-(a7)
	bne	.sloop
	cmp.l	#$2F094EAE,4(a1)			; move.l a1,-(a7) ; jsr ...
	bne	.sloop
	cmp.l	#$FD84225F,8(a1)			; ... -636(a6) = CacheClearU ; movea.l (a7)+,a1
	bne	.sloop
	cmp.w	#$4E75,12(a1)			; rts
	bne	.sloop
	move.l	-4(a1),d0			; the original function the wrapper jumps on to
	beq	.sloop
	movea.l	a6,a1
	move.w	d2,a0
	jsr	LVO_SetFunction(a6)		; Forbid, swap, SumLibrary, CacheClearU
	addq.l	#1,(P_STRIPPED-PayloadStart)(a3)
	bra	.sloop
.sdone:	movem.l	(sp)+,d0-d3/a0-a3
	rts
StripLVOs:	dc.w	LVO_AddLibrary,LVO_CloseLibrary,LVO_AddDevice,LVO_AddResource,0

PatchOneLib:
	movem.l	d2-d4/a1-a3/a5,-(sp)
	movea.l	a1,a5
	jsr	LVO_Forbid(a6)
	move.w	2(a3),d4			; nvec
	lea	4(a3),a2			; a2 -> first (lvo, stuboff) pair
.ploop:	subq.w	#1,d4
	bmi	.pdone
	move.w	(a2)+,d2
	move.w	(a2)+,d3			; stub offset (rel PayloadStart)
	movea.l	a5,a1
	move.w	d2,a0
	; compute stub abs = a4 + d3 ; put in d0
	move.l	a4,d0
	ext.l	d3
	add.l	d3,d0
	jsr	LVO_SetFunction(a6)
	bra	.ploop
.pdone:
	jsr	LVO_Permit(a6)
	jsr	LVO_CacheClearU(a6)
	movem.l	(sp)+,d2-d4/a1-a3/a5
	rts

; ---- AdvanceEntry: a3 -> next table entry (skip name-off + nvec + nvec*(lvo,stuboff)) --
AdvanceEntry:
	move.l	d0,-(sp)
	move.w	2(a3),d0			; nvec
	ext.l	d0
	lsl.l	#2,d0				; nvec*4
	addq.l	#4,d0				; + name-off word + nvec word
	adda.l	d0,a3
	move.l	(sp)+,d0
	rts

; ---- StrEq: case-sensitive C-string compare.  in a0,a1 ; out d0 = 1 if equal ----------
StrEq:
	move.l	a0,-(sp)
	move.l	a1,-(sp)
.seloop:
	move.b	(a0)+,d0
	cmp.b	(a1)+,d0
	bne	.sne
	tst.b	d0
	bne	.seloop
	moveq	#1,d0
	bra	.sedone
.sne:	moveq	#0,d0
.sedone:
	movea.l	(sp)+,a1
	movea.l	(sp)+,a0
	rts

libOpen:
	addq.w	#1,LIB_OPENCNT(a6)
	bclr	#3,LIB_FLAGS(a6)		; clear LIBF_DELEXP
	move.l	a6,d0
	rts
libClose:
	subq.w	#1,LIB_OPENCNT(a6)
	moveq	#0,d0				; never ask to be expunged (ROM-permanent)
	rts
libExpunge:
	moveq	#0,d0				; refuse to expunge; return 0 seglist
	rts
libExtFunc:
	moveq	#0,d0				; reserved vector
	rts

; --- double single-arg transcendental: opmode assembled as reg-to-reg fp0->fp0 -------
DPOP	macro
	move.l	d1,-(sp)
	move.l	d0,-(sp)
	dc.w	$F217,$5400|\1
	fmove.d	fp0,(sp)
	move.l	(sp)+,d0
	move.l	(sp)+,d1
	rts
	endm

; --- single single-arg transcendental -------------------------------------------------
SPOP	macro
	dc.w	$F200,$4400|\1
	fmove.s	fp0,d0
	rts
	endm

	cnop	0,4
; ---- mathieeedoubtrans stubs ----
dp_sin:		DPOP	OP_FSIN
dp_cos:		DPOP	OP_FCOS
dp_tan:		DPOP	OP_FTAN
dp_atan:	DPOP	OP_FATAN
dp_asin:	DPOP	OP_FASIN
dp_acos:	DPOP	OP_FACOS
dp_exp:		DPOP	OP_FETOX
dp_log:		DPOP	OP_FLOGN
dp_log10:	DPOP	OP_FLOG10
dp_sqrt:	DPOP	OP_FSQRT
dp_sinh:	DPOP	OP_FSINH
dp_cosh:	DPOP	OP_FCOSH
dp_tanh:	DPOP	OP_FTANH

; ---- mathieeesingtrans stubs ----
sp_sin:		SPOP	OP_FSIN
sp_cos:		SPOP	OP_FCOS
sp_tan:		SPOP	OP_FTAN
sp_atan:	SPOP	OP_FATAN
sp_asin:	SPOP	OP_FASIN
sp_acos:	SPOP	OP_FACOS
sp_exp:		SPOP	OP_FETOX
sp_log:		SPOP	OP_FLOGN
sp_log10:	SPOP	OP_FLOG10
sp_sqrt:	SPOP	OP_FSQRT
sp_sinh:	SPOP	OP_FSINH
sp_cosh:	SPOP	OP_FCOSH
sp_tanh:	SPOP	OP_FTANH

dp_floor:
	move.l	d1,-(sp)
	move.l	d0,-(sp)
	fmove.d	(sp),fp0
	fintrz.x fp0,fp1
	fcmp.x	fp1,fp0				; CC from (x - trunc)
	fbge.w	.dfl
	fsub.s	#1.0,fp1			; x < trunc -> floor = trunc - 1
.dfl:	fmove.d	fp1,(sp)
	move.l	(sp)+,d0
	move.l	(sp)+,d1
	rts
dp_ceil:
	move.l	d1,-(sp)
	move.l	d0,-(sp)
	fmove.d	(sp),fp0
	fintrz.x fp0,fp1
	fcmp.x	fp1,fp0
	fble.w	.dcl
	fadd.s	#1.0,fp1			; x > trunc -> ceil = trunc + 1
.dcl:	fmove.d	fp1,(sp)
	move.l	(sp)+,d0
	move.l	(sp)+,d1
	rts
sp_floor:
	fmove.s	d0,fp0
	fintrz.x fp0,fp1
	fcmp.x	fp1,fp0
	fbge.w	.sfl
	fsub.s	#1.0,fp1
.sfl:	fmove.s	fp1,d0
	rts
sp_ceil:
	fmove.s	d0,fp0
	fintrz.x fp0,fp1
	fcmp.x	fp1,fp0
	fble.w	.scl
	fadd.s	#1.0,fp1
.scl:	fmove.s	fp1,d0
	rts

dp_pow:
	move.l	d1,-(sp)
	move.l	d0,-(sp)
	dc.w	$F217,$5400|OP_FLOGN
	addq.l	#8,sp
	move.l	d3,-(sp)
	move.l	d2,-(sp)
	fmul.d	(sp),fp0
	addq.l	#8,sp
	fmove.d	fp0,-(sp)			; store y*ln(x)
	dc.w	$F217,$5400|OP_FETOX
	fmove.d	fp0,(sp)
	move.l	(sp)+,d0
	move.l	(sp)+,d1
	rts
dp_sincos:
	move.l	a0,-(sp)			; save cos pointer
	move.l	d1,-(sp)
	move.l	d0,-(sp)			; angle at (sp) ; cosptr at 8(sp)
	dc.w	$F217,$5400|OP_FCOS
	move.l	8(sp),a1
	fmove.d	fp0,(a1)
	dc.w	$F217,$5400|OP_FSIN
	fmove.d	fp0,(sp)
	move.l	(sp)+,d0
	move.l	(sp)+,d1			; sin -> D0/D1
	addq.l	#4,sp				; drop saved cosptr
	rts
sp_pow:
	dc.w	$F200,$4400|OP_FLOGN		; flogn.s d0,fp0  -> fp0 = ln(base)
	fmul.s	d1,fp0
	fmove.d	fp0,-(sp)			; store as double
	dc.w	$F217,$5400|OP_FETOX
	addq.l	#8,sp
	fmove.s	fp0,d0
	rts
sp_sincos:
	dc.w	$F200,$4400|OP_FCOS		; fcos.s d0,fp0  -> fp0 = cos(angle)
	fmove.s	fp0,(a0)
	dc.w	$F200,$4400|OP_FSIN		; fsin.s d0,fp0  -> fp0 = sin(angle)
	fmove.s	fp0,d0				; sin -> D0
	rts

DBINOP	macro					; \1 = fadd|fsub|fmul|fdiv  (double, y \1 z)
	move.l	d1,-(sp)
	move.l	d0,-(sp)			; y at (sp)
	fmove.d	(sp),fp0
	addq.l	#8,sp
	move.l	d3,-(sp)
	move.l	d2,-(sp)			; z at (sp)
	\1.d	(sp),fp0
	addq.l	#8,sp
	fmove.d	fp0,-(sp)
	move.l	(sp)+,d0
	move.l	(sp)+,d1
	rts
	endm
SBINOP	macro					; \1 = fadd|fsub|fmul|fdiv  (single, y \1 z)
	fmove.s	d0,fp0
	\1.s	d1,fp0
	fmove.s	fp0,d0
	rts
	endm
dp_add:		DBINOP	fadd
dp_sub:		DBINOP	fsub
dp_mul:		DBINOP	fmul
dp_div:		DBINOP	fdiv
sp_add:		SBINOP	fadd
sp_sub:		SBINOP	fsub
sp_mul:		SBINOP	fmul
sp_div:		SBINOP	fdiv
dp_abs:	and.l	#$7FFFFFFF,d0			; |x|: clear sign (dbl bit 63 = D0 bit 31)
	rts
dp_neg:	bchg	#31,d0				; -x: flip sign
	rts
sp_abs:	and.l	#$7FFFFFFF,d0			; single |x|
	rts
sp_neg:	bchg	#31,d0
	rts
dp_fix:						; double -> long (truncate toward zero)
	move.l	d1,-(sp)
	move.l	d0,-(sp)
	fintrz.d (sp),fp0
	fmove.l	fp0,d0				; long -> D0
	addq.l	#8,sp
	rts
sp_fix:	fintrz.s d0,fp0				; single -> trunc
	fmove.l	fp0,d0
	rts
dp_flt:						; long D0 -> double
	fmove.l	d0,fp0
	fmove.d	fp0,-(sp)
	move.l	(sp)+,d0
	move.l	(sp)+,d1
	rts
sp_flt:	fmove.l	d0,fp0				; long -> single
	fmove.s	fp0,d0
	rts

; -- ffp2single: FFP in D0 -> IEEE single in D0.  Integer only.  Preserves all regs but D0. ----
ffp2single:
	tst.l	d0
	beq	.f2s_ret				; FFP 0 -> single 0 (bit-identical all-zero)
	movem.l	d1-d2,-(sp)
	move.l	d0,d1
	lsr.l	#8,d0
	and.l	#$00FFFFFF,d0			; keep the 24 mantissa bits (defensive)
	move.l	d1,d2
	and.l	#$0000007F,d2
	add.l	#61,d2				; +61 ; the leading 1 already in d0 gives the +1 -> +62
	swap	d2				; << 16 (high word was 0)
	lsl.l	#7,d2				; << 23 total -> (E_ffp+61)<<23
	add.l	d2,d0
	btst	#7,d1				; FFP sign (bit 7)?
	beq	.f2s_nosgn
	bset	#31,d0				; -> IEEE sign (bit 31)
.f2s_nosgn:
	movem.l	(sp)+,d1-d2
.f2s_ret:
	rts

single2ffp:
	movem.l	d1-d2,-(sp)
	move.l	d0,d1
	swap	d1				; low word = single bits 31..16
	lsr.w	#7,d1				; exp bits 30..23 -> bits 7..0 (sign lands at bit 8)
	and.l	#$000000FF,d1
	cmp.l	#62,d1				; E_ieee < 62 (incl 0/denormal) -> underflow
	blt	.s2f_zero
	cmp.l	#189,d1				; E_ieee > 189 (incl 255 Inf/NaN) -> overflow
	bgt	.s2f_sat
	move.l	d0,d2
	and.l	#$007FFFFF,d2			; frac
	or.l	#$00800000,d2
	lsl.l	#8,d2				; M << 8 -> bits 31..8
	sub.l	#62,d1
	or.l	d1,d2				; insert E_ffp into bits 6..0 (bit 7 still 0)
	tst.l	d0				; single sign (bit 31)?
	bpl	.s2f_pos
	bset	#7,d2				; negative -> FFP sign bit 7
.s2f_pos:
	move.l	d2,d0
	movem.l	(sp)+,d1-d2
	rts
.s2f_zero:
	moveq	#0,d0				; underflow / zero / denormal -> FFP 0
	movem.l	(sp)+,d1-d2
	rts
.s2f_sat:
	move.l	#$FFFFFF7F,d1			; +max FFP (M=0xFFFFFF, E=0x7F)
	tst.l	d0				; original single sign
	bpl	.s2f_satp
	bset	#7,d1				; -max FFP -> 0xFFFFFFFF
.s2f_satp:
	move.l	d1,d0
	movem.l	(sp)+,d1-d2
	rts

; -- FFP two-arg basic ops: y=D0, z=D1 -> D0 (mirrors mathieeesingbas operand order) --------
MFFBINOP macro					; \1 = fadd|fsub|fmul|fdiv  (FFP, y \1 z)
	move.l	d2,-(sp)				; preserve caller d2 (holds z across a conversion call)
	move.l	d1,d2
	bsr	ffp2single
	fmove.s	d0,fp0
	move.l	d2,d0
	bsr	ffp2single
	\1.s	d0,fp0
	fmove.s	fp0,d0
	bsr	single2ffp
	move.l	(sp)+,d2
	rts
	endm
mff_add:	MFFBINOP	fadd
mff_sub:	MFFBINOP	fsub
mff_mul:	MFFBINOP	fmul
mff_div:	MFFBINOP	fdiv

mff_abs:	bclr	#7,d0			; |x|: clear FFP sign bit 7 (abs(0)=0 stays 0)
	rts
mff_neg:	tst.l	d0			; -x: flip sign; neg(0) must stay canonical 0
	beq	.mn_ret				;   (bchg on 0x00000000 would yield 0x00000080)
	bchg	#7,d0
.mn_ret:	rts

mff_fix:					; FFP -> long (truncate toward zero)
	bsr	ffp2single
	fintrz.s d0,fp0
	fmove.l	fp0,d0
	rts
mff_flt:					; long D0 -> FFP
	fmove.l	d0,fp0
	fmove.s	fp0,d0
	bsr	single2ffp
	rts
mff_floor:					; largest integer <= x (FINTRZ + one-ulp, like sp_floor)
	bsr	ffp2single
	fmove.s	d0,fp0
	fintrz.x fp0,fp1
	fcmp.x	fp1,fp0				; CC from (x - trunc)
	fbge.w	.mfl
	fsub.s	#1.0,fp1				; x < trunc -> floor = trunc - 1
.mfl:	fmove.s	fp1,d0
	bsr	single2ffp
	rts
mff_ceil:					; smallest integer >= x
	bsr	ffp2single
	fmove.s	d0,fp0
	fintrz.x fp0,fp1
	fcmp.x	fp1,fp0
	fble.w	.mcl
	fadd.s	#1.0,fp1				; x > trunc -> ceil = trunc + 1
.mcl:	fmove.s	fp1,d0
	bsr	single2ffp
	rts

MTOP	macro					; \1 = M68881 opmode ; FFP arg D0 -> op -> FFP result D0
	bsr	ffp2single
	dc.w	$F200,$4400|\1			; <op>.s d0,fp0  (data single src; SAME word the sp_* stubs use)
	fmove.s	fp0,d0
	bsr	single2ffp
	rts
	endm
	cnop	0,4
mt_atan:	MTOP	OP_FATAN		; SPAtan  -30
mt_sin:		MTOP	OP_FSIN			; SPSin   -36
mt_cos:		MTOP	OP_FCOS			; SPCos   -42
mt_tan:		MTOP	OP_FTAN			; SPTan   -48
mt_sinh:	MTOP	OP_FSINH		; SPSinh  -60
mt_cosh:	MTOP	OP_FCOSH		; SPCosh  -66
mt_tanh:	MTOP	OP_FTANH		; SPTanh  -72
mt_exp:		MTOP	OP_FETOX		; SPExp   -78
mt_log:		MTOP	OP_FLOGN		; SPLog   -84 (natural log)
mt_sqrt:	MTOP	OP_FSQRT		; SPSqrt  -96
mt_asin:	MTOP	OP_FASIN		; SPAsin  -114
mt_acos:	MTOP	OP_FACOS		; SPAcos  -120
mt_log10:	MTOP	OP_FLOG10		; SPLog10 -126

mt_pow:
	move.l	d2,-(sp)			; preserve caller d2 (holds power across a conversion)
	move.l	d1,d2
	bsr	ffp2single
	dc.w	$F200,$4400|OP_FLOGN		; flogn.s d0,fp0 -> fp0 = ln(base)
	move.l	d2,d0
	bsr	ffp2single
	fmul.s	d0,fp0
	fmove.d	fp0,-(sp)			; stage the product as a double (memory src for fetox)
	dc.w	$F217,$5400|OP_FETOX
	addq.l	#8,sp
	fmove.s	fp0,d0
	bsr	single2ffp
	move.l	(sp)+,d2			; restore caller d2
	rts

mt_sincos:
	bsr	ffp2single
	move.l	d0,-(sp)			; stash single(angle) for the sin pass
	dc.w	$F200,$4400|OP_FCOS		; fcos.s d0,fp0 -> fp0 = cos(angle)
	fmove.s	fp0,d0
	bsr	single2ffp
	move.l	d0,(a0)
	move.l	(sp)+,d0			; recover single(angle)
	dc.w	$F200,$4400|OP_FSIN		; fsin.s d0,fp0 -> fp0 = sin(angle)
	fmove.s	fp0,d0
	bsr	single2ffp
	rts

mt_tieee:	bsr	ffp2single		; SPTieee -102: FFP -> IEEE single (ffp2single verbatim)
	rts
mt_fieee:	bsr	single2ffp		; SPFieee -108: IEEE single -> FFP (single2ffp verbatim)
	rts

TENT	macro					; \1 = lvo   \2 = stub label
	dc.w	\1, \2-PayloadStart
	endm

	cnop	0,2
TargetTable:
	; --- mathieeedoubtrans.library ---
	dc.w	n_dtrans-PayloadStart
	dc.w	15
	TENT	LVO_Sin,dp_sin
	TENT	LVO_Cos,dp_cos
	TENT	LVO_Tan,dp_tan
	TENT	LVO_Atan,dp_atan
	TENT	LVO_Asin,dp_asin
	TENT	LVO_Acos,dp_acos
	TENT	LVO_Exp,dp_exp
	TENT	LVO_Log,dp_log
	TENT	LVO_Log10,dp_log10
	TENT	LVO_Sqrt,dp_sqrt
	TENT	LVO_Sinh,dp_sinh
	TENT	LVO_Cosh,dp_cosh
	TENT	LVO_Tanh,dp_tanh
	TENT	LVO_Sincos,dp_sincos
	TENT	LVO_Pow,dp_pow
	; --- mathieeesingtrans.library ---
	dc.w	n_strans-PayloadStart
	dc.w	15
	TENT	LVO_Sin,sp_sin
	TENT	LVO_Cos,sp_cos
	TENT	LVO_Tan,sp_tan
	TENT	LVO_Atan,sp_atan
	TENT	LVO_Asin,sp_asin
	TENT	LVO_Acos,sp_acos
	TENT	LVO_Exp,sp_exp
	TENT	LVO_Log,sp_log
	TENT	LVO_Log10,sp_log10
	TENT	LVO_Sqrt,sp_sqrt
	TENT	LVO_Sinh,sp_sinh
	TENT	LVO_Cosh,sp_cosh
	TENT	LVO_Tanh,sp_tanh
	TENT	LVO_Sincos,sp_sincos
	TENT	LVO_Pow,sp_pow
	ifnd	FPRES_NOBAS
	; --- mathieeedoubbas.library (basic ops) ---
	dc.w	n_dbas-PayloadStart
	dc.w	10
	TENT	LVO_Floor,dp_floor
	TENT	LVO_Ceil,dp_ceil
	TENT	LVO_Fix,dp_fix
	TENT	LVO_Flt,dp_flt
	TENT	LVO_Abs,dp_abs
	TENT	LVO_Neg,dp_neg
	TENT	LVO_Add,dp_add
	TENT	LVO_Sub,dp_sub
	TENT	LVO_Mul,dp_mul
	TENT	LVO_Div,dp_div
	; --- mathieeesingbas.library (basic ops) ---
	dc.w	n_sbas-PayloadStart
	dc.w	10
	TENT	LVO_Floor,sp_floor
	TENT	LVO_Ceil,sp_ceil
	TENT	LVO_Fix,sp_fix
	TENT	LVO_Flt,sp_flt
	TENT	LVO_Abs,sp_abs
	TENT	LVO_Neg,sp_neg
	TENT	LVO_Add,sp_add
	TENT	LVO_Sub,sp_sub
	TENT	LVO_Mul,sp_mul
	TENT	LVO_Div,sp_div
	endc
	ifd	FPRES_FFP
	; --- mathffp.library (Motorola FFP basic ops) ---
	dc.w	n_mathffp-PayloadStart
	dc.w	10
	TENT	LVO_Floor,mff_floor
	TENT	LVO_Ceil,mff_ceil
	TENT	LVO_Fix,mff_fix
	TENT	LVO_Flt,mff_flt
	TENT	LVO_Abs,mff_abs
	TENT	LVO_Neg,mff_neg
	TENT	LVO_Add,mff_add
	TENT	LVO_Sub,mff_sub
	TENT	LVO_Mul,mff_mul
	TENT	LVO_Div,mff_div
	endc
	ifnd	FPRES_NOTRANS
	; --- mathtrans.library (Motorola FFP transcendentals) ---
	dc.w	n_mathtrans-PayloadStart
	dc.w	17
	TENT	LVO_Atan,mt_atan
	TENT	LVO_Sin,mt_sin
	TENT	LVO_Cos,mt_cos
	TENT	LVO_Tan,mt_tan
	TENT	LVO_Sincos,mt_sincos
	TENT	LVO_Sinh,mt_sinh
	TENT	LVO_Cosh,mt_cosh
	TENT	LVO_Tanh,mt_tanh
	TENT	LVO_Exp,mt_exp
	TENT	LVO_Log,mt_log
	TENT	LVO_Pow,mt_pow
	TENT	LVO_Sqrt,mt_sqrt
	TENT	LVO_Tieee,mt_tieee
	TENT	LVO_Fieee,mt_fieee
	TENT	LVO_Asin,mt_asin
	TENT	LVO_Acos,mt_acos
	TENT	LVO_Log10,mt_log10
	endc
	dc.w	0				; end of table

n_dtrans:	dc.b	"mathieeedoubtrans.library",0
n_strans:	dc.b	"mathieeesingtrans.library",0
n_dbas:		dc.b	"mathieeedoubbas.library",0
n_sbas:		dc.b	"mathieeesingbas.library",0
n_mathffp:	dc.b	"mathffp.library",0
n_mathtrans:	dc.b	"mathtrans.library",0
n_040:		dc.b	"68040.library",0
id_040:		dc.b	"68040.library 45.1 (BigMig benign shim; no FPSP)",0
	cnop	0,4
PayloadEnd:
PAYLOAD_LEN	equ	PayloadEnd-PayloadStart
P_NCOPIES_MAX	equ	32
P_COPIES	equ	PAYLOAD_LEN
PAYLOAD_ALLOC	equ	PAYLOAD_LEN+4*P_NCOPIES_MAX

	cnop	0,2
DiagEnd:

ROM_IMAGE_SIZE	equ	3160
	ifnd	FPRES_SELFTEST
	dcb.b	ROM_IMAGE_SIZE-(*-ROMBASE),0
	endc
