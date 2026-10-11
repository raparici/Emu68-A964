/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: what the 3D engine's modules share. */

#ifndef A3D_AGENT_H
#define A3D_AGENT_H

#include <stdint.h>
#include "a3d_abi.h"
#include "a3d_w3d.h"
#include "a3d_guest.h"
#include "a3d_heap.h"
#include "a3d_bin.h"

#define A3D_AGENT_VERSION       4u
#define A3D_MAX_CTX             8u
#define A3D_MAX_TEX             1024u
#define A3D_MAX_CHAIN           64u     /* staging chunks per band list (1 MB)         */
#define A3D_MAX_TEXSIZE         256u
#define A3D_GUARD_HALF          1023.5f

void a3d_plat_clean(const void *p, uint32_t len);       /* make CPU writes visible to DDR readers  */
void a3d_plat_clean_nc(const void *p, uint32_t len);
void *a3d_pool_alloc_t(uint32_t bytes);
void a3d_plat_log(const char *fmt, ...);
uint32_t a3d_plat_cycles(void);                          /* a free-running counter for the stats    */
int a3d_plat_gpu_map(void);
uint32_t a3d_plat_gpu_rd(uint32_t off);
void a3d_plat_gpu_wr(uint32_t off, uint32_t v);
void a3d_plat_gpu_relax(void);
uint32_t a3d_plat_cycles_per_ms(void);
uint32_t a3d_plat_prof(void);
int a3d_plat_prot_set(uint32_t key, uint32_t base, uint32_t lo, uint32_t bytes);
int a3d_plat_prot_pending(uint32_t key, uint32_t on);
void a3d_plat_present_begin(void);
uint32_t a3d_plat_display_guard(uint32_t lo, uint32_t hi);
#define A3D_PROT_SLOTS          3u

/* ---- a render target, resolved -------------------------------------------------------------------- */
typedef struct a3d_target
{
    uint32_t base;          /* guest address of the bitmap (A3D_P_TBASE)                        */
    uint32_t guest;         /* guest address of the first row (bitmap base + y offset rows)   */
    uint32_t bprow, width, height, format;      /* W3D_FMT_*                                   */
    uint8_t *host;          /* host pointer of `guest`                                         */
    uint32_t phys;          /* physical address of `guest` (direct targets)                    */
    uint32_t direct;
    uint8_t *priv;
    uint32_t priv_phys;
    uint32_t bw;
    uint32_t color_be;
} a3d_target;

void a3d_present_row(const uint8_t *s, uint8_t *d, uint32_t w, uint32_t fmt, uint32_t le);
void a3d_fill_row(const uint8_t *s, uint8_t *d, uint32_t w, uint32_t fmt, uint32_t le);
extern uint32_t a3d_fill_ref_on;

typedef struct a3d_region { uint32_t phys, bytes; uint8_t *va; } a3d_region;
#define A3D_LS_REGIONS 4u
#define A3D_LS_FRAME    1u          /* submitted by FRAME (W3D_FlushFrame): a frame ends with it      */
typedef struct a3d_listset
{
    uint32_t fence;
    uint32_t flags;             /* A3D_LS_*                                                         */
    uint32_t frame, passes;     /* FRAME sets: the context's frame number and passes in the frame   */
    uint32_t bands, width, height, band_lines;
    const uint8_t *dir;         /* per band {u32 phys of the list, u32 bytes}, big... native words  */
    uint32_t dir_phys;
    a3d_region region[A3D_LS_REGIONS];      /* how a backend running on the CPU reaches the addresses */
    uint32_t nregions;
    uint32_t ctx;               /* context index (for the completion work)                      */
    uint32_t color_be;
} a3d_listset;

