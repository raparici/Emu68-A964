cmake_minimum_required(VERSION 3.14.0)
project(Emu68 VERSION 0.9.328)

include(cmake/verstring.cmake)
include(cmake/firmware_download.cmake)
get_verstring(VERSTRING)

set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_STANDARD 11)

set(SUPPORTED_TARGETS "raspi" "raspi64" "pbpro" "rockpro64" "virt" "de10nano" "zynq7000" "de25nano")   # zynq7000, de25nano: TBD
set(TARGET "raspi64" CACHE STRING "One of target machines: ${SUPPORTED_TARGETS}")
set_property(CACHE TARGET PROPERTY STRINGS ${SUPPORTED_TARGETS})

set(SUPPORTED_VARIANTS "none" "pistorm" "minimig")
set(VARIANT "none" CACHE STRING "Compilation variant: ${SUPPORTED_VARIANTS}")
set_property(CACHE VARIANT PROPERTY STRINGS ${SUPPORTED_VARIANTS})

# BigMig (TARGET=de10nano VARIANT=minimig): the defaults below are the release configuration
if(${TARGET} STREQUAL "de10nano")
    set(BIGMIG_DEFAULT ON)
else()
    set(BIGMIG_DEFAULT OFF)
endif()

# JIT (armhf back-end)
option(EMU68_BCC_INLINE "armhf: keep translating past a conditional exit" ${BIGMIG_DEFAULT})
option(EMU68_INNER_LOOP "armhf: close inner loops inside a translation unit (needs EMU68_BCC_INLINE)" ${BIGMIG_DEFAULT})
option(EMU68_BANK_FASTPATH "armhf: fixed chipset banks called directly instead of through a data abort" ${BIGMIG_DEFAULT})
option(EMU68_CPU040 "68040 and 68060 personalities (selected at run time)" ${BIGMIG_DEFAULT})
option(EMU68_OMMX "OMMX vector extension (AMMX compatible)" ${BIGMIG_DEFAULT})
# DE10-Nano platform
option(EMU68_FAST_DISPATCH "chain translation units through an ARM trampoline" ON)
option(EMU68_FPU "68882 coprocessor (double precision)" ${BIGMIG_DEFAULT})
option(EMU68_EXTROM_1MB "1 MB Kickstart: map the extended ROM half at $E00000" ${BIGMIG_DEFAULT})
option(EMU68_CHIP_WINDOW "chip RAM reads through the seam slave's window (needs a core with it)" ${BIGMIG_DEFAULT})
option(EMU68_BLITFAIR "pace chip-RAM code while the blitter runs" ${BIGMIG_DEFAULT})
option(EMU68_BLTBUSY_PACE "pace DMACONR reads that see the blitter busy" ${BIGMIG_DEFAULT})
option(EMU68_SEAM_POSTED_WR "posted chip writes on the seam" ${BIGMIG_DEFAULT})
option(EMU68_BIGMIGHD "bigmigHD.device boot ROM board" ${BIGMIG_DEFAULT})
option(EMU68_ZZ9000 "ZZ9000 card model (RTG)" ${BIGMIG_DEFAULT})
option(EMU68_ZZNET "ZZ9000 card model: Ethernet" ${BIGMIG_DEFAULT})
option(EMU68_DEVICETREE "devicetree.resource board" ${BIGMIG_DEFAULT})
option(EMU68_UAESND "uaesnd 16-bit audio card model" ${BIGMIG_DEFAULT})
option(EMU68_MHI "MHI MP3/MP2 decoding (needs EMU68_ZZ9000 and EMU68_UAESND)" ${BIGMIG_DEFAULT})
option(EMU68_FP_RESIDENT "FPU math resident board (68040/68060 personalities)" ${BIGMIG_DEFAULT})
option(EMU68_SHARE_L2 "MiSTer Share: data page mapped to the guest" ${BIGMIG_DEFAULT})
option(EMU68_SHARE_DOORBELL "MiSTer Share: zero-copy doorbell" ${BIGMIG_DEFAULT})
option(EMU68_A3D "BigMig 3Dx2 3D engine (needs EMU68_OMMX)" ${BIGMIG_DEFAULT})

