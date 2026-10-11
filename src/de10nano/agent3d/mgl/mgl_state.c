/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2 GL core: state, enables, clears, queries, texture objects. */

#include <string.h>
#include "mgl_core.h"

void mgl_geom_init(GLcontext c);                /* mgl_geom.c */

#ifndef MGL_RENDERER_STRING
#define MGL_RENDERER_STRING "BigMig 3Dx2"
#endif
#ifndef MGL_VERSION_STRING
#define MGL_VERSION_STRING  "1.1 BigMig 3Dx2"
#endif
#define MGL_VENDOR_STRING   "BigMig"
#define MGL_EXT_STRING      "GL_MGL_packed_pixels GL_EXT_color_table"
#define MGL_MAX_TEX_SIZE    256

void mgl_seterr(GLcontext c, GLenum e)
{
    if (c->error == GL_NO_ERROR)                /* GL keeps the first error until glGetError reads it */
        c->error = e;
    if (!c->st_glerrors++)
        c->st_glerror_first = e;
}

static int no_begin(GLcontext c)
{
    if (c->in_begin)
        mgl_seterr(c, GL_INVALID_OPERATION);
    return 1;
}

static void dirty(GLcontext c)
{
    c->rs_dirty = 1;
}

typedef char mgl_raster_is_words[(sizeof(mgl_raster) % 4u) == 0 ? 1 : -1];
static int same_words(const void *a, const void *b, uint32_t n)
{
    const uint32_t *x = (const uint32_t *)a, *y = (const uint32_t *)b;
    for (uint32_t i = 0; i < n / 4u; i++)
        if (x[i] != y[i])
            return 0;
    return 1;
}

static void tex_realise(GLcontext c, mgl_texobj *t);

void mgl_raster_sync(GLcontext c)
{
    c->st_rs_syncs++;
    mgl_raster r;
    memset(&r, 0, sizeof(r));
    const uint32_t name = c->bound[0];
    mgl_texobj *t = name < MGL_MAX_TEXNAMES ? &c->tex[name] : 0;
    if (c->en_tex2d[0] && t && t->used && t->nlevels)
    {
        if (t->dirty || !t->handle)
            tex_realise(c, t);
        r.tex = t->handle;
        r.min_filter = t->min_filter;
        r.mag_filter = t->mag_filter;
        r.wrap_s = t->wrap_s;
        r.wrap_t = t->wrap_t;
    }
    r.texenv = c->texenv[0];
    memcpy(r.envcolor, c->envcolor[0], sizeof(r.envcolor));
    r.blend = c->en_blend;
    r.blend_src = c->blend_src;
    r.blend_dst = c->blend_dst;
    r.depth_test = c->en_depth;
    r.depth_func = c->depth_func;
    r.depth_mask = c->depth_mask;
    r.alpha_test = c->en_alpha;
    r.alpha_func = c->alpha_func;
    r.alpha_ref = c->alpha_ref;
    r.fog = c->en_fog;
    r.fog_mode = c->fog_mode;
    r.fog_start = c->fog_start;
    r.fog_end = c->fog_end;
    r.fog_density = c->fog_density;
    memcpy(r.fog_color, c->fog_color, sizeof(r.fog_color));
    if (r.fog)
    {
        const float *pm = c->proj[c->proj_sp].m;
        const float ws = pm[11] * -c->fog_start + pm[15], we = pm[11] * -c->fog_end + pm[15];
        r.fog_wstart = ws > 1.0f ? 1.0f / ws : 1.0f;
        r.fog_wend = we > 1.0f ? 1.0f / we : 1.0f;
    }
    r.scissor = c->en_scissor;
    r.sc_x = c->sc_x;
    r.sc_y = c->height - (c->sc_y + c->sc_h);   /* GL's box (y up, from the bottom) in the sink's y-down rows */
    r.sc_w = c->sc_w;
    r.sc_h = c->sc_h;
    r.color_mask = c->color_mask;
    r.dither = c->en_dither;
    r.persp = c->en_persp;
    c->rs_dirty = 0;
    if (c->rs_valid && same_words(&r, &c->rs, sizeof(r)))
        return;                                 /* changed and changed back: nothing for the sink */
    c->rs = r;
    c->rs_valid = 1;
    c->st_state++;
    if (c->sink.state)
        c->sink.state(c->sink.ud, &r);
}

void mgl_core_init(GLcontext c, const mgl_sink *sink, int32_t width, int32_t height)
{
    memset(c, 0, sizeof(*c));
    c->sink = *sink;
    c->width = width;
    c->height = height;
    mgl_geom_init(c);
    c->error = GL_NO_ERROR;
    c->blend_src = GL_ONE;
    c->blend_dst = GL_ZERO;
    c->blend_eq = GL_FUNC_ADD;
    c->depth_func = GL_LESS;
    c->depth_mask = 1;
    c->alpha_func = GL_ALWAYS;
    c->alpha_ref = 0.0f;
    c->cull_mode = GL_BACK;
    c->front_face = GL_CCW;
    c->shade_model = GL_SMOOTH;
    for (uint32_t u = 0; u < MGL_UNITS; u++)
        c->texenv[u] = GL_MODULATE;
    c->fog_mode = GL_EXP;
    c->fog_start = 0.0f;
    c->fog_end = 1.0f;
    c->fog_density = 1.0f;
    c->sc_x = 0;
    c->sc_y = 0;
    c->sc_w = width;
    c->sc_h = height;
    c->color_mask = 0xFu;
    c->clear_depth = 1.0f;
    c->en_dither = 1;
    c->persp_hint = GL_DONT_CARE;
    c->en_persp = 1;
    c->fog_hint = GL_DONT_CARE;
    c->point_size = 1.0f;
    c->line_width = 1.0f;
    c->unpack_align = 4;
    c->rs_dirty = 1;
}