typedef struct a3d_backend
{
    const char *name;
    uint32_t id;                                /* A3D_BE_*                                       */
    int (*submit)(const a3d_listset *ls);       /* 0 = queued; else a W3D error                    */
    /* do up to `budget` units of work (SOFT: pixels); returns the last completed fence */
    uint32_t (*progress)(uint32_t budget);
    uint32_t (*completed)(void);
    void (*reset)(void);
    uint32_t async;
    uint32_t color_le;
    uint32_t (*units)(void);                    /* raster units (HELLO's caps): 0 for the CPU backends   */
    uint32_t (*orders)(void);
} a3d_backend;
#define A3D_ORD_LE      1u
#define A3D_ORD_BE      2u

extern const a3d_backend a3d_be_null;
extern const a3d_backend a3d_be_soft;
extern const a3d_backend a3d_be_softle;
#define A3D_BE_SOFTLE   0x81u       /* a3d_init's argument only: never reported */
extern uint32_t a3d_soft_le;        /* the SOFT backend's colour order in DDR: 1 = little-endian (A3D_BE_SOFTLE) */
extern const a3d_backend a3d_be_fabric;
int a3d_fabric_probe(void);
uint32_t a3d_backend_id(void);
uint32_t a3d_chunks_held(void);
extern uint32_t a3d_fab_fe_force;
typedef struct a3d_soft_tu
{
    const uint8_t *base;        /* the texture's block (level 0 first, little-endian texels) */
    uint32_t lw, lh;            /* log2 of level 0                                           */
    uint32_t fmt;               /* RasterIX pixel format (0 RGBA4444, 1 RGBA5551, 2 RGB565)   */
    uint32_t clamps, clampt, bilinear, mip;
} a3d_soft_tu;
uint32_t a3d_soft_lvl_off(uint32_t lw, uint32_t lh, uint32_t l);         /* in texels */
uint32_t a3d_soft_lod(uint32_t lw, uint32_t lh, int32_t s, int32_t t, int32_t sxy, int32_t txy);
void a3d_soft_sample(const a3d_soft_tu *T, uint32_t l, int32_t s, int32_t t, uint32_t out[4]);
uint32_t a3d_soft_fog(const uint32_t lut[RIX_FOG_LUT_WORDS], float x);   /* 0..255, 255 = no fog */
void a3d_soft_taps(const a3d_soft_tu *T, uint32_t l, int32_t s, int32_t t, uint32_t addr[4], uint32_t *subu,
                   uint32_t *subv);
void a3d_soft_fog_apply(const uint32_t lut[RIX_FOG_LUT_WORDS], const uint32_t fogc[4], float x, uint32_t c[4]);
void a3d_fog_build(uint32_t mode, float d_start, float d_end, float density, uint32_t k, uint32_t lut[RIX_FOG_LUT_WORDS]);
uint32_t a3d_fog_scale_k(uint32_t mode, float d_start, float d_end, float density);

typedef struct a3d_cmd
{
    uint32_t op;                /* the header word                                                */
    const uint8_t *payload;     /* payload words (list byte order: rix_get32 reads them)          */
    uint32_t n;                 /* payload words                                                   */
} a3d_cmd;
/* 1 = a command was decoded; 0 = end of list; -1 = malformed (unknown opcode or truncated) */
int a3d_list_next(const uint8_t *list, uint32_t bytes, uint32_t *pos, a3d_cmd *c);
#define A3D_TV_WORDS            18u
#define A3D_TV_HDR              (A3D_OP_TRIANGLE_VERTS | A3D_TV_ST_RAW | A3D_TV_WORDS)
typedef struct a3d_walk
{
    const a3d_listset *ls;
    const uint8_t *l;
    uint32_t bytes, pos;
    uint32_t chunks;                /* chunks entered (a bound against a LINK loop)                    */
} a3d_walk;
/* 1 = the band has a list; 0 = an empty band; -1 = its directory entry does not resolve */
int a3d_walk_begin(a3d_walk *w, const a3d_listset *ls, uint32_t band);
/* 1 = a command (never a LINK); 0 = the end of the band; -1 = malformed (a list error) */
int a3d_walk_next(a3d_walk *w, a3d_cmd *c);
/* [phys, phys + bytes) in the list set's regions -> a pointer (0 = not in any) */
const uint8_t *a3d_ls_va(const a3d_listset *ls, uint32_t phys, uint32_t bytes);

