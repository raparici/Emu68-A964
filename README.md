# Emu68-A964

Emu68-A964 is **[Emu68](https://github.com/michalsc/Emu68), Michal Schulz's 68k just-in-time
recompiler**, with an ARMv7 (armhf) back-end. It is the CPU of
[BigMig](https://github.com/raparici/BigMig-Emu68A9), an Amiga core for MiSTer: it runs bare metal
on the second Cortex-A9 core of the DE10-Nano's Cyclone V SoC, while MiSTer's Linux and its Main
program run on the first core and the Minimig chipset runs in the FPGA.

Everything here starts from Emu68: its translator, its m68k instruction handlers, its register
allocator, its FPU and its board model. Emu68-A964 is a fork of it, made at upstream commit
`b100dbd`, and it keeps Emu68's structure, names and licence. Thank you, Michal.

This tree is Emu68-A964 0.9.328 (the image still identifies itself as `Emu68A9`). OMMX, the vector extension of the instruction set, has its
own document: [OMMX.md](OMMX.md).

## What runs where

| | |
|---|---|
| core #1 (bare metal) | this firmware: the JIT, the m68k exception and interrupt model, and the Zorro boards it emulates |
| FPGA | the Minimig chipset (Agnus, Denise, Paula, CIAs, Gary), chip RAM, and the two 3D render units of BigMig 3Dx2 |
| core #0 (Linux) | MiSTer's Main: the OSD, disks and files, video scan-out, sound output, Ethernet, and MP3 decoding for the firmware's cards |

The firmware reaches the chipset through an AXI bridge into the FPGA (the *seam*), and it shares a
few pages of memory with the Main for settings, status and the data rings of the cards.

## Components

Every file of the port starts with its licence and one line that says what it is; the prefix of
that line names the component (for example `OMMX:`, `ZZ9000:`, `BigMig 3Dx2:`).

| Component | What it does | Where |
|---|---|---|
| **Emu68** | the translator, the m68k instruction handlers, the 68882 FPU, the math library, the boards and the support code | `src/M68k_*.c`, `src/math/`, `src/boards/`, `src/support.c`, `include/` |
| **JIT additions** | the translator as the release runs it: conditional exits kept inside the translation unit (forward exits joined to the unit's later code, loops closed in it, FBcc treated like Bcc), units ending in `JSR (An)` / `JMP (An)` chained to their target's unit, a dispatch that promotes a unit to the head of its cache bucket; the JIT's run-time settings | `src/jit_r10c.c`, `src/jit_r10a1.c`, `src/jit_levers.c`, `include/jit_levers.h`, `src/jit_fixes_tail.c`, `src/bank_bw_tail.c` |
| **armhf back-end** | Emu68's code generator is written against an A64 encoder interface; these layers implement that interface in A32 (single instructions in `shims/`, multi-word sequences in `emit/`, the encoder headers in `superinc/`, which comes first on the include path); the ARMv7 exception vectors, register allocator and EABI helpers | `superinc/`, `shims/`, `emit/`, `src/armhf/` |
| **DE10-Nano platform** | start-up of core #1, the memory map and the DDR carve-out, cache maintenance and the PL310 L2, the UART, the Fast RAM boards, the chipset transport over the seam, the HPS clock | `src/de10nano/start_de10nano.c`, `support_de10nano.c`, `cpuram_boards.c`, `hps_clock.h`, `src/pistorm/chipset_backend_axi.c`, `ps_protocol.*`, `amiga_memmap.h`, `scripts/ldscript-be-de10.lds` |
| **OMMX** | the vector extension: AMMX as a subset, native 32-bit lane operations, `OIDENT`, `ONCALL`; per-task register banks | `include/OMMX.h`, `src/M68k_OMMX.c`, `src/ommx_banks.c`; see [OMMX.md](OMMX.md) |
| **BigMig 3Dx2** | the 3D engine behind BigMig's `Warp3D.library`: contexts, textures, triangle setup and binning into band lists for the GPU in the FPGA (FABRIC backend), or rendered by core #1 itself (SOFT backend); reached from the guest with `ONCALL` | `src/de10nano/agent3d/` (`a3d_*`) |
| **BigMig 3Dx2 GL core** | the OpenGL 1.1 subset behind BigMig's `minigl.library` 29.x: state, matrices, lighting, clipping, texturing, GLU and GLUT logic, executed on core #1 from a context the library records | `src/de10nano/agent3d/mgl/` (`mgl_*`), `a3d_mglc.c` |
| **ZZ9000 card model** | MNT's ZZ9000 Zorro III card as its Picasso96 and network drivers see it: the register page, the 2D operations and the blit queue, the sprite, page flips, the Ethernet rings | `src/de10nano/zz9000/`, `src/de10nano/zz9000_board.c` |
| **uaesnd** | WinUAE's UAESND 16-bit sound board, as its AHI driver expects it: the descriptor walk, the resampler and mixer, and a PCM ring to the Main | `src/de10nano/uaesnd/`, `src/de10nano/uaesnd_board.c` |
| **MHI** | the MP3 / MP2 decoder card used by `mhibigmig.library`: a mailbox in the ZZ9000 register window, the stream and the PCM path; the Main decodes on core #0 | `src/de10nano/mhi/` |
| **bigmigHD.device** | a Zorro III autoconfig board whose boot ROM delivers `bigmigHD.device`, the hard-disk driver for the HDF images the Main serves, and its doorbell | `src/de10nano/bigmighd_board.c`, `bigmighd_rom.h` |
| **FPU resident (FPU ROM)** | a Zorro III board whose ROM holds a resident that patches the math libraries to run on the 68882 under the 68040 and 68060 personalities | `src/de10nano/fpresident/`, `src/de10nano/fpresident_board.c` |
| **devicetree.resource** | a Zorro III board that delivers Emu68's `devicetree.resource` to the guest (off unless the Main asks for it) | `src/de10nano/dtree_board.c` |

## Building

What the release uses:

* the [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
  15.2.rel1 for `arm-none-eabi`, on the `PATH`;
* CMake 3.14 or later, `make`, `git` and `patch`.

The two submodules are Michal Schulz's forks of capstone and tiny-std, at the commits Emu68
pinned, with the patches in `external/` (see [external/capstone-patches/README.md](external/capstone-patches/README.md)):

```sh
git clone https://github.com/michalsc/capstone.git external/capstone
git -C external/capstone checkout 63f7f3ac
(cd external/capstone && for p in ../capstone-patches/0*.patch; do patch -p1 < "$p"; done)

git clone https://github.com/michalsc/tiny-std.git external/tiny-stl
git -C external/tiny-stl checkout 6772983
(cd external/tiny-stl && patch -p1 < ../tiny-stl-patches/0001-bare-metal-tweaks.patch)
```

Then:

```sh
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchains/arm-elf.cmake -DTARGET=de10nano -DVARIANT=minimig
make -j
```

The result is `build/Emu68.img`. With `TARGET=de10nano` every BigMig option defaults to its
release value (`CMakeLists.txt`, the `option(...)` block); nothing else needs to be set. The
version string embeds the build date and the git hash of the tree; set `SOURCE_DATE_EPOCH` to fix
the date.

The image goes to `/media/fat/linux/Emu68.img` on the MiSTer SD card, next to the BigMig core.
Firmware and core are released and tested together: use the firmware of the same BigMig release.

## Targets

| `TARGET` | State |
|---|---|
| `de10nano` | BigMig on MiSTer (DE10-Nano, Cortex-A9): the one target built and tested today |
| `zynq7000` | Cortex-A9 Zynq-7000 boards, such as the Z3660 and the ZZ9000: planned |
| `de25nano` | DE25-Nano (Agilex 5, AArch64): planned |
| `raspi`, `raspi64`, `pbpro`, `rockpro64`, `virt` | inherited from Emu68 and not tested here; Emu68's own repository is the place for them |

Ports to other Cortex-A9 boards, starting with the Zynq-7000, are welcome: fork this repository and send pull requests.

## Notes on porting

The JIT and the armhf back-end need an ARMv7-A core with NEON and VFPv3 running big-endian (BE8);
they use no hardware divide. Zynq-7000 boards, such as the Z3660 accelerator, have the same CPU as
the DE10-Nano (two Cortex-A9 cores with NEON and a PL310 L2), so that part carries over unchanged.
What is specific to the DE10-Nano and BigMig:

* `src/de10nano/start_de10nano.c`, `support_de10nano.c`, `hps_clock.h` and
  `scripts/ldscript-be-de10.lds`: how core #1 starts, its MMU map, the UART, the clock manager;
* `src/pistorm/amiga_memmap.h`: the DDR carve-out and the pages shared with the Main;
* `src/pistorm/chipset_backend_axi.c` and `ps_protocol.h`: the transport to the chipset. Another
  board replaces this with its own path to the Amiga bus;
* the board models (ZZ9000, uaesnd, MHI, bigmigHD.device, 3D) talk to a companion program on the
  other core for scan-out, audio, network and disk. Their guest-facing side is independent of
  that, but each needs its companion side reimplemented;
* the 3D engine's FABRIC backend needs BigMig's GPU in the FPGA; its SOFT backend runs on any of
  these CPUs.

## Licences

Each file carries its own licence header; the licence texts are in `LICENSE` and `LICENSES/`.

| Part | Licence |
|---|---|
| Emu68, and most of the port (JIT additions, armhf back-end, DE10-Nano platform, OMMX, the 3D engine and its GL core, bigmigHD.device board and ROM, FPU resident) | MPL-2.0 (`LICENSE`) |
| Some files of the port (part of `emit/`, `shims/` and `superinc/`, `src/armhf/RA_nofp.c`, MHI, the devicetree.resource board, `uaesnd_inject.h`) | MIT (`LICENSES/MIT.txt`) |
| The ZZ9000 card model, after MNT Research's ZZ9000OS (Lucie L. Hartmann, MNT Research GmbH; Dimitris Panokostas); some of its files are ZZ9000OS's own | GPL-3.0-or-later (`LICENSES/GPL-3.0.txt`) |
| uaesnd, the card model of WinUAE's UAESND board | GPL-3.0-or-later |
| `src/de10nano/agent3d/a3d_be_soft.c`: its texture unit and fog follow the RTL of [RasterIX](https://github.com/ToNi3141/RasterIX) (ToNi3141), from which BigMig's GPU is derived | GPL-3.0-or-later |
| `src/math/`: musl and FreeBSD msun code (Sun Microsystems and others) | their permissive notices, in each file |
| `src/tlsf.c` | the AROS Development Team's, as in Emu68 |
| `external/capstone` (submodule) | BSD (its `LICENSE.TXT`) |
| `external/tiny-stl` (submodule) | MPL-2.0 |

**The firmware image as a whole.** `Emu68.img` links MPL-2.0 files with GPL-3.0-or-later files.
MPL-2.0 allows this (its section 3.3: the GPL is one of its Secondary Licenses), and the image is
then distributed under the terms of the GPL-3.0-or-later. Each MPL-2.0 and MIT file remains
available under its own licence on its own.

## Credits

* **Michal Schulz**: Emu68, which this is; and `brcm-sdhc.device`, which `bigmigHD.device` is
  derived from.
* **Apollo Team**: AMMX, which OMMX contains (*AC68080 Programmer's Reference Manual*).
* **MNT Research (Lucie L. Hartmann) and Dimitris Panokostas**: the ZZ9000 card and ZZ9000OS, whose model is here.
* **Toni Wilen**: WinUAE's UAESND board, whose model is here.
* **ToNi3141**: RasterIX, the base of BigMig's GPU.
* The authors of musl, FreeBSD msun, capstone, tiny-std and AROS's TLSF.
* **Mark Watson**: Minimig Hybrid, the first ARM-plus-FPGA Minimig, which BigMig follows.