set(ARCH_FILES "")
set(TARGET_FILES "")
set(BASE_FILES
    src/support.c
    src/tlsf.c
    src/devicetree.c
    src/cpp_support.cpp
    src/EmuLogo.c
    src/HunkLoader.c
    src/ElfLoader.c
    src/disasm.c
    src/jit_levers.c
    src/jit_fixes_tail.c
    src/jit_r10c.c
    src/bank_bw_tail.c
    src/jit_r10a1.c
)
set_source_files_properties(src/jit_r10c.c PROPERTIES COMPILE_OPTIONS "-Os;-fno-merge-constants")
set_source_files_properties(src/jit_r10a1.c PROPERTIES COMPILE_OPTIONS "-Os;-fno-merge-constants;-mgeneral-regs-only")

set(EMU68_FILES
    src/M68k_Translator.c
    src/M68k_SR.c
    src/M68k_MULDIV.c
    src/M68k_MOVE.c
    src/M68k_EA.c
    src/M68k_LINE0.c
    src/M68k_LINE4.c
    src/M68k_LINE5.c
    src/M68k_LINE6.c
    src/M68k_LINE8.c
    src/M68k_LINE9.c
    src/M68k_LINEB.c
    src/M68k_LINEC.c
    src/M68k_LINED.c
    src/M68k_LINEE.c
    src/M68k_LINEF.c
    src/M68k_OMMX.c
    src/ommx_banks.c
    src/M68k_Exception.c
    src/M68k_CC.c

    src/math/__rem_pio2.c
    src/math/__rem_pio2_large.c
    src/math/__tan.c
    src/math/__sin.c
    src/math/__cos.c
    src/math/__expo2.c
    src/math/atan.c
    src/math/atanh.c
    src/math/acos.c
    src/math/asin.c
    src/math/tan.c
    src/math/tanh.c
    src/math/scalbn.c
    src/math/fmod.c
    src/math/remquo.c
    src/math/floor.c
    src/math/cosh.c
    src/math/exp.c
    src/math/exp10.c
    src/math/exp2.c
    src/math/expm1.c
    src/math/log.c
    src/math/log1p.c
    src/math/log10.c
    src/math/log2.c
    src/math/sinh.c
    src/math/pow.c
    src/math/modf.c
    src/math/sincos.c
)

if(EMU68_FPU)
    add_compile_definitions(EMU68_FPU)
endif()
if(EMU68_EXTROM_1MB)
    add_compile_definitions(EMU68_EXTROM_1MB)
endif()
if(EMU68_FAST_DISPATCH)
    add_compile_definitions(EMU68_FAST_DISPATCH)
endif()
if(EMU68_ZZNET)
    add_compile_definitions(EMU68_ZZNET)
endif()
if(EMU68_CPU040)
    add_compile_definitions(EMU68_CPU040)
endif()
if(EMU68_OMMX)
    add_compile_definitions(EMU68_OMMX)
endif()