typedef struct a3d_stats_t
{
    uint32_t oncalls, oncalls_trap, oncalls_jit;
    uint32_t errors;            /* requests answered with an error                                  */
    uint32_t contexts, textures, tex_bytes;
    uint32_t draws, tris_in, tris_culled, tris_clipped, tris_binned, tri_bands;
    uint32_t passes, cuts, frames, fences_done;
    uint32_t list_bytes, chunks_peak;
    uint32_t soft_pixels, soft_bands, null_cmds, list_errors;
    uint32_t presents, depth_commits, depth_loads;
    uint32_t wait_mode;
    uint32_t waits, unlocks;    /* WAIT and UNLOCK calls                                              */
    uint32_t lazy_waits;
    uint32_t wait_cuts;         /* waits that submitted an open pass mid-frame (EXACT, or a refusal)  */
    uint32_t sub_frame;         /* submissions by FRAME: passes - sub_frame = every cut, whatever cause */
    uint32_t prot_sets, prot_clears, prot_refused;   /* targets protected / released / refused          */
    uint32_t prot_faults;       /* protection faults taken (the guest CPU touched a protected page)    */
    uint32_t prot_shared;       /* of them outside every target's own bytes (a page shared with data)  */
    uint32_t prot_self;
    uint32_t prot_stale;        /* of them on a page no key claimed (released, must stay 0)            */
    uint32_t fault_flushes;     /* contexts finished because of a fault: the cuts AUTO really pays     */
    uint32_t fence_flushes;     /* contexts finished because of a 2D op or a flip on their target      */
    uint32_t closes;            /* contexts finished because their target changed or went away        */
    uint32_t fault_cycles;      /* cycles spent finishing work inside faults and fences (PMCCNTR / 64) */
    uint32_t prot_idle;
    uint32_t mmu_applies;       /* times the protection rewrote page tables                            */
    uint32_t tlb_all;           /* full TLB invalidations it issued                                    */
    uint32_t tlb_pages;         /* single-page TLB invalidations it issued                             */
    uint32_t tex_updates;       /* TEX_UPDATE calls answered (W3D_UpdateTexImage / UpdateTexSubImage)    */
    uint32_t tex_renames;       /* ... that renamed the texture (pending work still sampled its block)  */
    uint32_t tex_update_drains; /* ... that had to wait for that work instead (no block or retire slot)  */
    uint32_t fog_luts;          /* fog tables computed (a context's fog parameters changed)              */
    uint32_t backend;
    uint32_t fab_kicks;         /* doorbells rung                                                         */
    uint32_t fab_kick_clocks;   /* the sum of KICK_CLOCKS (GPU clocks, 75 MHz, doorbell -> fence), wrapping */
    uint32_t fab_kick_max;      /* the longest submission, GPU clocks                                     */
    uint32_t fab_l2inv;         /* L2_INV writes (texels written by the ARM since the last kick)           */
    uint32_t fab_errors;        /* submissions after which ERR was non-zero (cleared each time)           */
    uint32_t fab_err_last;      /* the last non-zero ERR                                                  */
    uint32_t fab_hangs;
    uint32_t fab_wait_polls;    /* FENCE_DONE polls a wait spent (the bounded spin of WAIT, the drains)   */
    uint32_t chunk_waits;
    uint32_t prof_draw;         /* DRAW: vertex fetch, conversion, cull, clip, binning (+ a rare cut inside) */
    uint32_t prof_submit;       /* passes closed: COMMITs, LINKs, the directory, the cache clean, the kick  */
    uint32_t prof_present;      /* private targets converted into the real bitmap at completion             */
    uint32_t prof_tex;          /* TEX_ALLOC / TEX_UPDATE: texel conversion, mip levels                      */
    uint32_t prof_wait;         /* waiting for the GPU: WAIT's bounded spin, drains (the backend's polls)    */
    uint32_t fence_tries;       /* RTG queue gates asked (a3d_fence_try) that found the GPU still working  */
    uint32_t frame_guard_waits; /* sets that waited for the context's previous frame to be presented       */
    uint32_t q_covers;          /* ZZ9000 WaitBlitter answered "idle" with blits still waiting (AUTO)      */
    uint32_t q_faults;          /* CPU accesses to their memory: the 3D work and the queue finished first  */
    uint32_t q_dma;             /* DMA hand-offs (bigmigHD / Share) of their memory: the same, no fault    */
    uint32_t q_refused;         /* covers the protection refused (WaitBlitter waited as before)            */
    uint32_t q_gated;
    uint32_t q_gate_waits;      /* queue looks that found the head's gate shut                             */
    uint32_t q_gate_blocks;     /* gates a synchronous path cleared (blocking: a drain, a full queue)      */
    uint32_t q_tmpl;            /* template / pattern ops (text) queued behind 3D work instead of draining */
    uint32_t fab_cswap_sets;
    uint32_t vp_draws;          /* triangle draws that ran the vertex pass                                  */
    uint32_t vp_verts;          /* vertices it fetched and packed (once each)                               */
    uint32_t vp_refused;        /* triangle draws of the one-struct plan it did not take (range, density)   */
    uint32_t direct_cleans;     /* sets rendering a target in place on the GPU: its bands cleaned + invalidated */
    uint32_t vp_neon;           /* vertices the vertex pass packed four at a time (NEON)                      */
    uint32_t vp_redo;           /* ... lanes the reference redid (a NEON flush to zero: a3d_draw.c a3d_pack4)  */
    uint32_t vp_scalar;         /* ... vertices packed by the reference alone (no NEON: the 68k library)       */
    uint32_t vp_batches;        /* ... batches of up to 64                                                     */
    uint32_t tgt_kept;          /* target changes that kept the buffers (the same geometry: a double buffer's flip) */
    uint32_t tgt_alloc;         /* ... that allocated them (the first target, another size)                   */
    uint32_t priv_fill_rows;    /* private-buffer rows filled from the target (a band LOADed, not cleared)    */
    uint32_t parks;
    uint32_t park_finishes;     /* parked work finished early: a CPU touch, a 2D fence, an eviction            */
    uint32_t priv_guard_waits;  /* a set or a fill for one target waited for another target's present          */
    uint32_t display_waits;     /* a set waited for a deferred flip away from its target (a3d_display_guard)    */
} a3d_stats_t;
#define A3D_STATS_M1A_BYTES     104u
extern a3d_stats_t a3d_cnt;

