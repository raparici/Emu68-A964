FORCE_COMMON_ALLOCATION

SECTIONS
{
    . = 0xff810000;

    __bootstrap_start = . ;

    .text :
    {
        *(.startup)
        PROVIDE(g_note_build_id = .);
        *(.note.gnu.build-id)
        *(EXCLUDE_FILE(*a3d_mglc.c.obj *mgl_geom.c.obj *mgl_state.c.obj *mgl_math.c.obj *mgl_rsplan.c.obj *mgl_abi5.c.obj *mgl_glu.c.obj *mgl_light.c.obj *mgl_glutq.c.obj *mgl_mtex.c.obj *mgl_readpx.c.obj *mgl_texds.c.obj *mgl_texgen.c.obj *a3d_r8.c.obj *zz_aprime.c.obj *zz_r9.c.obj *jit_r10c.c.obj *jit_r10a1.c.obj *a3d_heapb.c.obj) .text)
        *(EXCLUDE_FILE(*a3d_mglc.c.obj *mgl_geom.c.obj *mgl_state.c.obj *mgl_math.c.obj *mgl_rsplan.c.obj *mgl_abi5.c.obj *mgl_glu.c.obj *mgl_light.c.obj *mgl_glutq.c.obj *mgl_mtex.c.obj *mgl_readpx.c.obj *mgl_texds.c.obj *mgl_texgen.c.obj *a3d_mgl.c.obj *mgl_stage.c.obj *a3d_r8.c.obj *zz_aprime.c.obj *zz_r9.c.obj *jit_r10c.c.obj *jit_r10a1.c.obj *a3d_heapb.c.obj) .text.*)
        *(.stub)
        *(.gnu.warning)
        *(.gnu.linkonce.t.*)

        . = ALIGN(0x20);
        *(.vectors)

        *(.a3d_textend)
        *(.a3d_textend_rodata)
        *zz_aprime.c.obj(.text .text.* .rodata .rodata.*)
        *(.r9_textend)
        *(.r9_textend_rodata)
        *(.r10_textend)
        *(.r10_textend_rodata)
    }

    .rodata ALIGN(0x1000) :
    {
        *(EXCLUDE_FILE(*a3d_mglc.c.obj *mgl_geom.c.obj *mgl_state.c.obj *mgl_math.c.obj *mgl_rsplan.c.obj *mgl_abi5.c.obj *mgl_glu.c.obj *mgl_light.c.obj *mgl_glutq.c.obj *mgl_mtex.c.obj *mgl_readpx.c.obj *mgl_texds.c.obj *mgl_texgen.c.obj *a3d_r8.c.obj *zz_aprime.c.obj *zz_r9.c.obj *jit_r10c.c.obj *jit_r10a1.c.obj *a3d_heapb.c.obj) .rodata)
        *(EXCLUDE_FILE(*a3d_mglc.c.obj *mgl_geom.c.obj *mgl_state.c.obj *mgl_math.c.obj *mgl_rsplan.c.obj *mgl_abi5.c.obj *mgl_glu.c.obj *mgl_light.c.obj *mgl_glutq.c.obj *mgl_mtex.c.obj *mgl_readpx.c.obj *mgl_texds.c.obj *mgl_texgen.c.obj *a3d_mgl.c.obj *mgl_stage.c.obj *a3d_r8.c.obj *zz_aprime.c.obj *zz_r9.c.obj *jit_r10c.c.obj *jit_r10a1.c.obj *a3d_heapb.c.obj) .rodata.*)
        *(.gnu.linkonce.r.*)

        . = ALIGN(32);
        __boards_start = .;
        *(.boards.z2)
        *(.boards.z3)
        *(.boards.z9)
        *(.boards)
        LONG(0)
        __boards_end = .;
    }
    .rodata1 : { *(.rodata1) }

    .sdata2 : { *(.sdata2) *(.sdata2.*) *(.gnu.linkonce.s2.*) }
    .sbss2  : { *(.sbss2) *(.sbss2.*) *(.gnu.linkonce.sb2.*) }

    .data ALIGN(0x1000) :
    {
        *core.bin.o(.data)
        *(EXCLUDE_FILE(*a3d_mglc.c.obj *mgl_geom.c.obj *mgl_state.c.obj *mgl_math.c.obj *mgl_rsplan.c.obj *mgl_abi5.c.obj *mgl_glu.c.obj *mgl_light.c.obj *mgl_glutq.c.obj *mgl_mtex.c.obj *mgl_readpx.c.obj *mgl_texds.c.obj *mgl_texgen.c.obj *a3d_r8.c.obj *jit_r10c.c.obj *jit_r10a1.c.obj *a3d_heapb.c.obj) .data)
        *(EXCLUDE_FILE(*a3d_mglc.c.obj *mgl_geom.c.obj *mgl_state.c.obj *mgl_math.c.obj *mgl_rsplan.c.obj *mgl_abi5.c.obj *mgl_glu.c.obj *mgl_light.c.obj *mgl_glutq.c.obj *mgl_mtex.c.obj *mgl_readpx.c.obj *mgl_texds.c.obj *mgl_texgen.c.obj *a3d_r8.c.obj *jit_r10c.c.obj *jit_r10a1.c.obj *a3d_heapb.c.obj) .data.*)
        *(.gnu.linkonce.d.*)
    }

    .data1              : { *(.data1) }
    .eh_frame           : { KEEP (*(.eh_frame)) }
    .gcc_except_table   : { *(.gcc_except_table) }

    .a3d_late ALIGN(0x20) :
    {
        *(.a3d_late_text)
        *a3d_mgl.c.obj(.text .text.* .rodata .rodata.*)
        *mgl_stage.c.obj(.text .text.* .rodata .rodata.*)
        *(.a3d_late2_text)
    }

    .sdata ALIGN(0x1000) :
    {
        *(.sdata)
        *(.sdata.*)
        *(.gnu.linkonce.s.*)
    }

    .mmu ALIGN(0x4000) :
    {
        *(.mmu)
    }

    __bss_start = . ;

    .sbss ALIGN(0x1000) :
    {
        *(.sbss)
        *(.sbss.*)
        *(.gnu.linkonce.sb.*)
        *(.scommon)
    }

    .bss ALIGN(0x1000) :
    {
        *(EXCLUDE_FILE(*a3d_mglc.c.obj *mgl_geom.c.obj *mgl_state.c.obj *mgl_math.c.obj *mgl_rsplan.c.obj *mgl_abi5.c.obj *mgl_glu.c.obj *mgl_light.c.obj *mgl_glutq.c.obj *mgl_mtex.c.obj *mgl_readpx.c.obj *mgl_texds.c.obj *mgl_texgen.c.obj *jit_r10c.c.obj *jit_r10a1.c.obj *a3d_heapb.c.obj) .bss)
        *(EXCLUDE_FILE(*a3d_mglc.c.obj *mgl_geom.c.obj *mgl_state.c.obj *mgl_math.c.obj *mgl_rsplan.c.obj *mgl_abi5.c.obj *mgl_glu.c.obj *mgl_light.c.obj *mgl_glutq.c.obj *mgl_mtex.c.obj *mgl_readpx.c.obj *mgl_texds.c.obj *mgl_texgen.c.obj *jit_r10c.c.obj *jit_r10a1.c.obj *a3d_heapb.c.obj) .bss.[!~]*)
        *(.gnu.linkonce.b.*)
        *(EXCLUDE_FILE(*a3d_mglc.c.obj *mgl_geom.c.obj *mgl_state.c.obj *mgl_math.c.obj *mgl_rsplan.c.obj *mgl_abi5.c.obj *mgl_glu.c.obj *mgl_light.c.obj *mgl_glutq.c.obj *mgl_mtex.c.obj *mgl_readpx.c.obj *mgl_texds.c.obj *mgl_texgen.c.obj *jit_r10a1.c.obj *a3d_heapb.c.obj) COMMON)
        *(.bss.~a3d_r8)
        *(.bss.~zz_ap)
        *(.bss.~r9)
        *(EXCLUDE_FILE(*a3d_mglc.c.obj *mgl_geom.c.obj *mgl_state.c.obj *mgl_math.c.obj *mgl_rsplan.c.obj *mgl_abi5.c.obj *mgl_glu.c.obj *mgl_light.c.obj *mgl_glutq.c.obj *mgl_mtex.c.obj *mgl_readpx.c.obj *mgl_texds.c.obj *mgl_texgen.c.obj *jit_r10a1.c.obj *a3d_heapb.c.obj) .bss.~*)
        *jit_r10c.c.obj(.bss .bss.* COMMON)
        *jit_r10a1.c.obj(.bss .bss.* COMMON)
        *a3d_heapb.c.obj(.bss .bss.* COMMON)
    }
    . = ALIGN(0x1000);

    __bss_end = . ;

    .a3d_tail ALIGN(0x20) :
    {
        *(.a3d_tail_text)
        *(.a3d_tail_rodata)
        *a3d_r8.c.obj(.text .text.* .rodata .rodata.* .data .data.*)
        *(.b157_tail_text)
        *(.fpfix_tail_text)
        *(.r9_tail_text)
        *(.r9_tail_rodata)
        *zz_r9.c.obj(.text .text.* .rodata .rodata.*)
        *(.hunt_tail_text)
        *(.r10_tail_text)
        *(.r10_tail_rodata)
        *(.fix_tail_text)
        *(.zznet_tail_text)
        *(.r10c_tail_text)
        *(.r10c_tail_rodata)
        *jit_r10c.c.obj(.text .text.* .rodata .rodata.* .data .data.*)
        *(.r10c2_tail_rodata)       /* M68k_Translator.c's translator and five of its statics, for jit_r10c.c */
        *(.bw_tail_text)
        *(.r10e_tail_text)
        *(.r10a1_tail_text)
        *(.r10a1_tail_rodata)
        *jit_r10a1.c.obj(.text .text.* .rodata .rodata.* .data .data.*)
        *a3d_heapb.c.obj(.text .text.* .rodata .rodata.* .data .data.*)
        *(.mglc_tail_text)
        *a3d_mglc.c.obj(.text .text.* .rodata .rodata.* .data .data.* .bss .bss.* COMMON)
        *mgl_geom.c.obj(.text .text.* .rodata .rodata.* .data .data.* .bss .bss.* COMMON)
        *mgl_state.c.obj(.text .text.* .rodata .rodata.* .data .data.* .bss .bss.* COMMON)
        *mgl_math.c.obj(.text .text.* .rodata .rodata.* .data .data.* .bss .bss.* COMMON)
        *mgl_rsplan.c.obj(.text .text.* .rodata .rodata.* .data .data.* .bss .bss.* COMMON)
        *mgl_abi5.c.obj(.text .text.* .rodata .rodata.* .data .data.* .bss .bss.* COMMON)
        *mgl_glu.c.obj(.text .text.* .rodata .rodata.* .data .data.* .bss .bss.* COMMON)
        *mgl_light.c.obj(.text .text.* .rodata .rodata.* .data .data.* .bss .bss.* COMMON)
        *mgl_glutq.c.obj(.text .text.* .rodata .rodata.* .data .data.* .bss .bss.* COMMON)
        *mgl_mtex.c.obj(.text .text.* .rodata .rodata.* .data .data.* .bss .bss.* COMMON)
        *mgl_readpx.c.obj(.text .text.* .rodata .rodata.* .data .data.* .bss .bss.* COMMON)
        *mgl_texds.c.obj(.text .text.* .rodata .rodata.* .data .data.* .bss .bss.* COMMON)
        *mgl_texgen.c.obj(.text .text.* .rodata .rodata.* .data .data.* .bss .bss.* COMMON)
    }
    . = ALIGN(0x1000);

    _end = . ;

    __bootstrap_end = . ;

    .trailer ALIGN(4) :
    {
    LONG(0)
    LONG(0)
    LONG(0x01000000)    /* DTOK = 1, please give us device tree */
    LONG(0x04000000)
    LONG(0x44544f4b)    /* DTOK */
    LONG(0x20000000)
    LONG(0x04000000)
    LONG(0x5250544c)
    }
}
