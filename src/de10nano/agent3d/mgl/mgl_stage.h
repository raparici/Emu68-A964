/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2 GL core: the geometry stage (cull, clip, to screen). */

#ifndef MGL_STAGE_H
#define MGL_STAGE_H

#include <stdint.h>

typedef struct mgl_cvtx                     /* a vertex in clip (or object) space, with its attributes */
{
    float x, y, z, w;
    float s, t;
    float r, g, b, a;
} mgl_cvtx;

typedef struct mgl_vtx                      /* the sink's screen space: y down, w = 1 / w_clip, s t normalised */
{
    float x, y, z, w;
    float s, t;
    float r, g, b, a;
} mgl_vtx;

/* the geometry a batch is drawn under */
#define MGL_GC_CULL         0x1u            /* face culling on                                             */
#define MGL_GC_CULL_FRONT   0x2u            /* cull the front faces (else the back ones)                   */
#define MGL_GC_CULL_BOTH    0x4u            /* GL_FRONT_AND_BACK                                           */
#define MGL_GC_FRONT_CW     0x8u            /* the front face is clockwise (else counter-clockwise)        */
typedef struct mgl_geom
{
    float    mvp[16];                       /* projection x modelview, column-major (GL order)             */
    int32_t  vp_x, vp_y, vp_w, vp_h;        /* glViewport                                                  */
    int32_t  height;                        /* the drawable's height (GL y up -> screen y down)            */
    float    depth_n, depth_f;              /* glDepthRange                                                */
    float    zoffset;                       /* MGLSetZOffset, added to window z                            */
    uint32_t flags;                         /* MGL_GC_*                                                    */
} mgl_geom;

/* where the stage writes: the caller's batch (screen vertices + triangles of three indices) */
typedef struct mgl_stage_out
{
    mgl_vtx  *v;
    uint16_t *idx;
    uint32_t *nv, *nt;                      /* the fill counters                                           */
    void    (*room)(void *ud, uint32_t nv, uint32_t nt);   /* make room for nv / nt more (may flush: resets the counters) */
    void     *ud;
    uint32_t *st_in, *st_culled, *st_clipped;
} mgl_stage_out;

/* object -> clip: o.x .. o.w = mvp x (x, y, z, w); the attributes are not touched */
void mgl_stage_xform(const float *mvp, float x, float y, float z, float w, mgl_cvtx *o);
/* one clip-space triangle (flat colour already applied) into the output: culled, clipped, projected */
void mgl_stage_tri(const mgl_geom *g, const mgl_cvtx *a, const mgl_cvtx *b, const mgl_cvtx *c, const mgl_stage_out *o);
/* a clip-space vertex to screen space */
void mgl_stage_to_screen(const mgl_geom *g, const mgl_cvtx *v, mgl_vtx *o);
/* the outcode of a clip-space vertex: a bit per plane it is outside of */
uint32_t mgl_stage_outcode(const mgl_cvtx *v);

#endif