#if defined(__arm__) && !defined(__APPLE__) && !defined(A3D_HEAP_LIB68K)
#define A3D_LATE_TEXT   __attribute__((section(".a3d_late_text")))
#define A3D_LATE_BSS    __attribute__((section(".bss.a3d_late")))
#else
#define A3D_LATE_TEXT
#define A3D_LATE_BSS
#endif
#define A3D_LATE_CALL   A3D_LATE_TEXT __attribute__((noinline))
#if defined(__arm__) && !defined(__APPLE__) && !defined(A3D_HEAP_LIB68K)
#define A3D_TAIL_TEXT   __attribute__((section(".a3d_tail_text")))
#define A3D_TAIL_RODATA __attribute__((section(".a3d_tail_rodata")))
#else
#define A3D_TAIL_TEXT
#define A3D_TAIL_RODATA
#endif
#define A3D_TAIL_CALL   A3D_TAIL_TEXT __attribute__((noinline))
#if defined(__arm__) && !defined(__APPLE__) && !defined(A3D_HEAP_LIB68K)
#define A3D_R8_BSS      __attribute__((section(".bss.~a3d_r8")))
#else
#define A3D_R8_BSS
#endif

typedef struct a3d_mgl_stats
{
    uint32_t batches;
    uint32_t tris_in;           /* triangles they carried into the geometry stage                             */
    uint32_t culled;            /* ... the stage culled (the face, a degenerate one) or rejected (outside)       */
    uint32_t clipped;           /* ... the stage clipped (Sutherland-Hodgman against the crossed planes)        */
    uint32_t tris_out;          /* screen triangles the stage handed to a3d_emit (a clipped one may be several)  */
} a3d_mgl_stats_t;
extern a3d_mgl_stats_t a3d_mcnt;