void mgl_core_free(GLcontext c)
{
    mgl_core_flush(c);
    if (c->dv && c->sink.free)
        c->sink.free(c->sink.ud, c->dv, (uint32_t)sizeof(mgl_cvtx) * 3u * MGL_DEFER_TRI);
    c->dv = 0;
    c->deferred = 0;
    for (uint32_t n = 0; n < MGL_MAX_TEXNAMES; n++)
    {
        mgl_texobj *t = &c->tex[n];
        if (!t->used)
            continue;
        if (t->handle && c->sink.tex_free)
            c->sink.tex_free(c->sink.ud, t->handle);
        for (uint32_t l = 0; l < MGL_MAX_LEVELS; l++)
            if (t->lv[l].px && c->sink.free)
                c->sink.free(c->sink.ud, t->lv[l].px, 0);
        memset(t, 0, sizeof(*t));
    }
}

void mgl_core_resize(GLcontext c, int32_t width, int32_t height)
{
    mgl_core_flush(c);
    c->width = width;
    c->height = height;
    c->geom_dirty = 1;
    dirty(c);
}

void mgl_core_set_zoffset(GLcontext c, float off)
{
    mgl_core_flush(c);
    c->zoffset = off;
    c->geom_dirty = 1;
}

static uint32_t *cap_ptr(GLcontext c, GLenum cap)
{
    switch (cap)
    {
    case GL_BLEND:               return &c->en_blend;
    case GL_DEPTH_TEST:          return &c->en_depth;
    case GL_ALPHA_TEST:          return &c->en_alpha;
    case GL_CULL_FACE:           return &c->en_cull;
    case GL_TEXTURE_2D:          return &c->en_tex2d[c->active_unit];
    case GL_FOG:                 return &c->en_fog;
    case GL_SCISSOR_TEST:        return &c->en_scissor;
    case GL_DITHER:              return &c->en_dither;
    case GL_POLYGON_OFFSET_FILL: return &c->en_polyoffset;
    case MGL_PERSPECTIVE_MAPPING: return &c->en_persp;
    default:                     return 0;
    }
}