# BigMig 3Dx2: the 3D engine on core #1, called from the guest through ONCALL (an OMMX instruction)
if(EMU68_A3D)
    if(NOT EMU68_OMMX)
        message(FATAL_ERROR "EMU68_A3D needs EMU68_OMMX: ONCALL is an OMMX instruction")
    endif()
    if(NOT ${TARGET} STREQUAL "de10nano")
        message(FATAL_ERROR "EMU68_A3D builds for TARGET=de10nano only")
    endif()
    add_compile_definitions(EMU68_A3D RIX_STORE_SWAP=0)
    list(APPEND BASE_FILES
        src/de10nano/agent3d/a3d_agent.c
        src/de10nano/agent3d/a3d_draw.c
        src/de10nano/agent3d/a3d_tex.c
        src/de10nano/agent3d/a3d_bin.c
        src/de10nano/agent3d/a3d_guest.c
        src/de10nano/agent3d/a3d_heap.c
        src/de10nano/agent3d/a3d_listdec.c
        src/de10nano/agent3d/a3d_be_null.c
        src/de10nano/agent3d/a3d_be_soft.c
        src/de10nano/agent3d/a3d_be_fabric.c
        src/de10nano/agent3d/a3d_fw.c
        src/de10nano/agent3d/a3d_fw_prot.c
        src/de10nano/agent3d/a3d_mgl.c
        src/de10nano/agent3d/mgl/mgl_stage.c
        src/de10nano/agent3d/a3d_r8.c
        src/de10nano/agent3d/a3d_heapb.c
        src/de10nano/agent3d/a3d_mglc.c
        src/de10nano/agent3d/mgl/mgl_geom.c src/de10nano/agent3d/mgl/mgl_state.c src/de10nano/agent3d/mgl/mgl_math.c
        src/de10nano/agent3d/mgl/mgl_rsplan.c src/de10nano/agent3d/mgl/mgl_abi5.c src/de10nano/agent3d/mgl/mgl_glu.c
        src/de10nano/agent3d/mgl/mgl_light.c src/de10nano/agent3d/mgl/mgl_glutq.c src/de10nano/agent3d/mgl/mgl_mtex.c
        src/de10nano/agent3d/mgl/mgl_readpx.c src/de10nano/agent3d/mgl/mgl_texds.c src/de10nano/agent3d/mgl/mgl_texgen.c
    )
    set_source_files_properties(src/de10nano/agent3d/a3d_heapb.c PROPERTIES COMPILE_OPTIONS "-Os;-fno-merge-constants;-mgeneral-regs-only")
    # runs inside the data-abort handler with the guest's VFP/NEON state live: general registers only, no library calls
    set_source_files_properties(src/de10nano/agent3d/a3d_fw_prot.c
                                PROPERTIES COMPILE_OPTIONS "-U__ARM_NEON__;-U__ARM_NEON;-mgeneral-regs-only;-fno-tree-loop-distribute-patterns")
    # one float semantics on every host: no fused multiply-add contraction
    set_source_files_properties(
        src/de10nano/agent3d/a3d_r8.c
        src/de10nano/agent3d/a3d_agent.c src/de10nano/agent3d/a3d_draw.c src/de10nano/agent3d/a3d_tex.c
        src/de10nano/agent3d/a3d_bin.c src/de10nano/agent3d/a3d_be_soft.c
        PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
    # the GL core: one section per function and object, placed by the linker scripts after the draw path
    set_source_files_properties(
        src/de10nano/agent3d/a3d_mgl.c src/de10nano/agent3d/mgl/mgl_stage.c
        PROPERTIES COMPILE_OPTIONS "-ffp-contract=off;-ffunction-sections;-fdata-sections")
    set_source_files_properties(
        src/de10nano/agent3d/a3d_mglc.c
        src/de10nano/agent3d/mgl/mgl_geom.c src/de10nano/agent3d/mgl/mgl_state.c src/de10nano/agent3d/mgl/mgl_math.c
        src/de10nano/agent3d/mgl/mgl_rsplan.c src/de10nano/agent3d/mgl/mgl_abi5.c src/de10nano/agent3d/mgl/mgl_glu.c
        src/de10nano/agent3d/mgl/mgl_light.c src/de10nano/agent3d/mgl/mgl_glutq.c src/de10nano/agent3d/mgl/mgl_mtex.c
        src/de10nano/agent3d/mgl/mgl_readpx.c src/de10nano/agent3d/mgl/mgl_texds.c src/de10nano/agent3d/mgl/mgl_texgen.c
        PROPERTIES COMPILE_OPTIONS "-ffp-contract=off;-ffunction-sections;-fdata-sections;-fno-math-errno;-include${CMAKE_SOURCE_DIR}/src/de10nano/agent3d/a3d_mglc_fw.h")
endif()

if(EMU68_BCC_INLINE)
    add_compile_definitions(EMU68_BCC_INLINE)
endif()
if(EMU68_INNER_LOOP)
    if(NOT EMU68_BCC_INLINE)
        message(FATAL_ERROR "EMU68_INNER_LOOP requires EMU68_BCC_INLINE")
    endif()
    add_compile_definitions(EMU68_INNER_LOOP)
endif()

if (${TARGET} IN_LIST SUPPORTED_TARGETS)
    message("-- Selected target machine: ${TARGET}")
    if(${TARGET} STREQUAL "zynq7000" OR ${TARGET} STREQUAL "de25nano")
        message(FATAL_ERROR "TARGET ${TARGET}: TBD")
    endif()
    if(${TARGET} STREQUAL "raspi64" OR ${TARGET} STREQUAL "raspi")
        if (${TARGET} STREQUAL "raspi64")
            list(APPEND TARGET_FILES src/raspi/start_rpi64.c)
        else()
            list(APPEND TARGET_FILES src/raspi/start_rpi.c)
        endif()
        list(APPEND TARGET_FILES src/raspi/support_rpi.c)
        download_raspi_firmware()
        install(DIRECTORY ${CMAKE_BINARY_DIR}/firmware/ DESTINATION .)
        install(FILES ${CMAKE_BINARY_DIR}/Emu68.img DESTINATION .)
        install(FILES ${CMAKE_SOURCE_DIR}/scripts/config.txt DESTINATION .)
        add_compile_definitions(RASPI)
    elseif(${TARGET} STREQUAL "pbpro")
        list(APPEND TARGET_FILES
            src/pine64/start_pbpro.c
            src/pine64/support_pbpro.c
        )
        install(FILES ${CMAKE_SOURCE_DIR}/blobs/rk3399-pinebookpro.dtb DESTINATION .)
        install(FILES ${CMAKE_BINARY_DIR}/Emu68.img DESTINATION .)
        install(FILES ${CMAKE_SOURCE_DIR}/scripts/extlinux.conf DESTINATION extlinux/)
    elseif(${TARGET} STREQUAL "virt")
        list(APPEND TARGET_FILES
            src/virt/start_virt.c
            src/virt/support_virt.c
        )
        install(FILES ${CMAKE_BINARY_DIR}/Emu68.img DESTINATION .)
    elseif(${TARGET} STREQUAL "de10nano")
        # DE10-Nano: bare metal on the second Cortex-A9 of the Cyclone V HPS
        list(APPEND TARGET_FILES
            src/de10nano/start_de10nano.c
            src/de10nano/support_de10nano.c
        )
        add_compile_definitions(DE10NANO)
        install(FILES ${CMAKE_BINARY_DIR}/Emu68.img DESTINATION .)
    endif()

    if(EMU68_BANK_FASTPATH)
        add_compile_definitions(EMU68_BANK_FASTPATH)
    endif()

    if (${TARGET} STREQUAL "raspi" OR ${TARGET} STREQUAL "de10nano")
        set(TARGET_ARCH "armhf")
    else()
        set(TARGET_ARCH "aarch64")
    endif()
else()
    message(FATAL_ERROR "Wrong target machine specified: ${TARGET}")
endif()

if (${VARIANT} IN_LIST SUPPORTED_VARIANTS)
    message("-- Selected variant: ${VARIANT}")
    if(${VARIANT} STREQUAL "pistorm")
        if(${TARGET} STREQUAL "raspi64" OR ${TARGET} STREQUAL "raspi")
            add_compile_definitions(PISTORM)
            include_directories(src/pistorm)
            list(APPEND BASE_FILES
                src/pistorm/ps_protocol.c
                src/boards/devicetree.c
                src/boards/z2ram.c
                src/boards/sdcard.c
                src/boards/68040.c
            )
        else()
            message(FATAL_ERROR "PiStorm variant is supported on raspi targets, only.")
        endif()
    elseif(${VARIANT} STREQUAL "minimig")
        if(${TARGET} STREQUAL "de10nano")
            # the Amiga chipset in the FPGA, reached through the AXI seam (src/pistorm/chipset_backend_axi.c)
            add_compile_definitions(CHIPSET_BACKEND CHIPSET_AXI SEAM_CHIPRAM)
            include_directories(src/pistorm)
            list(APPEND BASE_FILES
                src/pistorm/chipset_backend_axi.c
                src/de10nano/cpuram_boards.c
            )
            if(EMU68_SEAM_POSTED_WR)
                add_compile_definitions(EMU68_SEAM_POSTED_WR)
            endif()
            if(EMU68_BLITFAIR)
                add_compile_definitions(EMU68_BLITFAIR)
            endif()
            if(EMU68_CHIP_WINDOW)
                add_compile_definitions(EMU68_CHIP_WINDOW)
            endif()
            if(EMU68_BLTBUSY_PACE)
                add_compile_definitions(EMU68_BLTBUSY_PACE)
            endif()
            if(EMU68_SHARE_L2)
                add_compile_definitions(EMU68_SHARE_L2)
            endif()
            if(EMU68_SHARE_DOORBELL)
                add_compile_definitions(EMU68_SHARE_DOORBELL)
            endif()
            # autoconfig board order: bigmigHD, ZZ9000, devicetree, uaesnd, FPU resident
            if(EMU68_BIGMIGHD)
                add_compile_definitions(EMU68_BIGMIGHD)
                list(APPEND BASE_FILES src/de10nano/bigmighd_board.c)
            endif()
            if(EMU68_ZZ9000)
                add_compile_definitions(EMU68_ZZ9000)
                list(APPEND BASE_FILES
                    src/de10nano/zz9000_board.c
                    src/de10nano/zz9000/zz_card.c
                    src/de10nano/zz9000/zz_queue.c
                    src/de10nano/zz9000/gfx.c
                    src/de10nano/zz9000/gfx_rows_scalar.c
                    src/de10nano/zz9000/gfx_rows_neon.c
                    src/de10nano/zz9000/zz_aprime.c
                    src/de10nano/zz9000/zz_r9.c
                    src/de10nano/zz9000/zz_idle.c
                )
                # runs in the data-abort and dispatch contexts, where the guest's FPU state lives in d0-d15: scalar
                set_source_files_properties(
                    src/de10nano/zz9000/gfx.c
                    src/de10nano/zz9000/zz_card.c
                    src/de10nano/zz9000/zz_queue.c
                    src/de10nano/zz9000_board.c
                    src/de10nano/zz9000/gfx_rows_scalar.c
                    PROPERTIES COMPILE_OPTIONS "-U__ARM_NEON__;-U__ARM_NEON;-mgeneral-regs-only;-DGFX_NO_FLOAT;-DGFX_NEON_ROWS")
                set_source_files_properties(
                    src/de10nano/zz9000/zz_aprime.c
                    src/de10nano/zz9000/zz_r9.c
                    src/de10nano/zz9000/zz_idle.c
                    PROPERTIES COMPILE_OPTIONS "-U__ARM_NEON__;-U__ARM_NEON;-mgeneral-regs-only;-Os")
                # hand-written row copies in q8-q15 only (file-scope asm, no compiler-generated code)
                set_source_files_properties(
                    src/de10nano/zz9000/gfx_rows_neon.c
                    PROPERTIES COMPILE_OPTIONS "-marm;-mfpu=neon;-DGFX_NEON_ROWS")
            endif()
            if(EMU68_DEVICETREE)
                add_compile_definitions(EMU68_DEVICETREE)
                list(APPEND BASE_FILES src/de10nano/dtree_board.c)
            endif()
            if(EMU68_UAESND)
                add_compile_definitions(EMU68_UAESND)
                list(APPEND BASE_FILES
                    src/de10nano/uaesnd_board.c
                    src/de10nano/uaesnd/uaesnd_card.c
                    src/de10nano/uaesnd/uaesnd_mix.c
                    src/de10nano/uaesnd/uaesnd_ring.c
                )
                set_source_files_properties(
                    src/de10nano/uaesnd_board.c
                    src/de10nano/uaesnd/uaesnd_card.c
                    src/de10nano/uaesnd/uaesnd_mix.c
                    src/de10nano/uaesnd/uaesnd_ring.c
                    PROPERTIES COMPILE_OPTIONS "-U__ARM_NEON__;-U__ARM_NEON;-mgeneral-regs-only")
            endif()
            if(EMU68_MHI)
                if(NOT EMU68_ZZ9000 OR NOT EMU68_UAESND)
                    message(FATAL_ERROR "EMU68_MHI needs EMU68_ZZ9000 (mailbox window) and EMU68_UAESND (PCM output)")
                endif()
                add_compile_definitions(EMU68_MHI)
                list(APPEND BASE_FILES src/de10nano/mhi/mhi_fw.c)
                set_source_files_properties(
                    src/de10nano/mhi/mhi_fw.c
                    PROPERTIES COMPILE_OPTIONS "-U__ARM_NEON__;-U__ARM_NEON;-mgeneral-regs-only;-fno-jump-tables")
            endif()
            if(EMU68_FP_RESIDENT)
                add_compile_definitions(EMU68_FP_RESIDENT)
                list(APPEND BASE_FILES src/de10nano/fpresident_board.c)
            endif()
        else()
            message(FATAL_ERROR "minimig variant is supported on the de10nano target, only.")
        endif()
    endif()
else()
    message(FATAL_ERROR "Wrong variant selected: ${VARIANT}")
endif()

add_link_options(-Wl,--build-id -nostdlib -nostartfiles -static)
add_compile_options(-mbig-endian -fno-exceptions -ffreestanding -Wall -Wextra)
add_compile_options(-fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=)

if(${TARGET_ARCH} STREQUAL "armhf")
    list(APPEND ARCH_FILES
        src/armhf/start.c
        src/armhf/RegisterAllocator.c
        src/armhf/glue.c
        src/armhf/aeabi_ldivmod.c
        src/armhf/RA_nofp.c
    )
    if(${VARIANT} STREQUAL "minimig")
        list(APPEND ARCH_FILES src/armhf/vectors.c)
        # the data-abort path is general-register only
        set_source_files_properties(src/armhf/vectors.c src/pistorm/chipset_backend_axi.c src/bank_bw_tail.c
            PROPERTIES COMPILE_OPTIONS "-mgeneral-regs-only")
    endif()
    set(CAPSTONE_ARM_SUPPORT ON CACHE BOOL "CAPSTONE_ARM_SUPPORT")
    if(${TARGET} STREQUAL "de10nano")
        set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/ldscript-be-de10.lds)
    else()
        set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/ldscript-be.lds)
    endif()
    add_link_options(-Wl,--be8 -Wl,--format=elf32-bigarm -T ${LINKER_SCRIPT})
    # r9 is the emitter's scratch register (REG_EMIT_TMP): never allocated, so never saved by a unit
    add_compile_options(-ffixed-r9)
    if(${TARGET} STREQUAL "de10nano")
        # Cortex-A9: NEON + VFPv3, no FMA, no hardware integer divide
        add_compile_options(-mcpu=cortex-a9 -mfloat-abi=hard -mfpu=neon -O3 -ffixed-r11 -fomit-frame-pointer)
    else()
        add_compile_options(-mcpu=cortex-a7 -mfloat-abi=hard -mfpu=neon-vfpv4 -O3 -ffixed-r11 -fomit-frame-pointer)
    endif()