typedef struct a3d_r8_stats
{
    uint32_t pbuf_allocs;       /* second private buffers allocated (a flip target's own)                      */
    uint32_t pbuf_refused;      /* ... refused (the pool would keep less than A3D_PBUF_RESERVE): shared        */
    uint32_t pbuf_shares;
    uint32_t pbuf_trims;        /* buffers given back for a texture the pool refused                           */
    uint32_t pbuf_own_waits;    /* band fills that waited for their target's own older present (A, B, A)       */
    uint32_t state_skips;       /* draws whose Warp3D state was unchanged: a3d_state skipped                   */
    uint32_t state_full;        /* ... translated in full                                                      */
    uint32_t polls_quick;       /* polls that found nothing new (the leaner ONCALL entry)                      */
    uint32_t stage_nc;          /* 1: the band lists' staging is non-cacheable (a3d_fw.c a3d_fw_stage_nc)       */
} a3d_r8_stats_t;
extern a3d_r8_stats_t a3d_r8cnt;
extern uint32_t a3d_state_epoch;
extern uint32_t a3d_state_full_on;
extern uint32_t a3d_pbuf_shared_on;
uint32_t a3d_pbuf_trim(void);         /* a refused texture: the second buffers back to the pool         */

int a3d_init(uint32_t backend);
int a3d_ready(void);

#define A3D_WAIT_AUTO           0u
#define A3D_WAIT_EXACT          1u
extern uint32_t a3d_wait_mode;
extern uint32_t a3d_wait_budget;
extern uint32_t a3d_prot_enabled;
#define A3D_TOUCH_FAULT         1u      /* the guest CPU took a protection fault                        */
#define A3D_TOUCH_FENCE         2u
#define A3D_TOUCH_SHARED        0x100u
uint32_t a3d_touch(uint32_t lo, uint32_t hi, uint32_t why);
uint32_t a3d_fence_of(uint32_t lo, uint32_t hi);
uint32_t a3d_fence_presented(uint32_t fence);
uint32_t a3d_fence_try(uint32_t fence);
void a3d_fence_wait(uint32_t fence);
uint32_t a3d_prot_count(void);
/* the number of contexts with a lazy promise outstanding (unfinished work the CPU was promised) */
uint32_t a3d_lazy_count(void);
void a3d_oncall(uint32_t id, uint32_t regs[4], uint32_t via_trap);
uint32_t a3d_state_hash(void);
void a3d_reset_all(void);       /* drop every context and texture (a guest reboot) */

struct a3d_ctx
{
    uint32_t used, gen;
    uint32_t gctx, gpriv;           /* guest W3D_Context and its private block                       */
    a3d_target tgt;                 /* the target the open pass (if ours) renders into               */
    uint32_t tgt_valid;
    uint8_t *depth;                 /* the DDR depth buffer (pool), on demand; 16-bit little-endian  */
    uint32_t depth_phys, depth_w, depth_h;
    uint32_t depth_stride;
    uint64_t depth_band_valid;      /* bands whose depth lives in `depth`                            */
    uint64_t priv_stale;
    uint32_t fence_sub, fence_done;
    uint32_t frames, errors;
    uint32_t never_ended;           /* no FRAME/TARGET/Z clear seen yet (the unlock fallback)        */
    uint32_t lazy, lazy_fence;      /* the work through lazy_fence is promised to the CPU            */
    uint32_t prot;                  /* bit s: slot s is registered (key = index * A3D_PROT_SLOTS + s)  */
    uint32_t prot_cur;              /* the CURRENT target's slot + 1; 0 = the current target has none */
    uint32_t prot_base[A3D_PROT_SLOTS], prot_lo[A3D_PROT_SLOTS], prot_hi[A3D_PROT_SLOTS];
                                    /* bitmap base (2D fences match surfaces from it) and rows, per slot */
    uint32_t prot_used[A3D_PROT_SLOTS];     /* when each slot was last current (the eviction order)   */
    uint32_t prot_fence[A3D_PROT_SLOTS];
    uint32_t priv_fence, priv_guest;
    uint32_t frame_passes;          /* passes submitted since the last frame end (the frame hash)    */
    uint32_t last_frame_fence;
    uint32_t wait_retry;            /* the last WAIT answered "not yet": the next one is the stub's retry */
    /* what the binner last received from this context (only changed values are passed on) */
    uint32_t reg_val[16];
    uint32_t reg_set;
    uint32_t tex_bound;             /* the texture handle of the stream last sent                    */
    uint32_t tex_ugen;
    uint32_t fog_key[6];            /* mode, start, end, density bits, the w scale exponent, valid    */
    uint32_t fog_lut[RIX_FOG_LUT_WORDS];
    uint32_t clear_color, clear_depth;
};
typedef struct a3d_ctx a3d_ctx;