void MGLSetState(GLcontext c, GLenum cap, GLboolean state)
{
    if (!no_begin(c))
        return;
    uint32_t *p = cap_ptr(c, cap);
    if (!p)
    {
        if (cap == GL_POINT_SMOOTH || cap == GL_TEXTURE_GEN_S || cap == GL_TEXTURE_GEN_T ||
            cap == MGL_Z_OFFSET || cap == GL_POLYGON_OFFSET_LINE || cap == GL_POLYGON_OFFSET_POINT ||
            cap == GL_SHARED_TEXTURE_PALETTE_EXT || cap == GL_COLOR_TABLE)
            return;
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    const uint32_t v = state ? 1u : 0u;
    if (*p != v)
    {
        *p = v;
        if (cap == GL_CULL_FACE)
            c->geom_dirty = 1;
        else if (cap != GL_POLYGON_OFFSET_FILL)   /* the core's own: no sink state */
            dirty(c);
    }
}

GLboolean GLIsEnabled(GLcontext c, GLenum cap)
{
    uint32_t *p = cap_ptr(c, cap);
    if (p)
        return *p ? GL_TRUE : GL_FALSE;
    switch (cap)
    {
    case GL_VERTEX_ARRAY:        return c->va_vertex.enabled ? GL_TRUE : GL_FALSE;
    case GL_COLOR_ARRAY:         return c->va_color.enabled ? GL_TRUE : GL_FALSE;
    case GL_TEXTURE_COORD_ARRAY: return c->va_tex[c->client_unit].enabled ? GL_TRUE : GL_FALSE;
    default:                     break;
    }
    return GL_FALSE;
}

void GLBlendFunc(GLcontext c, GLenum s, GLenum d)
{
    if (!no_begin(c))
        return;
    if (c->blend_src != s || c->blend_dst != d)
    {
        c->blend_src = s;
        c->blend_dst = d;
        dirty(c);
    }
}

void GLBlendFuncSeparate(GLcontext c, GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha)
{
    (void)srcAlpha;                             /* one blend unit: the colour factors apply to alpha too */
    (void)dstAlpha;
    GLBlendFunc(c, srcRGB, dstRGB);
}

void GLBlendEquation(GLcontext c, GLenum mode)
{
    if (mode != GL_FUNC_ADD && mode != GL_FUNC_SUBTRACT && mode != GL_FUNC_REVERSE_SUBTRACT && mode != GL_MIN &&
        mode != GL_MAX)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    c->blend_eq = mode;                         /* kept; the sink draws GL_FUNC_ADD (Warp3D has no equation) */
}

void GLDepthFunc(GLcontext c, GLenum func)
{
    if (!no_begin(c))
        return;
    if (c->depth_func != func)
    {
        c->depth_func = func;
        dirty(c);
    }
}

void GLDepthMask(GLcontext c, GLboolean flag)
{
    if (!no_begin(c))
        return;
    const uint32_t v = flag ? 1u : 0u;
    if (c->depth_mask != v)
    {
        c->depth_mask = v;
        dirty(c);
    }
}

void GLAlphaFunc(GLcontext c, GLenum func, GLclampf ref)
{
    if (!no_begin(c))
        return;
    const float r = ref < 0.0f ? 0.0f : ref > 1.0f ? 1.0f : ref;
    if (c->alpha_func != func || c->alpha_ref != r)
    {
        c->alpha_func = func;
        c->alpha_ref = r;
        dirty(c);
    }
}

void GLCullFace(GLcontext c, GLenum mode)
{
    if (!no_begin(c))
        return;
    if (mode != GL_FRONT && mode != GL_BACK && mode != GL_FRONT_AND_BACK)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    c->cull_mode = mode;
    c->geom_dirty = 1;
}

void GLFrontFace(GLcontext c, GLenum mode)
{
    if (!no_begin(c))
        return;
    if (mode != GL_CW && mode != GL_CCW)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    c->front_face = mode;
    c->geom_dirty = 1;
}

void GLShadeModel(GLcontext c, GLenum mode)
{
    if (!no_begin(c))
        return;
    if (mode != GL_FLAT && mode != GL_SMOOTH)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    c->shade_model = mode;
}

void GLColorMask(GLcontext c, GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
    if (!no_begin(c))
        return;
    const uint32_t m = (r ? 1u : 0u) | (g ? 2u : 0u) | (b ? 4u : 0u) | (a ? 8u : 0u);
    if (c->color_mask != m)
    {
        c->color_mask = m;
        dirty(c);
    }
}

void GLScissor(GLcontext c, GLint x, GLint y, GLsizei w, GLsizei h)
{
    if (!no_begin(c))
        return;
    if (w < 0 || h < 0)
    {
        mgl_seterr(c, GL_INVALID_VALUE);
        return;
    }
    c->sc_x = x;
    c->sc_y = y;
    c->sc_w = (int32_t)w;
    c->sc_h = (int32_t)h;
    dirty(c);
}

void GLHint(GLcontext c, GLenum target, GLenum mode)
{
    if (target == GL_PERSPECTIVE_CORRECTION_HINT)
        c->persp_hint = mode;
    else if (target == GL_FOG_HINT)
        c->fog_hint = mode;
}

void GLFogf(GLcontext c, GLenum pname, GLfloat param)
{
    switch (pname)
    {
    case GL_FOG_MODE:    c->fog_mode = (GLenum)param; break;
    case GL_FOG_START:   c->fog_start = param; break;
    case GL_FOG_END:     c->fog_end = param; break;
    case GL_FOG_DENSITY: c->fog_density = param; break;
    default:             mgl_seterr(c, GL_INVALID_ENUM); return;
    }
    dirty(c);
}

void GLFogfv(GLcontext c, GLenum pname, GLfloat *param)
{
    if (pname == GL_FOG_COLOR)
    {
        memcpy(c->fog_color, param, sizeof(c->fog_color));
        dirty(c);
    }
    else
        GLFogf(c, pname, param[0]);
}

void GLTexEnvi(GLcontext c, GLenum target, GLenum pname, GLint param)
{
    if (target != GL_TEXTURE_ENV || pname != GL_TEXTURE_ENV_MODE)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    if ((GLenum)param != GL_MODULATE && (GLenum)param != GL_DECAL && (GLenum)param != GL_REPLACE &&
        (GLenum)param != GL_BLEND)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    if (c->texenv[c->active_unit] != (GLenum)param)
    {
        c->texenv[c->active_unit] = (GLenum)param;
        dirty(c);
    }
}

void GLPointSize(GLcontext c, GLfloat size)
{
    if (size <= 0.0f)
    {
        mgl_seterr(c, GL_INVALID_VALUE);
        return;
    }
    c->point_size = size;
}

void GLLineWidth(GLcontext c, GLfloat width)
{
    if (width <= 0.0f)
    {
        mgl_seterr(c, GL_INVALID_VALUE);
        return;
    }
    c->line_width = width;
}

void GLPolygonMode(GLcontext c, GLenum face, GLenum mode)
{
    (void)face;
    if (mode != GL_FILL)                        /* the GPU fills: GL_LINE / GL_POINT drawn filled (documented) */
        return;
    (void)c;
}

void GLPolygonOffset(GLcontext c, GLfloat factor, GLfloat units)
{
    c->po_factor = factor;
    c->po_units = units;
}

void GLPixelStorei(GLcontext c, GLenum pname, GLint param)
{
    if (pname == GL_UNPACK_ALIGNMENT)
    {
        if (param != 1 && param != 2 && param != 4 && param != 8)
        {
            mgl_seterr(c, GL_INVALID_VALUE);
            return;
        }
        c->unpack_align = param;
    }
    else if (pname == GL_UNPACK_ROW_LENGTH)
        c->unpack_row = param;
    /* the pack side (glReadPixels) and the swap/LSB flags: accepted, no effect */
}

void GLTexGeni(GLcontext c, GLenum coord, GLenum mode, GLenum map)  { (void)c; (void)coord; (void)mode; (void)map; }
void GLTexGenfv(GLcontext c, GLenum coord, GLenum pname, const GLfloat *params)
{
    (void)c; (void)coord; (void)pname; (void)params;
}
void GLColorTable(GLcontext c, GLenum target, GLenum internalformat, GLint width, GLenum format, GLenum type, GLvoid *data)
{
    (void)target;
    (void)internalformat;
    if (width < 1 || width > 256 || type != GL_UNSIGNED_BYTE || (format != GL_RGB && format != GL_RGBA) || !data)
    {
        mgl_seterr(c, GL_INVALID_VALUE);
        return;
    }
    const uint8_t *p = (const uint8_t *)data;
    const uint32_t n = format == GL_RGBA ? 4u : 3u;
    for (GLint i = 0; i < width; i++)
    {
        c->palette[i][0] = p[n * (uint32_t)i];
        c->palette[i][1] = p[n * (uint32_t)i + 1u];
        c->palette[i][2] = p[n * (uint32_t)i + 2u];
        c->palette[i][3] = n == 4u ? p[n * (uint32_t)i + 3u] : 255;
    }
}
void GLDrawBuffer(GLcontext c, GLenum mode)  { (void)c; (void)mode; }     /* the back buffer, always */
void GLReadBuffer(GLcontext c, GLenum mode)  { (void)c; (void)mode; }

void GLClearColor(GLcontext c, GLclampf r, GLclampf g, GLclampf b, GLclampf a)
{
    c->clear_color[0] = r < 0.0f ? 0.0f : r > 1.0f ? 1.0f : r;
    c->clear_color[1] = g < 0.0f ? 0.0f : g > 1.0f ? 1.0f : g;
    c->clear_color[2] = b < 0.0f ? 0.0f : b > 1.0f ? 1.0f : b;
    c->clear_color[3] = a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a;
}

void GLClearDepth(GLcontext c, GLclampd depth)
{
    c->clear_depth = (float)(depth < 0.0 ? 0.0 : depth > 1.0 ? 1.0 : depth);
}

void GLClear(GLcontext c, GLbitfield mask)
{
    if (!no_begin(c))
        return;
    mgl_core_flush(c);
    if (c->rs_dirty || !c->rs_valid)
        mgl_raster_sync(c);
    uint32_t m = (uint32_t)mask & (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!c->depth_mask)
        m &= ~(uint32_t)GL_DEPTH_BUFFER_BIT;    /* GL: the depth write mask applies to clears */
    if (m && c->sink.clear)
        c->sink.clear(c->sink.ud, m, c->clear_color, c->clear_depth, &c->rs);
}

void GLFlush(GLcontext c)
{
    mgl_core_flush(c);
    if (c->sink.flush)
        c->sink.flush(c->sink.ud, 0);
}

void GLFinish(GLcontext c)
{
    mgl_core_flush(c);
    if (c->sink.flush)
        c->sink.flush(c->sink.ud, 1);
}

void GLReadPixels(GLcontext c, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, GLvoid *pixels)
{
    (void)x; (void)y; (void)w; (void)h; (void)format; (void)type; (void)pixels;
    GLFinish(c);
}

GLenum GLGetError(GLcontext c)
{
    const GLenum e = c->error;
    c->error = GL_NO_ERROR;
    return e;
}

const GLubyte *GLGetString(GLcontext c, GLenum name)
{
    (void)c;
    switch (name)
    {
    case GL_VENDOR:     return (const GLubyte *)MGL_VENDOR_STRING;
    case GL_RENDERER:   return (const GLubyte *)MGL_RENDERER_STRING;
    case GL_VERSION:    return (const GLubyte *)MGL_VERSION_STRING;
    case GL_EXTENSIONS: return (const GLubyte *)MGL_EXT_STRING;
    default:            mgl_seterr(c, GL_INVALID_ENUM); return (const GLubyte *)"";
    }
}

/* the values of a state query as floats; returns the count (0 = unknown name) */
static int get_values(GLcontext c, GLenum p, float *v)
{
    switch (p)
    {
    case GL_MODELVIEW_MATRIX:  memcpy(v, c->mv[c->mv_sp].m, 64); return 16;
    case GL_PROJECTION_MATRIX: memcpy(v, c->proj[c->proj_sp].m, 64); return 16;
    case GL_TEXTURE_MATRIX:    memcpy(v, c->texm[c->tex_sp].m, 64); return 16;
    case GL_VIEWPORT:          v[0] = (float)c->vp_x; v[1] = (float)c->vp_y; v[2] = (float)c->vp_w; v[3] = (float)c->vp_h; return 4;
    case GL_SCISSOR_BOX:       v[0] = (float)c->sc_x; v[1] = (float)c->sc_y; v[2] = (float)c->sc_w; v[3] = (float)c->sc_h; return 4;
    case GL_DEPTH_RANGE:       v[0] = c->depth_n; v[1] = c->depth_f; return 2;
    case GL_CURRENT_COLOR:     memcpy(v, c->cur_color, 16); return 4;
    case GL_CURRENT_TEXTURE_COORDS: memcpy(v, c->cur_tex[c->active_unit], 16); return 4;
    case GL_COLOR_CLEAR_VALUE: memcpy(v, c->clear_color, 16); return 4;
    case GL_DEPTH_CLEAR_VALUE: v[0] = c->clear_depth; return 1;
    case GL_FOG_COLOR:         memcpy(v, c->fog_color, 16); return 4;
    case GL_FOG_START:         v[0] = c->fog_start; return 1;
    case GL_FOG_END:           v[0] = c->fog_end; return 1;
    case GL_FOG_DENSITY:       v[0] = c->fog_density; return 1;
    case GL_FOG_MODE:          v[0] = (float)c->fog_mode; return 1;
    case GL_ALPHA_TEST_REF:    v[0] = c->alpha_ref; return 1;
    case GL_ALPHA_TEST_FUNC:   v[0] = (float)c->alpha_func; return 1;
    case GL_BLEND_SRC:         v[0] = (float)c->blend_src; return 1;
    case GL_BLEND_DST:         v[0] = (float)c->blend_dst; return 1;
    case GL_DEPTH_FUNC:        v[0] = (float)c->depth_func; return 1;
    case GL_DEPTH_WRITEMASK:   v[0] = c->depth_mask ? 1.0f : 0.0f; return 1;
    case GL_CULL_FACE_MODE:    v[0] = (float)c->cull_mode; return 1;
    case GL_FRONT_FACE:        v[0] = (float)c->front_face; return 1;
    case GL_SHADE_MODEL:       v[0] = (float)c->shade_model; return 1;
    case GL_MATRIX_MODE:       v[0] = (float)c->matrix_mode; return 1;
    case GL_MODELVIEW_STACK_DEPTH:  v[0] = (float)(c->mv_sp + 1u); return 1;
    case GL_PROJECTION_STACK_DEPTH: v[0] = (float)(c->proj_sp + 1u); return 1;
    case GL_TEXTURE_STACK_DEPTH:    v[0] = (float)(c->tex_sp + 1u); return 1;
    case GL_MAX_TEXTURE_SIZE:  v[0] = (float)MGL_MAX_TEX_SIZE; return 1;
    case GL_MAX_VIEWPORT_DIMS: v[0] = 2048.0f; v[1] = 2048.0f; return 2;
    case GL_MAX_TEXTURE_UNITS_ARB: v[0] = 1.0f; return 1;
    case GL_TEXTURE_2D_BINDING: v[0] = (float)c->bound[c->active_unit]; return 1;
    case GL_RED_BITS: case GL_BLUE_BITS: v[0] = 5.0f; return 1;
    case GL_GREEN_BITS:        v[0] = 6.0f; return 1;
    case GL_ALPHA_BITS:        v[0] = 0.0f; return 1;
    case GL_DEPTH_BITS:        v[0] = 16.0f; return 1;
    case GL_DOUBLEBUFFER:      v[0] = 1.0f; return 1;
    case GL_RGBA_MODE:         v[0] = 1.0f; return 1;
    case GL_POINT_SMOOTH:      v[0] = 0.0f; return 1;
    case GL_LINE_WIDTH:        v[0] = c->line_width; return 1;
    case GL_UNPACK_ALIGNMENT:  v[0] = (float)c->unpack_align; return 1;
    case GL_UNPACK_ROW_LENGTH: v[0] = (float)c->unpack_row; return 1;
    case GL_PACK_ALIGNMENT:    v[0] = 4.0f; return 1;
    case GL_COLOR_WRITEMASK:
        v[0] = (c->color_mask & 1u) ? 1.0f : 0.0f; v[1] = (c->color_mask & 2u) ? 1.0f : 0.0f;
        v[2] = (c->color_mask & 4u) ? 1.0f : 0.0f; v[3] = (c->color_mask & 8u) ? 1.0f : 0.0f; return 4;
    case GL_BLEND_EQUATION:    v[0] = (float)c->blend_eq; return 1;
    default:
    {
        uint32_t *e = cap_ptr(c, p);
        if (e)
        {
            v[0] = *e ? 1.0f : 0.0f;
            return 1;
        }
        return 0;
    }
    }
}

void GLGetFloatv(GLcontext c, GLenum pname, GLfloat *params)
{
    float v[16];
    const int n = get_values(c, pname, v);
    if (!n)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    memcpy(params, v, sizeof(float) * (uint32_t)n);
}

void GLGetIntegerv(GLcontext c, GLenum pname, GLint *params)
{
    float v[16];
    const int n = get_values(c, pname, v);
    if (!n)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    const int colour = pname == GL_CURRENT_COLOR || pname == GL_COLOR_CLEAR_VALUE || pname == GL_FOG_COLOR;
    for (int i = 0; i < n; i++)
        params[i] = colour ? (GLint)((double)v[i] * 2147483647.0) : (GLint)(v[i] >= 0.0f ? v[i] + 0.5f : v[i] - 0.5f);
}

void GLGetBooleanv(GLcontext c, GLenum pname, GLboolean *params)
{
    float v[16];
    const int n = get_values(c, pname, v);
    if (!n)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    for (int i = 0; i < n; i++)
        params[i] = v[i] != 0.0f ? GL_TRUE : GL_FALSE;
}

void GLGetDoublev(GLcontext c, GLenum pname, GLdouble *params)
{
    float v[16];
    const int n = get_values(c, pname, v);
    if (!n)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    for (int i = 0; i < n; i++)
        params[i] = (double)v[i];
}

void GLGetPointerv(GLcontext c, GLenum pname, GLvoid **params)
{
    switch (pname)
    {
    case GL_VERTEX_ARRAY_POINTER:        *params = (GLvoid *)c->va_vertex.ptr; break;
    case GL_COLOR_ARRAY_POINTER:         *params = (GLvoid *)c->va_color.ptr; break;
    case GL_TEXTURE_COORD_ARRAY_POINTER: *params = (GLvoid *)c->va_tex[c->client_unit].ptr; break;
    default:                             *params = 0; mgl_seterr(c, GL_INVALID_ENUM); break;
    }
}

static uint32_t fmt_bytes(uint32_t f)
{
    switch (f)
    {
    case MGL_TF_RGBA8: return 4;
    case MGL_TF_RGB8:  return 3;
    case MGL_TF_LA8:   return 2;
    default:           return 1;
    }
}

static mgl_texobj *texobj(GLcontext c, GLuint name)
{
    return name < MGL_MAX_TEXNAMES ? &c->tex[name] : 0;
}

static void tex_drop_levels(GLcontext c, mgl_texobj *t)
{
    for (uint32_t l = 0; l < MGL_MAX_LEVELS; l++)
    {
        if (t->lv[l].px && c->sink.free)
            c->sink.free(c->sink.ud, t->lv[l].px, t->lv[l].w * t->lv[l].h * fmt_bytes(t->fmt));
        t->lv[l].px = 0;
        t->lv[l].w = t->lv[l].h = 0;
    }
    t->nlevels = 0;
}

/* the sink creates (or re-creates) the texture from the levels the core has */
static void tex_realise(GLcontext c, mgl_texobj *t)
{
    if (t->handle && c->sink.tex_free)
        c->sink.tex_free(c->sink.ud, t->handle);
    t->handle = 0;
    if (c->sink.tex_create && t->nlevels)
        t->handle = c->sink.tex_create(c->sink.ud, t->fmt, t->nlevels, t->lv);
    t->dirty = 0;
}

void GLGenTextures(GLcontext c, GLsizei n, GLuint *textures)
{
    GLsizei k = 0;
    for (uint32_t name = 1; name < MGL_MAX_TEXNAMES && k < n; name++)
        if (!c->tex[name].used)
        {
            c->tex[name].used = 1;              /* reserved: GL makes a name "used" by glGenTextures */
            c->tex[name].min_filter = GL_NEAREST_MIPMAP_LINEAR;
            c->tex[name].mag_filter = GL_LINEAR;
            c->tex[name].wrap_s = c->tex[name].wrap_t = GL_REPEAT;
            textures[k++] = name;
        }
    while (k < n)
        textures[k++] = 0;
    if (n > 0 && textures[n - 1] == 0)
        mgl_seterr(c, GL_OUT_OF_MEMORY);
}

void GLDeleteTextures(GLcontext c, GLsizei n, const GLuint *textures)
{
    mgl_core_flush(c);
    for (GLsizei k = 0; k < n; k++)
    {
        mgl_texobj *t = texobj(c, textures[k]);
        if (!t || textures[k] == 0 || !t->used)
            continue;
        if (t->handle && c->sink.tex_free)
            c->sink.tex_free(c->sink.ud, t->handle);
        tex_drop_levels(c, t);
        memset(t, 0, sizeof(*t));
        for (uint32_t u = 0; u < MGL_UNITS; u++)
            if (c->bound[u] == textures[k])
                c->bound[u] = 0;                /* GL: a deleted bound texture reverts to the default */
        dirty(c);
    }
}

GLboolean GLIsTexture(GLcontext c, GLuint texture)
{
    mgl_texobj *t = texobj(c, texture);
    return t && texture && t->used && t->nlevels ? GL_TRUE : GL_FALSE;
}

GLboolean GLAreTexturesResident(GLcontext c, GLsizei n, const GLuint *textures, GLboolean *residences)
{
    GLboolean all = GL_TRUE;
    for (GLsizei k = 0; k < n; k++)
    {
        residences[k] = GLIsTexture(c, textures[k]);
        if (!residences[k])
            all = GL_FALSE;
    }
    return all;
}

void GLPrioritizeTextures(GLcontext c, GLsizei n, const GLuint *textures, const GLclampf *priorities)
{
    for (GLsizei k = 0; k < n; k++)
    {
        mgl_texobj *t = texobj(c, textures[k]);
        if (t)
            t->priority = priorities[k];
    }
}

void GLBindTexture(GLcontext c, GLenum target, GLuint texture)
{
    if (target != GL_TEXTURE_2D)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    mgl_texobj *t = texobj(c, texture);
    if (!t)
    {
        mgl_seterr(c, GL_OUT_OF_MEMORY);        /* a name beyond the table: refused, never written past it */
        return;
    }
    if (texture && !t->used)
    {
        t->used = 1;                            /* binding an unused name creates the object (GL 1.1) */
        t->min_filter = GL_NEAREST_MIPMAP_LINEAR;
        t->mag_filter = GL_LINEAR;
        t->wrap_s = t->wrap_t = GL_REPEAT;
    }
    if (c->bound[c->active_unit] != texture)
    {
        c->bound[c->active_unit] = texture;
        dirty(c);
    }
}

void GLTexParameteri(GLcontext c, GLenum target, GLenum pname, GLint param)
{
    if (target != GL_TEXTURE_2D)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    mgl_texobj *t = texobj(c, c->bound[c->active_unit]);
    if (!t)
        return;
    switch (pname)
    {
    case GL_TEXTURE_MIN_FILTER: t->min_filter = (GLenum)param; break;
    case GL_TEXTURE_MAG_FILTER: t->mag_filter = (GLenum)param; break;
    case GL_TEXTURE_WRAP_S:     t->wrap_s = (GLenum)param; break;
    case GL_TEXTURE_WRAP_T:     t->wrap_t = (GLenum)param; break;
    case GL_TEXTURE_PRIORITY:   t->priority = (float)param; return;
    default:                    mgl_seterr(c, GL_INVALID_ENUM); return;
    }
    dirty(c);
}

static uint32_t core_fmt(GLint internalformat)
{
    switch (internalformat)
    {
    case GL_ALPHA: case GL_ALPHA8:                                 return MGL_TF_A8;
    case GL_LUMINANCE: case GL_LUMINANCE8:                         return MGL_TF_L8;
    case GL_LUMINANCE_ALPHA: case GL_LUMINANCE8_ALPHA8:            return MGL_TF_LA8;
    case 3: case GL_RGB: case GL_RGB5: case GL_RGB8:               return MGL_TF_RGB8;
    case MGL_UNSIGNED_SHORT_5_6_5:                                 return MGL_TF_RGB8;
    case 4: case GL_RGBA: case GL_RGBA8: case GL_RGB5_A1:          return MGL_TF_RGBA8;
    case MGL_UNSIGNED_SHORT_4_4_4_4:                               return MGL_TF_RGBA8;
    case GL_INTENSITY: case GL_INTENSITY8:                         return MGL_TF_I8;
    default:                                                       return 0;
    }
}

static int src_texel(GLcontext c, const uint8_t *p, GLenum format, GLenum type, uint8_t o[4])
{
    if (format == MGL_UNSIGNED_SHORT_5_6_5 || type == MGL_UNSIGNED_SHORT_5_6_5)
    {
        const uint32_t v = ((uint32_t)p[0] << 8) | p[1];
        o[0] = (uint8_t)(((v >> 11) & 31u) * 255u / 31u);
        o[1] = (uint8_t)(((v >> 5) & 63u) * 255u / 63u);
        o[2] = (uint8_t)((v & 31u) * 255u / 31u);
        o[3] = 255;
        return 2;
    }
    if (format == MGL_UNSIGNED_SHORT_4_4_4_4 || type == MGL_UNSIGNED_SHORT_4_4_4_4)
    {
        const uint32_t v = ((uint32_t)p[0] << 8) | p[1];
        o[3] = (uint8_t)(((v >> 12) & 15u) * 17u);
        o[0] = (uint8_t)(((v >> 8) & 15u) * 17u);
        o[1] = (uint8_t)(((v >> 4) & 15u) * 17u);
        o[2] = (uint8_t)((v & 15u) * 17u);
        return 2;
    }
    if (type != GL_UNSIGNED_BYTE)
        return 0;
    switch (format)
    {
    case GL_RGBA:            o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; o[3] = p[3]; return 4;
    case GL_RGB:             o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; o[3] = 255; return 3;
    case GL_LUMINANCE:       o[0] = o[1] = o[2] = p[0]; o[3] = 255; return 1;
    case GL_LUMINANCE_ALPHA: o[0] = o[1] = o[2] = p[0]; o[3] = p[1]; return 2;
    case GL_ALPHA:           o[0] = o[1] = o[2] = 0; o[3] = p[0]; return 1;
    case GL_INTENSITY:       o[0] = o[1] = o[2] = o[3] = p[0]; return 1;
    case MGL_UBYTE_ARGB:     o[0] = p[1]; o[1] = p[2]; o[2] = p[3]; o[3] = p[0]; return 4;
    case MGL_UBYTE_BGRA:     o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; o[3] = p[3]; return 4;
    case GL_COLOR_INDEX:     memcpy(o, c->palette[p[0]], 4); return 1;
    default:                 return 0;
    }
}

static void put_texel(uint8_t *d, uint32_t fmt, const uint8_t s[4])
{
    switch (fmt)
    {
    case MGL_TF_RGBA8: d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; break;
    case MGL_TF_RGB8:  d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; break;
    case MGL_TF_L8:    d[0] = s[0]; break;
    case MGL_TF_A8:    d[0] = s[3]; break;
    case MGL_TF_LA8:   d[0] = s[0]; d[1] = s[3]; break;
    case MGL_TF_I8:    d[0] = s[0]; break;
    default: break;
    }
}

static int copy_rect(GLcontext c, mgl_level *lv, uint32_t fmt, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h,
                     GLenum format, GLenum type, const uint8_t *src)
{
    uint8_t probe[4];
    const uint8_t zero[4] = { 0, 0, 0, 0 };
    const int bpt = src_texel(c, zero, format, type, probe);
    if (!bpt)
        return 0;
    const uint32_t rowpx = c->unpack_row > 0 ? (uint32_t)c->unpack_row : w;
    uint32_t stride = rowpx * (uint32_t)bpt;
    const uint32_t al = (uint32_t)c->unpack_align;
    stride = (stride + al - 1u) / al * al;
    const uint32_t ob = fmt_bytes(fmt);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++)
        {
            uint8_t t[4];
            src_texel(c, src + y * stride + x * (uint32_t)bpt, format, type, t);
            put_texel(lv->px + ((y0 + y) * lv->w + x0 + x) * ob, fmt, t);
        }
    return 1;
}

