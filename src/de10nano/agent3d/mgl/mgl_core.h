/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2 GL core: the OpenGL 1.1 subset of the minigl.library interface. */

#ifndef MGL_CORE_H
#define MGL_CORE_H

#include <stdint.h>
#include <stddef.h>
#include "mgl_abi.h"
#include "mgl_stage.h"

/* the raster state the sink needs for the triangles that follow (only what changes the pixels) */
typedef struct mgl_raster
{
    uint32_t tex;                   /* the bound texture's sink handle, 0 = untextured                        */
    uint32_t texenv;                /* GL_MODULATE / GL_DECAL / GL_REPLACE / GL_BLEND                         */
    float    envcolor[4];
    uint32_t min_filter, mag_filter, wrap_s, wrap_t;   /* the bound texture's (GL enums)                     */
    uint32_t blend;                 /* GL_BLEND enabled                                                       */
    uint32_t blend_src, blend_dst;  /* GL factors                                                             */
    uint32_t depth_test, depth_func, depth_mask;
    uint32_t alpha_test, alpha_func;
    float    alpha_ref;
    uint32_t fog, fog_mode;
    float    fog_start, fog_end, fog_density, fog_color[4];
    float    fog_wstart, fog_wend;       /* the same in Warp3D's w-space: 1 / w_clip at those eye distances, [0, 1] */
    uint32_t scissor;               /* GL_SCISSOR_TEST enabled; the box below in sink coordinates (y down)    */
    int32_t  sc_x, sc_y, sc_w, sc_h;
    uint32_t color_mask;            /* bit 0 r, 1 g, 2 b, 3 a                                                 */
    uint32_t dither;
    uint32_t persp;
} mgl_raster;

/* a texture level as the core keeps it: canonical layouts only */
#define MGL_TF_RGBA8    1u          /* r, g, b, a bytes                                                       */
#define MGL_TF_RGB8     2u          /* r, g, b                                                                */
#define MGL_TF_L8       3u          /* luminance                                                              */
#define MGL_TF_A8       4u          /* alpha                                                                  */
#define MGL_TF_LA8      5u          /* luminance, alpha                                                       */
#define MGL_TF_I8       6u          /* intensity                                                              */
#define MGL_MAX_LEVELS  12u
typedef struct mgl_level
{
    uint32_t w, h;
    uint8_t *px;                    /* w * h * bytes(format), rows packed, owned by the core (sink allocator) */
} mgl_level;