/* the pass: the one list set being binned (it belongs to one context and one target at a time) */
#define A3D_BT_HEADER   1u          /* band header emitted                                            */
#define A3D_BT_COLOR    2u          /* colour initialised (LOAD, or a MEMSET that follows)            */
#define A3D_BT_DEPTH    4u          /* depth initialised                                              */
#define A3D_BT_CDIRTY   8u          /* colour written: COMMIT colour at the end of the pass           */
#define A3D_BT_ZDIRTY   16u         /* depth written: COMMIT depth (it outlives the pass)             */
typedef struct a3d_pass
{
    uint32_t open;
    uint32_t ctx;                   /* context index                                                  */
    uint32_t fence;                 /* the fence it will carry                                        */
    uint32_t band[A3D_MAX_BANDS];   /* A3D_BT_* per band                                              */
    uint32_t staged;                /* bytes binned so far                                            */
    uint32_t depth_used;            /* some band initialised depth                                    */
    uint32_t work;
} a3d_pass;
extern a3d_pass a3d_P;
extern a3d_binner a3d_B;
extern uint32_t a3d_vp_off;
extern a3d_ctx a3d_ctxs[A3D_MAX_CTX];

/* open a pass on c's current target if none is open; submit another context's pass first */
int a3d_pass_ensure(a3d_ctx *c);
#define A3D_PROF_DRAW       0u
#define A3D_PROF_SUBMIT     1u
#define A3D_PROF_PRESENT    2u
#define A3D_PROF_TEX        3u
#define A3D_PROF_WAIT       4u
void a3d_prof_begin(uint32_t b);
void a3d_prof_end(uint32_t b, uint32_t *bucket);
void a3d_fbstate_begin(a3d_ctx *c);
void a3d_fbstate_end(a3d_ctx *c);
void a3d_band_prepare(a3d_ctx *c, uint32_t band, uint32_t need_color, uint32_t need_depth,
                      uint32_t clearing_color, uint32_t clearing_depth);