void GLTexImage2D(GLcontext c, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                  GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    if (!no_begin(c))
        return;
    if (target != GL_TEXTURE_2D)
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    const uint32_t fmt = core_fmt(internalformat);
    if (!fmt || level < 0 || (uint32_t)level >= MGL_MAX_LEVELS || width < 1 || height < 1 || width > 2048 ||
        height > 2048 || border != 0)
    {
        mgl_seterr(c, fmt ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }
    mgl_texobj *t = texobj(c, c->bound[c->active_unit]);
    if (!t)
        return;
    mgl_core_flush(c);
    if (level == 0 && t->nlevels && t->fmt != fmt)
        tex_drop_levels(c, t);                  /* a new format: the chain starts over */
    if (!t->nlevels || t->fmt != fmt)
        t->fmt = fmt;
    mgl_level *lv = &t->lv[level];
    const uint32_t bytes = (uint32_t)width * (uint32_t)height * fmt_bytes(fmt);
    if (!lv->px || lv->w != (uint32_t)width || lv->h != (uint32_t)height)
    {
        if (lv->px && c->sink.free)
            c->sink.free(c->sink.ud, lv->px, lv->w * lv->h * fmt_bytes(fmt));
        lv->px = c->sink.alloc ? (uint8_t *)c->sink.alloc(c->sink.ud, bytes) : 0;
        if (!lv->px)
        {
            lv->w = lv->h = 0;
            mgl_seterr(c, GL_OUT_OF_MEMORY);
            return;
        }
        lv->w = (uint32_t)width;
        lv->h = (uint32_t)height;
        t->dirty = 1;                           /* a new size: the sink's object is re-created */
    }
    if (pixels)
    {
        if (!copy_rect(c, lv, fmt, 0, 0, (uint32_t)width, (uint32_t)height, format, type, (const uint8_t *)pixels))
        {
            mgl_seterr(c, GL_INVALID_ENUM);
            return;
        }
    }
    else
        memset(lv->px, 0, bytes);
    if ((uint32_t)level + 1u > t->nlevels)
    {
        t->nlevels = (uint32_t)level + 1u;
        t->dirty = 1;
    }
    if (!t->dirty && t->handle && c->sink.tex_update)
        c->sink.tex_update(c->sink.ud, t->handle, fmt, (uint32_t)level, lv, 0, 0, lv->w, lv->h);
    else
        t->dirty = 1;
    dirty(c);
}

void GLTexSubImage2D(GLcontext c, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                     GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
    if (!no_begin(c))
        return;
    mgl_texobj *t = texobj(c, c->bound[c->active_unit]);
    if (target != GL_TEXTURE_2D || !t || level < 0 || (uint32_t)level >= t->nlevels)
    {
        mgl_seterr(c, GL_INVALID_VALUE);
        return;
    }
    mgl_level *lv = &t->lv[level];
    if (xoffset < 0 || yoffset < 0 || width < 0 || height < 0 || (uint32_t)(xoffset + width) > lv->w ||
        (uint32_t)(yoffset + height) > lv->h || !lv->px)
    {
        mgl_seterr(c, GL_INVALID_VALUE);
        return;
    }
    if (!width || !height || !pixels)
        return;
    mgl_core_flush(c);
    if (!copy_rect(c, lv, t->fmt, (uint32_t)xoffset, (uint32_t)yoffset, (uint32_t)width, (uint32_t)height, format, type,
                   (const uint8_t *)pixels))
    {
        mgl_seterr(c, GL_INVALID_ENUM);
        return;
    }
    if (!t->dirty && t->handle && c->sink.tex_update)
        c->sink.tex_update(c->sink.ud, t->handle, t->fmt, (uint32_t)level, lv, (uint32_t)xoffset, (uint32_t)yoffset,
                           (uint32_t)width, (uint32_t)height);
    else
        t->dirty = 1;
}

void GLCopyTexImage2D(GLcontext c, GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width,
                      GLsizei height, GLint border)
{
    (void)x; (void)y;
    GLTexImage2D(c, target, level, (GLint)internalformat, width, height, border, GL_RGBA, GL_UNSIGNED_BYTE, 0);
}

void GLCopyTexSubImage2D(GLcontext c, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y,
                         GLsizei width, GLsizei height)
{
    (void)c; (void)target; (void)level; (void)xoffset; (void)yoffset; (void)x; (void)y; (void)width; (void)height;
}

MGL_A5_TEXT int mgl_get_values(GLcontext c, GLenum p, float *v)
{
    return get_values(c, p, v);
}