typedef struct mgl_sink
{
    void *ud;
    /* memory for texture images (the library: AllocVec in Fast RAM; the host: malloc) */
    void *(*alloc)(void *ud, uint32_t bytes);
    void  (*free)(void *ud, void *p, uint32_t bytes);
    uint32_t (*tex_create)(void *ud, uint32_t fmt, uint32_t nlevels, const mgl_level *lv);
    void     (*tex_update)(void *ud, uint32_t handle, uint32_t fmt, uint32_t level, const mgl_level *lv,
                           uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    void     (*tex_free)(void *ud, uint32_t handle);
    /* drawing: the raster state (whenever it changed), then indexed triangles, lines, points */
    void (*state)(void *ud, const mgl_raster *r);
    void (*tris)(void *ud, const mgl_vtx *v, uint32_t nv, const uint16_t *idx, uint32_t ntri);
    void (*lines)(void *ud, const mgl_vtx *v, uint32_t nlines);         /* v[2i], v[2i+1]                        */
    void (*points)(void *ud, const mgl_vtx *v, uint32_t npoints, float size);
    void (*clear)(void *ud, uint32_t mask, const float rgba[4], float depth, const mgl_raster *r);
    void (*flush)(void *ud, uint32_t finish);                            /* glFlush 0, glFinish 1                 */
    void (*tris_object)(void *ud, const mgl_cvtx *v, uint32_t ntri, const mgl_geom *g);
} mgl_sink;

#define MGL_MV_DEPTH        32u
#define MGL_PROJ_DEPTH      4u
#define MGL_TEX_DEPTH       4u
#define MGL_UNITS           2u
#define MGL_IMM_MAX         4096u           /* vertices of one glBegin/glEnd kept at once (longer ones flush in parts)  */
#define MGL_BATCH_VTX       4096u           /* screen-space vertices of one batch to the sink                         */
#define MGL_BATCH_TRI       4096u
#define MGL_MAX_TEXNAMES    2048u
#define MGL_DEFER_TRI       1024u

typedef struct mgl_m4 { float m[16]; } mgl_m4;                  /* column-major, GL's order */

typedef struct mgl_texobj
{
    uint32_t used;
    uint32_t fmt;
    uint32_t nlevels;
    mgl_level lv[MGL_MAX_LEVELS];
    uint32_t handle;                        /* the sink's, 0 = not created / refused                             */
    uint32_t dirty;                         /* levels changed since the sink last saw them (create pending)      */
    uint32_t min_filter, mag_filter, wrap_s, wrap_t;
    float    priority;
} mgl_texobj;

typedef struct mgl_array
{
    GLint size;
    GLenum type;
    GLsizei stride;
    const uint8_t *ptr;
    uint32_t enabled;
} mgl_array;

#define MGL_MAX_LIGHTS      8u
typedef struct mgl_light
{
    float amb[4], dif[4], spe[4];
    float pos[4];                           /* eye coordinates                                                   */
    float dir[3];                           /* eye coordinates (the modelview's upper 3x3)                       */
    float exp, cut;                         /* GL_SPOT_EXPONENT, GL_SPOT_CUTOFF (degrees; 180 = no spot)         */
    float att[3];                           /* constant, linear, quadratic                                       */
} mgl_light;
typedef struct mgl_material
{
    float amb[4], dif[4], spe[4], emi[4];
    float shi;
} mgl_material;
typedef struct mgl_dlist
{
    uint32_t *w;
    uint32_t n;                             /* words used                                                        */
    uint32_t cap;                           /* words allocated                                                   */
    uint32_t used;                          /* the name is in use (glGenLists / glEndList)                       */
} mgl_dlist;

struct mgl_context
{
    mgl_sink sink;
    int32_t  width, height;                 /* the drawable                                                      */
    GLenum   error;

    /* matrices */
    GLenum   matrix_mode;
    mgl_m4   mv[MGL_MV_DEPTH], proj[MGL_PROJ_DEPTH], texm[MGL_TEX_DEPTH];
    uint32_t mv_sp, proj_sp, tex_sp;
    mgl_m4   mvp;                           /* proj x mv, recomputed when dirty                                 */
    uint32_t mvp_dirty;
    uint32_t tex_identity;                  /* the texture matrix is the identity (skip it)                     */
    int32_t  vp_x, vp_y, vp_w, vp_h;
    float    depth_n, depth_f;

    /* current attributes */
    float    cur_color[4];
    float    cur_tex[MGL_UNITS][4];
    float    cur_normal[3];
    GLenum   active_unit, client_unit;

    /* immediate mode */
    uint32_t in_begin;
    GLenum   prim;
    mgl_cvtx imm[MGL_IMM_MAX];
    uint32_t nimm;
    uint32_t imm_total;                     /* vertices of this primitive so far (across partial flushes)       */
    mgl_cvtx loop_first;                    /* GL_LINE_LOOP's first vertex once a partial flush moved it out    */
    uint32_t loop_saved;

    /* vertex arrays */
    mgl_array va_vertex, va_color, va_tex[MGL_UNITS];

    /* state */
    uint32_t en_blend, en_depth, en_alpha, en_cull, en_tex2d[MGL_UNITS], en_fog, en_scissor, en_dither;
    uint32_t en_polyoffset;
    uint32_t en_persp;
    GLenum   blend_src, blend_dst, blend_eq;
    GLenum   depth_func;
    uint32_t depth_mask;
    GLenum   alpha_func;
    float    alpha_ref;
    GLenum   cull_mode, front_face, shade_model;
    GLenum   texenv[MGL_UNITS];
    float    envcolor[MGL_UNITS][4];
    GLenum   fog_mode;
    float    fog_start, fog_end, fog_density, fog_color[4];
    int32_t  sc_x, sc_y, sc_w, sc_h;
    uint32_t color_mask;
    float    clear_color[4];
    float    clear_depth;
    GLenum   persp_hint, fog_hint;
    float    point_size, line_width;
    float    zoffset;                       /* MGLSetZOffset: added to window z                                  */
    float    po_factor, po_units;
    GLint    unpack_align, unpack_row;
    uint8_t  palette[256][4];

    /* textures */
    uint32_t bound[MGL_UNITS];              /* GL names                                                          */
    mgl_texobj tex[MGL_MAX_TEXNAMES];       /* indexed by name (0 = the default texture, never created)         */

    /* the batch to the sink */
    mgl_raster rs;                          /* what the sink has                                                 */
    uint32_t   rs_valid;
    uint32_t   rs_dirty;                    /* the GL state changed since the sink got rs                        */
    mgl_vtx    bv[MGL_BATCH_VTX];
    uint16_t   bi[3u * MGL_BATCH_TRI];
    uint32_t   nbv, nbt;

    mgl_geom   geom;
    uint32_t   geom_dirty;
    uint32_t   deferred;
    mgl_cvtx  *dv;
    uint32_t   ndt;                         /* deferred triangles held                                           */
    uint32_t   geom_flat;
    mgl_stage_out stage_out;                /* the immediate path's output: the batch above                      */

    uint32_t st_tris_in, st_tris_culled, st_tris_clipped, st_batches, st_state, st_verts;
    uint32_t st_defer_batches;
    uint32_t st_rs_syncs;
    uint32_t st_verts_array;                /*   vertices from vertex arrays (st_verts: all of them)                 */
    uint32_t st_tris_flat, st_tris_flat_def;    /* MGL_FLATFAN / _FLATSTRIP triangles; of them, through the deferred batch */
    uint32_t st_draws_fast, st_draws_generic;
    uint32_t arr_generic;
    uint32_t st_glerrors;
    GLenum   st_glerror_first;              /*   glGetError only ever shows one; the trace shows these (mgl_amiga.c)   */
};

typedef struct mgl_ctx5
{
    GLcontext owner;
    struct mgl_ctx5 *next;
    mgl_array va_normal;                    /* GL_NORMAL_ARRAY                                                   */
    uint32_t en_lighting, en_color_material, en_normalize;
    uint32_t en_light[MGL_MAX_LIGHTS];
    GLenum   cm_face, cm_mode;              /* glColorMaterial                                                   */
    mgl_light light[MGL_MAX_LIGHTS];
    mgl_material mat[2];                    /* front, back                                                       */
    float    lm_ambient[4];
    uint32_t lm_local_viewer, lm_two_side;
    mgl_dlist *dl;                          /* the display lists by name, dl_max of them (sink memory)            */
    uint32_t dl_max;
    uint32_t dl_cur;                        /* the list being compiled (0 = none)                                */
    uint32_t dl_exec;                       /* GL_COMPILE_AND_EXECUTE                                            */
    uint32_t dl_depth;                      /* glCallList nesting while one replays                              */
    mgl_dlist dl_new;                       /* what the list being compiled holds so far                         */
    uint32_t dl_oom;                        /* its memory ran out (GL_OUT_OF_MEMORY once; the list ends empty)    */
    uint32_t lt_ready;
    float    lt_mv[16];                     /* the modelview it was made under                                   */
    float    lt_nm[9];                      /* its normal matrix: the upper 3x3's inverse, transposed (rows)      */
    float    lt_sdir[MGL_MAX_LIGHTS][3];    /* each light's spot direction, unit                                 */
    float    lt_scos[MGL_MAX_LIGHTS];       /* the cosine of its cutoff                                          */
    float    lt_l[MGL_MAX_LIGHTS][3];       /* a directional light's unit direction                              */
    float    lt_h[MGL_MAX_LIGHTS][3];       /* ... and its half vector with the viewer at infinity               */
    uint32_t mt_live;                       /* something kept, or a primitive being kept: the vertex entries' slow test */
    uint32_t mt_rec;                        /* this primitive is being kept                                      */
    uint32_t mt_env1_set;
    uint32_t mt_args_known;                 /* mglDrawMultitexBuffer's last arguments (MiniGL 1.2's protocol)     */
    uint32_t mt_called_frame;
    GLenum   mt_bs, mt_bd, mt_env;
    struct mgl_mtex_v *mt_pool;             /* the vertices: object position, unit 1's s t, colour               */
    uint32_t mt_npool, mt_cappool;
    struct mgl_mtex_t *mt_tri;              /* the triangles: three pool indices and the run                     */
    uint32_t mt_ntri, mt_captri;
    struct mgl_mtex_run *mt_run;            /* the states they were drawn under                                  */
    uint32_t mt_nrun, mt_caprun, mt_cur;    /* (mt_cur: this primitive's run)                                    */
    uint16_t *mt_order;                     /* the pass's index list, by run (3 a triangle)                      */
    uint32_t mt_caporder;
    GLenum   mt_prim;                       /* the primitive being kept, and its triangulation's window:          */
    uint32_t mt_k;                          /*   vertices so far,                                                */
    uint32_t mt_w[3], mt_c0;                /*   the last three (pool indices, by place mod 3) and a fan's centre  */
    uint32_t mt_dropped;                    /* triangles a full buffer could not keep (counted, not drawn)        */
    uint32_t mt_passes;                     /* passes drawn (tests, traces)                                       */
    uint32_t mt_generic;
    int32_t  pack_align, pack_row, pack_skip_rows, pack_skip_pixels;
    struct mgl_ds *ds;
    uint32_t nds, capds;
    uint32_t tg_en[2], tg_on;               /* GL_TEXTURE_GEN_S / _T, and either                                */
    uint32_t tg_en_rq[2];                   /* GL_TEXTURE_GEN_R / _Q: kept (the standard names no R / Q coordinate) */
    GLenum   tg_mode[2];                    /* GL_OBJECT_LINEAR / GL_EYE_LINEAR / GL_SPHERE_MAP                 */
    float    tg_obj[2][4], tg_eye[2][4];    /* the planes; the eye plane already through the modelview's inverse */
    uint32_t tg_ready;                      /* tg_nm made for tg_mv                                             */
    float    tg_mv[16], tg_nm[9];           /* the modelview the normal matrix was made from; its inverse, transposed */
    uint32_t (*tc_create)(void *ud, uint32_t fmt, uint32_t nlevels, const mgl_level *lv);
    void     (*tc_state)(void *ud, const mgl_raster *r);
    uint32_t (*tc_why)(void *ud, uint32_t fmt, uint32_t nlevels, const mgl_level *lv, uint32_t *nomem);
    uint32_t *tc_stamp;                     /* by texture name: the clock when it was last drawn (the sink's memory)  */
    uint32_t tc_clock;                      /* raster states sent                                                 */
    uint32_t tc_cur;                        /* the texture the last raster state drew with (0: none)               */
    uint32_t tc_evicted, tc_refused;        /* textures released for room; creations refused even so (tests, traces) */
    uint32_t m2b;                           /* the engine's handle for this context (0: not in the engine)        */
    int (*m2b_submit)(GLcontext c, const uint32_t *w, uint32_t n, uint32_t flags);
    int (*m2b_query)(GLcontext c, uint32_t *out16);   /* the engine's error and current attributes (16 words) */
    uint32_t *m2b_sent;                     /* by texture name: the handle last sent to the engine               */
    uint32_t m2b_submits, m2b_words;        /* buffers sent, words sent (tests, traces)                         */
    uint32_t m2b_tpend;
    uint32_t m2b_arrops;
    uint32_t m2b_arrfb, m2b_arrwhy;
} mgl_ctx5;
#define MGL_DLX_VERTEX  1u
#define MGL_DLX_STATE   2u                  /*   ... and the rest; GL_COMPILE_AND_EXECUTE applies both            */
#define MGL_M2B_LIST    0xFFFFFFFFu         /* dl_cur while the context's GL runs in the engine (never a list name) */
#define MGL_M2B_SWAP    1u                  /* submit flags: the buffer switch follows (the kept unit-1 pass drawn) */
#define MGL_M2B_SYNC    2u                  /*   the caller waits for the drawing (glFinish, a read of the buffer) */
#define MGL_M2B_END     4u
#define MGL_M2B_WORDS   4096u               /* the command buffer (16 KB): a call that does not fit sends it first  */
typedef struct mgl_ds { uint32_t name, ow, oh; } mgl_ds;      /* a texture name, its level 0 as the program gave it */
typedef struct mgl_mtex_v
{
    float x, y, z, w;
    float s, t;
    float r, g, b, a;
} mgl_mtex_v;
typedef struct mgl_mtex_t
{
    uint16_t v[3];                          /* pool indices, the core's vertex order                              */
    uint16_t run;
} mgl_mtex_t;
typedef struct mgl_mtex_snap
{
    uint32_t tex;                           /* unit 1's texture (a GL name)                                       */
    GLenum   env1;                          /* unit 1's texture environment                                      */
    uint32_t env1_set;                      /* ... set by the program (else the call's arguments)                */
    float    envcolor1[4];
    mgl_m4   mv, proj;                      /* the geometry                                                       */
    int32_t  vp_x, vp_y, vp_w, vp_h;
    float    depth_n, depth_f, zoffset;
    uint32_t en_cull;
    GLenum   cull_mode, front_face, shade_model;
    uint32_t en_depth;                      /* the raster state the pass keeps                                    */
    GLenum   depth_func;
    uint32_t depth_mask, en_alpha;
    GLenum   alpha_func;
    float    alpha_ref;
    uint32_t en_fog;
    GLenum   fog_mode;
    float    fog_start, fog_end, fog_density, fog_color[4];
    uint32_t en_scissor;
    int32_t  sc_x, sc_y, sc_w, sc_h;
    uint32_t color_mask, en_dither, en_persp;
} mgl_mtex_snap;
typedef struct mgl_mtex_run
{
    mgl_mtex_snap st;
    uint32_t first, count;                  /* its triangles in mt_order (made at the pass)                      */
} mgl_mtex_run;
int  mgl_mtex_unit1(GLcontext c);               /* unit 1 draws: GL_TEXTURE_2D on it, a texture with an image bound   */
void mgl_mtex_prim(GLcontext c, int unit1);     /* a primitive's first vertex: kept from here (unit 1), or the pass first */
void mgl_mtex_vertex(GLcontext c, float x, float y, float z, float w);    /* after the vertex was drawn             */
void mgl_mtex_element(GLcontext c, GLint i);    /* ... an array element (glArrayElement)                              */
void mgl_mtex_arrays(GLcontext c, GLenum mode, GLint first, GLsizei count, GLenum type, const GLvoid *indices);
#define MGL_MT_CALL  0u                         /* the pass: mglDrawMultitexBuffer                                    */
#define MGL_MT_PRIM  1u                         /*   a primitive drawn without unit 1 follows                        */
#define MGL_MT_CLEAR 2u                         /*   glClear follows                                                  */
#define MGL_MT_SWAP  3u                         /*   the buffer switch follows                                         */
#define MGL_MT_ROOM  4u
#define MGL_MT_MATRIX 5u
void mgl_mtex_flush(GLcontext c, uint32_t why);
void mgl_mtex_draw(GLcontext c, const mgl_mtex_v *pool, const uint16_t *idx, uint32_t ntri);
void mgl_mtex_call(GLcontext c, GLenum bsrc, GLenum bdst, GLenum env);
void mgl_mtex_free(GLcontext c, mgl_ctx5 *x5);
void GLClear_a5(GLcontext c, GLbitfield mask);
typedef struct mgl_rb
{
    const uint8_t *base;
    uint32_t pitch, width, height, swapped;
} mgl_rb;
extern int (*mgl_rb_lock_fn)(GLcontext c, mgl_rb *rb);
extern void (*mgl_rb_unlock_fn)(GLcontext c);
typedef struct mgl_pack { int32_t align, row, skip_rows, skip_pixels; } mgl_pack;
int  mgl_readpx_convert(const mgl_rb *rb, int32_t win_h, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,
                        GLenum type, const mgl_pack *pk, void *pixels);    /* 0 or the GL error (nothing written)     */
void mgl3_GLReadPixels(GLcontext c, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, GLvoid *pixels);
void GLReadPixels_a5(GLcontext c, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, GLvoid *pixels);
void GLPixelStorei_a5(GLcontext c, GLenum pname, GLint param);
void mgl_ds_TexImage2D(GLcontext c, GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border,
                       GLenum format, GLenum type, const GLvoid *pixels);
void mgl_ds_TexSubImage2D(GLcontext c, GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                          GLenum format, GLenum type, const GLvoid *pixels);
void mgl_ds_free(GLcontext c, mgl_ctx5 *x5);
typedef uint32_t (*mgl_tc_why_fn)(void *ud, uint32_t fmt, uint32_t nlevels, const mgl_level *lv, uint32_t *nomem);
void mgl_tc_attach(GLcontext c, mgl_tc_why_fn why);
void mgl_tc_detach(GLcontext c, mgl_ctx5 *x5);
mgl_ctx5 *mgl_x5_of_ud(void *ud);              /* the block whose context's sink is ud (0: none -- never makes one)  */
int  mgl_m2b_begin(GLcontext c, uint32_t handle, int (*submit)(GLcontext, const uint32_t *, uint32_t, uint32_t),
                   int (*query)(GLcontext, uint32_t *));
int  mgl_m2b_submit(GLcontext c, uint32_t flags);
void mgl_m2b_sync(GLcontext c);
void mgl_m2b_end(GLcontext c);
int  mgl_m2b_replay(GLcontext c, const uint32_t *w, uint32_t n, uint32_t flags);
void mgl_m2b_query(GLcontext c, uint32_t out16[16]);
void mgl_m2b_tex(GLcontext c, uint32_t name);
void mgl_m2b_tpend(GLcontext c);                /* the library side: a bound name's texture made, its handle sent */
void mgl_m2b_GLDeleteTextures(GLcontext c, GLsizei n, const GLuint *names);
void mgl_m2b_GLFlush(GLcontext c);
void mgl_m2b_mtexcall(GLcontext c, GLenum bsrc, GLenum bdst, GLenum env);
void mgl_m2b_GLFinish(GLcontext c);
extern void (*mgl_m2b_patchfn)(int on);
void GLTexGeni_a5(GLcontext c, GLenum coord, GLenum pname, GLenum param);
void GLTexGenfv_a5(GLcontext c, GLenum coord, GLenum pname, const GLfloat *p);
void mgl_texgen(GLcontext c, mgl_ctx5 *x5, float x, float y, float z, float w, float tc[4]);
void mgl_texgen_defaults(mgl_ctx5 *x5);
void GLBindTexture_a5(GLcontext c, GLenum target, GLuint texture);
void GLPushMatrix_a5(GLcontext c);
void GLPopMatrix_a5(GLcontext c);
void GLLoadIdentity_a5(GLcontext c);
void GLLoadMatrixf_a5(GLcontext c, const GLfloat *m);
void GLLoadMatrixd_a5(GLcontext c, const GLdouble *m);
void GLTexEnvi_a5(GLcontext c, GLenum target, GLenum pname, GLint param);
void GLTexEnvfv_a5(GLcontext c, GLenum target, GLenum pname, const GLfloat *p);
extern mgl_ctx5 *mgl_x5_last;
extern void (*mgl_x5_lockfn)(int on);           /* the platform's lock around the list (exec's Forbid); 0 = none     */
mgl_ctx5 *mgl_x5_find(GLcontext c);             /* the context's block, made the first time (never 0)                */
static inline mgl_ctx5 *X5(GLcontext c)
{
    mgl_ctx5 *const x = mgl_x5_last;
    if (__builtin_expect(x != 0 && x->owner == c, 1))
        return x;
    return mgl_x5_find(c);
}
void mgl_abi5_drop(GLcontext c);                /* the context's block and its display lists go (none: nothing)      */
void mgl_abi5_attach(GLcontext c);              /* a new context: a block left at its address dropped, a fresh one   */
void mgl_abi5_forget(void);

#if defined(__amigaos__)
#define MGL_A5_TEXT __attribute__((section(".text.zz5")))
#else
#define MGL_A5_TEXT
#endif

void mgl_core_init(GLcontext c, const mgl_sink *sink, int32_t width, int32_t height);
void mgl_core_free(GLcontext c);                       /* frees the texture images (through the sink)           */
void mgl_core_resize(GLcontext c, int32_t width, int32_t height);
void mgl_core_flush(GLcontext c);                      /* the pending batch to the sink                         */
void mgl_core_set_zoffset(GLcontext c, float off);
void mgl_core_set_deferred(GLcontext c, uint32_t on);

#define MGL_CORE_PROTO(i, ret, name, args) ret name args;
MGL_TABLE(MGL_CORE_PROTO)
#undef MGL_CORE_PROTO

int mgl_get_values(GLcontext c, GLenum p, float *v);   /* mgl_state.c: a state query's values as floats, their count */
uint32_t mgl_abi5_tr(uint32_t v);                       /* an ABI-5 token -> ours (generic class)                  */
uint32_t mgl_abi5_tr_blend(uint32_t v);                 /* an ABI-5 blend factor -> ours                           */
void GLLightf(GLcontext c, GLenum light, GLenum pname, GLfloat param);
void GLLightfv(GLcontext c, GLenum light, GLenum pname, const GLfloat *params);
void GLMaterialf(GLcontext c, GLenum face, GLenum pname, GLfloat param);
void GLMaterialfv(GLcontext c, GLenum face, GLenum pname, const GLfloat *params);
void GLLightModelf(GLcontext c, GLenum pname, GLfloat param);
void GLLightModelfv(GLcontext c, GLenum pname, const GLfloat *params);
void GLGetLightfv(GLcontext c, GLenum light, GLenum pname, GLfloat *params);
void GLGetMaterialfv(GLcontext c, GLenum face, GLenum pname, GLfloat *params);
void GLColorMaterial(GLcontext c, GLenum face, GLenum mode);
void GLTexEnvfv(GLcontext c, GLenum target, GLenum pname, const GLfloat *params);
void GLNormal3fv(GLcontext c, GLfloat *v);
void GLVertex2fv_a5(GLcontext c, GLfloat *v);
void GLVertex3fv_a5(GLcontext c, GLfloat *v);
void GLVertex4fv_a5(GLcontext c, GLfloat *v);
void GLColor3fv_a5(GLcontext c, GLfloat *v);
void GLColor4fv_a5(GLcontext c, GLfloat *v);
void GLColor3ubv_a5(GLcontext c, GLubyte *v);
void GLColor4ubv_a5(GLcontext c, GLubyte *v);
void GLTexCoord2fv_a5(GLcontext c, GLfloat *v);
void GLTexCoord4fv_a5(GLcontext c, GLfloat *v);
void GLMultiTexCoord2fvARB_a5(GLcontext c, GLenum unit, GLfloat *v);
void GLNormal3fv_a5(GLcontext c, GLfloat *v);
typedef struct mgl_afetched
{
    uint32_t has_col, has_vtx, tex_mask;
    float col[4], vtx[4], tex[MGL_UNITS][4];
} mgl_afetched;
void mgl_array_fetch(GLcontext c, GLint i, mgl_afetched *o);

/* mgl_math.c: deterministic float helpers (the same bits on every compiler and float implementation) */
float mgl_sinf(float x);
float mgl_cosf(float x);
float mgl_sqrtf(float x);
float mgl_tanf(float x);
float mgl_powf(float x, float y);
void mgl_light_rgba(GLcontext c, float x, float y, float z, float w, float *rgba);
void GLVertex4f_a5(GLcontext c, GLfloat x, GLfloat y, GLfloat z, GLfloat w);

#endif