/* make room before binning: 1 = room for a triangle in every band; 0 = submitted (cut) first */
int a3d_room(a3d_ctx *c);
/* submit c's open pass (if any); returns its fence (or the last one).  why = A3D_SUB_* */
#define A3D_SUB_OTHER   0u
#define A3D_SUB_FRAME   1u          /* W3D_FlushFrame: the frame ends                                */
#define A3D_SUB_WAIT    2u          /* an exact wait cut the frame                                   */
#define A3D_SUB_TOUCH   3u
uint32_t a3d_submit(a3d_ctx *c, uint32_t why);
/* run the backend until `fence` is complete (UNBOUNDED: CTX_DESTROY, Z reads) and process completions */
void a3d_drain_to(uint32_t fence);
/* a register value for the binner, passed on only when it changed */
void a3d_reg(a3d_ctx *c, uint32_t addr, uint32_t value);
int32_t a3d_draw(a3d_ctx *c, uint32_t kind, uint32_t a1);
int32_t a3d_clear(a3d_ctx *c, uint32_t what, uint32_t d1, uint32_t a1);
int32_t a3d_zspan(a3d_ctx *c, uint32_t op, uint32_t a1);
int32_t a3d_tex_alloc(a3d_ctx *c, uint32_t gtex, uint32_t flags);
int32_t a3d_tex_free(a3d_ctx *c, uint32_t gtex, uint32_t all);
int32_t a3d_tex_update(a3d_ctx *c, uint32_t kind, uint32_t a1);
void a3d_tex_retired_flush(void);
void a3d_tex_init(void);
void a3d_tex_drop_ctx(uint32_t ctxi);
void a3d_tex_completed(uint32_t fence);
uint32_t a3d_tex_hash(void);
/* the texture a draw binds: its RasterIX TMU register value and its TEXTURE_STREAM; 0 = not usable */
typedef struct a3d_texobj a3d_texobj;
a3d_texobj *a3d_tex_lookup(uint32_t gtex);
uint32_t a3d_tex_tmureg(const a3d_texobj *t, uint32_t gtex);
uint32_t a3d_tex_stream(const a3d_texobj *t, uint32_t *pages, uint32_t max);
void a3d_tex_size(const a3d_texobj *t, float *w, float *h);
void a3d_tex_used(a3d_texobj *t, uint32_t fence);
uint32_t a3d_tex_handle(const a3d_texobj *t);   /* (gen << 16) | (index + 1); 0 = none */
uint32_t a3d_tex_ugen(const a3d_texobj *t);     /* the upload generation: bumped when a TEX_UPDATE renames */
uint32_t a3d_tex_lvl_off(uint32_t lw, uint32_t lh, uint32_t l);
extern uint32_t a3d_tex_l2_dirty;

#ifdef A3D_HEAP_LIB68K
#define A3D_PBUFS           1u
#else
#define A3D_PBUFS           2u
#endif
#define A3D_PBUF_RESERVE    (2u << 20)  /* the pool a second buffer must leave free (textures come first)          */
typedef struct a3d_pbuf
{
    uint8_t *va;            /* the pool block (0: the slot is free)                                             */
    uint32_t phys;
    uint32_t guest;
    uint32_t fence;         /* the last set whose present reads it (0: none) ...                                */
    uint32_t pguest;        /* ... and that present's target                                                    */
    uint32_t refill;        /* its own target's present still owed when it was bound again (the fill waits for it) */
    uint32_t used;          /* when it was last bound (the shared fallback takes the oldest)                    */
} a3d_pbuf;
a3d_pbuf *a3d_pbuf_cur(const a3d_ctx *c);               /* c's current private buffer's slot (0: direct)    */
void a3d_pbuf_presented(const a3d_ctx *c, uint32_t fence); /* a set whose present reads c's buffer was submitted */
void a3d_pbuf_free_all(a3d_ctx *c);                      /* every private buffer of c back to the pool        */
void a3d_pbuf_first(a3d_ctx *c);                         /* c's newly allocated buffer becomes its slot 0     */
void a3d_pbuf_pick(a3d_ctx *c, a3d_target *t);           /* a kept target: its own buffer, a new one, or shared */
void a3d_pbuf_reset(void);                               /* a3d_init                                          */
void a3d_priv_fill_ref(const a3d_target *t, uint32_t y0, uint32_t y1, uint32_t le);
int a3d_tgt_cached(const a3d_ctx *c);                    /* the open pass's target words unchanged            */
void a3d_tgt_cache(const a3d_ctx *c);                    /* ... remember them (and c->tgt)                    */
void a3d_poll_quick(void);                               /* a poll that found nothing completed               */
void a3d_poll_noted(void);                               /* a full poll wrote every context's PENDING word    */
extern uint32_t a3d_poll_dirty;                          /* a texture freed / retired: the next poll walks them */

#endif /* A3D_AGENT_H */