else()
    list(APPEND ARCH_FILES
        src/aarch64/start.c
        src/aarch64/mmu.c
        src/aarch64/RegisterAllocator64.c
        src/aarch64/vectors.c
    )
    set(CAPSTONE_ARM64_SUPPORT ON CACHE BOOL "CAPSTONE_ARM64_SUPPORT")
    set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/ldscript-be64.lds)
    add_link_options(-Wl,-EB -Wl,--format=elf64-bigaarch64 -T ${LINKER_SCRIPT})
    add_compile_options(-march=armv8-a+crc -fomit-frame-pointer -O3 -ffixed-x12)

    if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 10.0)
        add_compile_options(-mno-outline-atomics)
    endif()
endif()

add_executable(Emu68.elf
    ${ARCH_FILES}
    ${TARGET_FILES}
    ${BASE_FILES}
    ${EMU68_FILES}
)
# the 3D heap's second region: these functions reach src/de10nano/agent3d/a3d_heapb.c
if(EMU68_A3D)
    target_link_options(Emu68.elf PRIVATE "-Wl,--wrap=a3d_heap_init,--wrap=a3d_heap_phys,--wrap=a3d_heap_va,--wrap=a3d_ls_va")
endif()

add_custom_command(
    TARGET Emu68.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -v -O binary "${CMAKE_BINARY_DIR}/Emu68.elf" "${CMAKE_BINARY_DIR}/Emu68.img"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_subdirectory(external)

target_link_libraries(Emu68.elf tinystl capstone-static)
if(${TARGET_ARCH} STREQUAL "armhf")
    target_include_directories(Emu68.elf PRIVATE
        ${CMAKE_SOURCE_DIR}/superinc
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/emit
        ${CMAKE_SOURCE_DIR}/external/capstone/include)
else()
    target_include_directories(Emu68.elf PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/external/capstone/include)
endif()
target_compile_definitions(Emu68.elf PRIVATE VERSION_STRING="${VERSTRING}")
