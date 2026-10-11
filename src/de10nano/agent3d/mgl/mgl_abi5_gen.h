/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2 GL core: ABI-5 entries (generated from the minigl.library interface tables). */

enum {
    OP5_NONE = 0,
    OP5_GLActiveTextureARB,
    OP5_GLAlphaFunc,
    OP5_GLBegin,
    OP5_GLBindTexture,
    OP5_GLBlendFunc,
    OP5_GLClear,
    OP5_GLClearColor,
    OP5_GLClearDepth,
    OP5_GLColor3fv,
    OP5_GLColor3ubv,
    OP5_GLColor4f,
    OP5_GLColor4fv,
    OP5_GLColor4ub,
    OP5_GLColor4ubv,
    OP5_GLColorMask,
    OP5_GLCullFace,
    OP5_GLDepthFunc,
    OP5_GLDepthMask,
    OP5_GLDepthRange,
    OP5_GLDrawBuffer,
    OP5_GLEnd,
    OP5_GLFogf,
    OP5_GLFogfv,
    OP5_GLFrontFace,
    OP5_GLFrustum,
    OP5_GLHint,
    OP5_GLLoadIdentity,
    OP5_GLLoadMatrixd,
    OP5_GLLoadMatrixf,
    OP5_GLMatrixMode,
    OP5_GLMultMatrixd,
    OP5_GLMultMatrixf,
    OP5_GLMultiTexCoord2fARB,
    OP5_GLMultiTexCoord2fvARB,
    OP5_GLNormal3f,
    OP5_GLOrtho,
    OP5_GLPointSize,
    OP5_GLPolygonMode,
    OP5_GLPopMatrix,
    OP5_GLPushMatrix,
    OP5_GLRotated,
    OP5_GLRotatef,
    OP5_GLRotatefEXT,
    OP5_GLRotatefEXTs,
    OP5_GLScaled,
    OP5_GLScalef,
    OP5_GLScissor,
    OP5_GLShadeModel,
    OP5_GLTexCoord2f,
    OP5_GLTexCoord2fv,
    OP5_GLTexCoord4f,
    OP5_GLTexCoord4fv,
    OP5_GLTexEnvi,
    OP5_GLTexGeni,
    OP5_GLTexParameteri,
    OP5_GLTranslated,
    OP5_GLTranslatef,
    OP5_GLVertex2fv,
    OP5_GLVertex3fv,
    OP5_GLVertex4f,
    OP5_GLVertex4fv,
    OP5_GLViewport,
    OP5_GLPolygonOffset,
    OP5_GLBlendEquation,
    OP5_GLBlendFuncSeparate,
    OP5_GLLineWidth,
    OP5_GLTexGenfv,
    OP5_GLCopyTexImage2D,
    OP5_GLCopyTexSubImage2D,
    OP5_GLEdgeFlag,
    OP5_GLLightf,
    OP5_GLLightfv,
    OP5_GLMaterialf,
    OP5_GLMaterialfv,
    OP5_GLLightModelf,
    OP5_GLLightModelfv,
    OP5_GLColorMaterial,
    OP5_GLTexEnvfv,
    OP5_GLNormal3fv,
    OP5_GEN_LAST
};
void a5_GLActiveTextureARB(GLcontext context, GLenum e1)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLActiveTextureARB, 1u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLActiveTextureARB(context, e1);
}

void a5_GLAlphaFunc(GLcontext context, GLenum e1, GLclampf f2)
{
    e1 = (GLenum)tr((uint32_t)e1);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLAlphaFunc, 2u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            rec_[1] = fbits((float)f2);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLAlphaFunc(context, e1, f2);
}

void a5_GLBegin(GLcontext context, GLenum e1)
{
    e1 = (GLenum)tr_prim((uint32_t)e1);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        M2B_TPEND(context, x5_);
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLBegin, 1u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
        }
        if (!(x5_->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    GLBegin(context, e1);
}

void a5_GLBindTexture(GLcontext context, GLenum e1, GLuint u2)
{
    e1 = (GLenum)tr((uint32_t)e1);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLBindTexture, 2u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            rec_[1] = (uint32_t)u2;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLBindTexture_a5(context, e1, u2);
    if (x5_->m2b)
        m2b_tex_q(context, x5_, u2);
}

void a5_GLBlendFunc(GLcontext context, GLenum e1, GLenum e2)
{
    e1 = (GLenum)tr_blend((uint32_t)e1);
    e2 = (GLenum)tr_blend((uint32_t)e2);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLBlendFunc, 2u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            rec_[1] = (uint32_t)e2;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLBlendFunc(context, e1, e2);
}

void a5_GLClear(GLcontext context, GLbitfield m1)
{
    m1 = (GLbitfield)tr_mask((uint32_t)m1);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLClear, 1u);
        if (rec_)
        {
            rec_[0] = (uint32_t)m1;
        }
        if (!(x5_->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    GLClear_a5(context, m1);
}

void a5_GLClearColor(GLcontext context, GLclampf f1, GLclampf f2, GLclampf f3, GLclampf f4)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLClearColor, 4u);
        if (rec_)
        {
            rec_[0] = fbits((float)f1);
            rec_[1] = fbits((float)f2);
            rec_[2] = fbits((float)f3);
            rec_[3] = fbits((float)f4);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLClearColor(context, f1, f2, f3, f4);
}

void a5_GLClearDepth(GLcontext context, GLclampd d1)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLClearDepth, 2u);
        if (rec_)
        {
            put_d(&rec_[0], (double)d1);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLClearDepth(context, d1);
}

void a5_GLColor3fv(GLcontext context, GLfloat * p1)
{
    if (!p1) return;                          /* 29.1: NULL is ignored */
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLColor3fv, 3u);
        if (rec_)
        {
            memcpy(&rec_[0], p1, 12u);
        }
        if (!(x5_->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    GLColor3fv(context, p1);
}

void a5_GLColor3ubv(GLcontext context, GLubyte * p1)
{
    if (!p1) return;                          /* 29.1: NULL is ignored */
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLColor3ubv, 1u);
        if (rec_)
        {
            memcpy(&rec_[0], p1, 3u);
        }
        if (!(x5_->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    GLColor3ubv(context, p1);
}

void a5_GLColor4f(GLcontext context, GLfloat f1, GLfloat f2, GLfloat f3, GLfloat f4)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLColor4f, 4u);
        if (rec_)
        {
            rec_[0] = fbits((float)f1);
            rec_[1] = fbits((float)f2);
            rec_[2] = fbits((float)f3);
            rec_[3] = fbits((float)f4);
        }
        if (!(x5_->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    GLColor4f(context, f1, f2, f3, f4);
}

void a5_GLColor4fv(GLcontext context, GLfloat * p1)
{
    if (!p1) return;                          /* 29.1: NULL is ignored */
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLColor4fv, 4u);
        if (rec_)
        {
            memcpy(&rec_[0], p1, 16u);
        }
        if (!(x5_->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    GLColor4fv(context, p1);
}

void a5_GLColor4ub(GLcontext context, GLubyte ub1, GLubyte ub2, GLubyte ub3, GLubyte ub4)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLColor4ub, 4u);
        if (rec_)
        {
            rec_[0] = (uint32_t)ub1;
            rec_[1] = (uint32_t)ub2;
            rec_[2] = (uint32_t)ub3;
            rec_[3] = (uint32_t)ub4;
        }
        if (!(x5_->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    GLColor4ub(context, ub1, ub2, ub3, ub4);
}

void a5_GLColor4ubv(GLcontext context, GLubyte * p1)
{
    if (!p1) return;                          /* 29.1: NULL is ignored */
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLColor4ubv, 1u);
        if (rec_)
        {
            memcpy(&rec_[0], p1, 4u);
        }
        if (!(x5_->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    GLColor4ubv(context, p1);
}

void a5_GLColorMask(GLcontext context, GLboolean b1, GLboolean b2, GLboolean b3, GLboolean b4)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLColorMask, 4u);
        if (rec_)
        {
            rec_[0] = (uint32_t)b1;
            rec_[1] = (uint32_t)b2;
            rec_[2] = (uint32_t)b3;
            rec_[3] = (uint32_t)b4;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLColorMask(context, b1, b2, b3, b4);
}

void a5_GLColorPointer(GLcontext context, GLint i1, GLenum e2, GLsizei n3, const GLvoid * p4)
{
    e2 = (GLenum)tr((uint32_t)e2);
    GLColorPointer(context, i1, e2, n3, p4);
}

void a5_GLCullFace(GLcontext context, GLenum e1)
{
    e1 = (GLenum)tr((uint32_t)e1);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLCullFace, 1u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLCullFace(context, e1);
}

void a5_GLDepthFunc(GLcontext context, GLenum e1)
{
    e1 = (GLenum)tr((uint32_t)e1);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLDepthFunc, 1u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLDepthFunc(context, e1);
}

void a5_GLDepthMask(GLcontext context, GLboolean b1)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLDepthMask, 1u);
        if (rec_)
        {
            rec_[0] = (uint32_t)b1;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLDepthMask(context, b1);
}

void a5_GLDepthRange(GLcontext context, GLclampd d1, GLclampd d2)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLDepthRange, 4u);
        if (rec_)
        {
            put_d(&rec_[0], (double)d1);
            put_d(&rec_[2], (double)d2);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLDepthRange(context, d1, d2);
}

void a5_GLDrawBuffer(GLcontext context, GLenum e1)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLDrawBuffer, 1u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLDrawBuffer(context, e1);
}

void a5_GLEnd(GLcontext context)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLEnd, 0u);
        if (rec_)
        {
        }
        if (!(x5_->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    GLEnd(context);
}

void a5_GLFogf(GLcontext context, GLenum e1, GLfloat f2)
{
    e1 = (GLenum)tr((uint32_t)e1);
    if ((uint32_t)e1 == GL_FOG_MODE) f2 = (GLfloat)tr((uint32_t)f2);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLFogf, 2u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            rec_[1] = fbits((float)f2);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLFogf(context, e1, f2);
}

void a5_GLFogfv(GLcontext context, GLenum e1, GLfloat * p2)
{
    e1 = (GLenum)tr((uint32_t)e1);
    if (!p2) { mgl_seterr(context, GL_INVALID_VALUE); return; }
    GLfloat p2_l[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    const uint32_t p2_n = cnt_fog((uint32_t)e1);
    for (uint32_t k_ = 0; k_ < p2_n; k_++) p2_l[k_] = p2[k_];
    if (pfmode_is_token((uint32_t)e1)) p2_l[0] = (GLfloat)tr((uint32_t)p2_l[0]);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLFogfv, 5u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            memcpy(&rec_[1], p2_l, 16u);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLFogfv(context, e1, p2_l);
}

void a5_GLFrontFace(GLcontext context, GLenum e1)
{
    e1 = (GLenum)tr((uint32_t)e1);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLFrontFace, 1u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLFrontFace(context, e1);
}

void a5_GLFrustum(GLcontext context, GLdouble d1, GLdouble d2, GLdouble d3, GLdouble d4, GLdouble d5, GLdouble d6)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLFrustum, 12u);
        if (rec_)
        {
            put_d(&rec_[0], (double)d1);
            put_d(&rec_[2], (double)d2);
            put_d(&rec_[4], (double)d3);
            put_d(&rec_[6], (double)d4);
            put_d(&rec_[8], (double)d5);
            put_d(&rec_[10], (double)d6);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLFrustum(context, d1, d2, d3, d4, d5, d6);
}

void a5_GLHint(GLcontext context, GLenum e1, GLenum e2)
{
    e1 = (GLenum)tr((uint32_t)e1);
    e2 = (GLenum)tr((uint32_t)e2);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLHint, 2u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            rec_[1] = (uint32_t)e2;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLHint(context, e1, e2);
}

void a5_GLLoadIdentity(GLcontext context)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLLoadIdentity, 0u);
        if (rec_)
        {
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLLoadIdentity_a5(context);
}

void a5_GLLoadMatrixd(GLcontext context, const GLdouble * p1)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLLoadMatrixd, 32u);
        if (rec_)
        {
            memcpy(&rec_[0], p1, 128u);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLLoadMatrixd_a5(context, p1);
}

void a5_GLLoadMatrixf(GLcontext context, const GLfloat * p1)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLLoadMatrixf, 16u);
        if (rec_)
        {
            memcpy(&rec_[0], p1, 64u);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLLoadMatrixf_a5(context, p1);
}

void a5_GLMatrixMode(GLcontext context, GLenum e1)
{
    e1 = (GLenum)tr((uint32_t)e1);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLMatrixMode, 1u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLMatrixMode(context, e1);
}

void a5_GLMultMatrixd(GLcontext context, const GLdouble * p1)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLMultMatrixd, 32u);
        if (rec_)
        {
            memcpy(&rec_[0], p1, 128u);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLMultMatrixd(context, p1);
}

void a5_GLMultMatrixf(GLcontext context, const GLfloat * p1)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLMultMatrixf, 16u);
        if (rec_)
        {
            memcpy(&rec_[0], p1, 64u);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLMultMatrixf(context, p1);
}

void a5_GLMultiTexCoord2fARB(GLcontext context, GLenum e1, GLfloat f2, GLfloat f3)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLMultiTexCoord2fARB, 3u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            rec_[1] = fbits((float)f2);
            rec_[2] = fbits((float)f3);
        }
        if (!(x5_->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    GLMultiTexCoord2fARB(context, e1, f2, f3);
}

void a5_GLMultiTexCoord2fvARB(GLcontext context, GLenum e1, GLfloat * p2)
{
    if (!p2) return;                          /* 29.1: NULL is ignored */
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLMultiTexCoord2fvARB, 3u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            memcpy(&rec_[1], p2, 8u);
        }
        if (!(x5_->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    GLMultiTexCoord2fvARB(context, e1, p2);
}

void a5_GLNormal3f(GLcontext context, GLfloat f1, GLfloat f2, GLfloat f3)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLNormal3f, 3u);
        if (rec_)
        {
            rec_[0] = fbits((float)f1);
            rec_[1] = fbits((float)f2);
            rec_[2] = fbits((float)f3);
        }
        if (!(x5_->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    GLNormal3f(context, f1, f2, f3);
}

void a5_GLOrtho(GLcontext context, GLdouble d1, GLdouble d2, GLdouble d3, GLdouble d4, GLdouble d5, GLdouble d6)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLOrtho, 12u);
        if (rec_)
        {
            put_d(&rec_[0], (double)d1);
            put_d(&rec_[2], (double)d2);
            put_d(&rec_[4], (double)d3);
            put_d(&rec_[6], (double)d4);
            put_d(&rec_[8], (double)d5);
            put_d(&rec_[10], (double)d6);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLOrtho(context, d1, d2, d3, d4, d5, d6);
}

void a5_GLPixelStorei(GLcontext context, GLenum e1, GLint i2)
{
    e1 = (GLenum)tr((uint32_t)e1);
    GLPixelStorei_a5(context, e1, i2);
}

void a5_GLPointSize(GLcontext context, GLfloat f1)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLPointSize, 1u);
        if (rec_)
        {
            rec_[0] = fbits((float)f1);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLPointSize(context, f1);
}

void a5_GLPolygonMode(GLcontext context, GLenum e1, GLenum e2)
{
    e1 = (GLenum)tr((uint32_t)e1);
    e2 = (GLenum)tr((uint32_t)e2);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLPolygonMode, 2u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            rec_[1] = (uint32_t)e2;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLPolygonMode(context, e1, e2);
}

void a5_GLPopMatrix(GLcontext context)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLPopMatrix, 0u);
        if (rec_)
        {
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLPopMatrix_a5(context);
}

void a5_GLPushMatrix(GLcontext context)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLPushMatrix, 0u);
        if (rec_)
        {
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLPushMatrix_a5(context);
}

void a5_GLReadPixels(GLcontext context, GLint i1, GLint i2, GLsizei n3, GLsizei n4, GLenum e5, GLenum e6, GLvoid * p7)
{
    e5 = (GLenum)tr((uint32_t)e5);
    e6 = (GLenum)tr((uint32_t)e6);
    GLReadPixels_a5(context, i1, i2, n3, n4, e5, e6, p7);
}

void a5_GLRotated(GLcontext context, GLdouble d1, GLdouble d2, GLdouble d3, GLdouble d4)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLRotated, 8u);
        if (rec_)
        {
            put_d(&rec_[0], (double)d1);
            put_d(&rec_[2], (double)d2);
            put_d(&rec_[4], (double)d3);
            put_d(&rec_[6], (double)d4);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLRotated(context, d1, d2, d3, d4);
}

void a5_GLRotatef(GLcontext context, GLfloat f1, GLfloat f2, GLfloat f3, GLfloat f4)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLRotatef, 4u);
        if (rec_)
        {
            rec_[0] = fbits((float)f1);
            rec_[1] = fbits((float)f2);
            rec_[2] = fbits((float)f3);
            rec_[3] = fbits((float)f4);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLRotatef(context, f1, f2, f3, f4);
}

void a5_GLRotatefEXT(GLcontext context, GLfloat f1, const GLint i2)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLRotatefEXT, 2u);
        if (rec_)
        {
            rec_[0] = fbits((float)f1);
            rec_[1] = (uint32_t)i2;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLRotatefEXT(context, f1, i2);
}

void a5_GLRotatefEXTs(GLcontext context, GLfloat f1, GLfloat f2, const GLint i3)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLRotatefEXTs, 3u);
        if (rec_)
        {
            rec_[0] = fbits((float)f1);
            rec_[1] = fbits((float)f2);
            rec_[2] = (uint32_t)i3;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLRotatefEXTs(context, f1, f2, i3);
}

void a5_GLScaled(GLcontext context, GLdouble d1, GLdouble d2, GLdouble d3)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLScaled, 6u);
        if (rec_)
        {
            put_d(&rec_[0], (double)d1);
            put_d(&rec_[2], (double)d2);
            put_d(&rec_[4], (double)d3);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLScaled(context, d1, d2, d3);
}

void a5_GLScalef(GLcontext context, GLfloat f1, GLfloat f2, GLfloat f3)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLScalef, 3u);
        if (rec_)
        {
            rec_[0] = fbits((float)f1);
            rec_[1] = fbits((float)f2);
            rec_[2] = fbits((float)f3);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLScalef(context, f1, f2, f3);
}

void a5_GLScissor(GLcontext context, GLint i1, GLint i2, GLsizei n3, GLsizei n4)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLScissor, 4u);
        if (rec_)
        {
            rec_[0] = (uint32_t)i1;
            rec_[1] = (uint32_t)i2;
            rec_[2] = (uint32_t)n3;
            rec_[3] = (uint32_t)n4;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLScissor(context, i1, i2, n3, n4);
}

void a5_GLShadeModel(GLcontext context, GLenum e1)
{
    e1 = (GLenum)tr((uint32_t)e1);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLShadeModel, 1u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLShadeModel(context, e1);
}

void a5_GLTexCoord2f(GLcontext context, GLfloat f1, GLfloat f2)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLTexCoord2f, 2u);
        if (rec_)
        {
            rec_[0] = fbits((float)f1);
            rec_[1] = fbits((float)f2);
        }
        if (!(x5_->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    GLTexCoord2f(context, f1, f2);
}

void a5_GLTexCoord2fv(GLcontext context, GLfloat * p1)
{
    if (!p1) return;                          /* 29.1: NULL is ignored */
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLTexCoord2fv, 2u);
        if (rec_)
        {
            memcpy(&rec_[0], p1, 8u);
        }
        if (!(x5_->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    GLTexCoord2fv(context, p1);
}

void a5_GLTexCoord4f(GLcontext context, GLfloat f1, GLfloat f2, GLfloat f3, GLfloat f4)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLTexCoord4f, 4u);
        if (rec_)
        {
            rec_[0] = fbits((float)f1);
            rec_[1] = fbits((float)f2);
            rec_[2] = fbits((float)f3);
            rec_[3] = fbits((float)f4);
        }
        if (!(x5_->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    GLTexCoord4f(context, f1, f2, f3, f4);
}

void a5_GLTexCoord4fv(GLcontext context, GLfloat * p1)
{
    if (!p1) return;                          /* 29.1: NULL is ignored */
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLTexCoord4fv, 4u);
        if (rec_)
        {
            memcpy(&rec_[0], p1, 16u);
        }
        if (!(x5_->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    GLTexCoord4fv(context, p1);
}

void a5_GLTexCoordPointer(GLcontext context, GLint i1, GLenum e2, GLsizei n3, const GLvoid * p4)
{
    e2 = (GLenum)tr((uint32_t)e2);
    GLTexCoordPointer(context, i1, e2, n3, p4);
}

void a5_GLTexEnvi(GLcontext context, GLenum e1, GLenum e2, GLint i3)
{
    e1 = (GLenum)tr((uint32_t)e1);
    e2 = (GLenum)tr((uint32_t)e2);
    if ((uint32_t)e2 == GL_TEXTURE_ENV_MODE) i3 = (GLint)tr((uint32_t)i3);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLTexEnvi, 3u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            rec_[1] = (uint32_t)e2;
            rec_[2] = (uint32_t)i3;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLTexEnvi_a5(context, e1, e2, i3);
}

void a5_GLTexGeni(GLcontext context, GLenum e1, GLenum e2, GLenum e3)
{
    e1 = (GLenum)tr((uint32_t)e1);
    e2 = (GLenum)tr((uint32_t)e2);
    if ((uint32_t)e2 == GL_TEXTURE_GEN_MODE) e3 = (GLenum)tr((uint32_t)e3);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLTexGeni, 3u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            rec_[1] = (uint32_t)e2;
            rec_[2] = (uint32_t)e3;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLTexGeni_a5(context, e1, e2, e3);
}

void a5_GLTexParameteri(GLcontext context, GLenum e1, GLenum e2, GLint i3)
{
    e1 = (GLenum)tr((uint32_t)e1);
    e2 = (GLenum)tr((uint32_t)e2);
    if (texpar_is_token((uint32_t)e2)) i3 = (GLint)tr((uint32_t)i3);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLTexParameteri, 3u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            rec_[1] = (uint32_t)e2;
            rec_[2] = (uint32_t)i3;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLTexParameteri(context, e1, e2, i3);
}

void a5_GLTranslated(GLcontext context, GLdouble d1, GLdouble d2, GLdouble d3)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLTranslated, 6u);
        if (rec_)
        {
            put_d(&rec_[0], (double)d1);
            put_d(&rec_[2], (double)d2);
            put_d(&rec_[4], (double)d3);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLTranslated(context, d1, d2, d3);
}

void a5_GLTranslatef(GLcontext context, GLfloat f1, GLfloat f2, GLfloat f3)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLTranslatef, 3u);
        if (rec_)
        {
            rec_[0] = fbits((float)f1);
            rec_[1] = fbits((float)f2);
            rec_[2] = fbits((float)f3);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLTranslatef(context, f1, f2, f3);
}

void a5_GLVertex2fv(GLcontext context, GLfloat * p1)
{
    if (!p1) return;                          /* 29.1: NULL is ignored */
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLVertex2fv, 2u);
        if (rec_)
        {
            memcpy(&rec_[0], p1, 8u);
        }
        if (!(x5_->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    GLVertex2fv_a5(context, p1);
}

void a5_GLVertex3fv(GLcontext context, GLfloat * p1)
{
    if (!p1) return;                          /* 29.1: NULL is ignored */
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLVertex3fv, 3u);
        if (rec_)
        {
            memcpy(&rec_[0], p1, 12u);
        }
        if (!(x5_->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    GLVertex3fv_a5(context, p1);
}

void a5_GLVertex4f(GLcontext context, GLfloat f1, GLfloat f2, GLfloat f3, GLfloat f4)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLVertex4f, 4u);
        if (rec_)
        {
            rec_[0] = fbits((float)f1);
            rec_[1] = fbits((float)f2);
            rec_[2] = fbits((float)f3);
            rec_[3] = fbits((float)f4);
        }
        if (!(x5_->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    GLVertex4f_a5(context, f1, f2, f3, f4);
}

void a5_GLVertex4fv(GLcontext context, GLfloat * p1)
{
    if (!p1) return;                          /* 29.1: NULL is ignored */
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLVertex4fv, 4u);
        if (rec_)
        {
            memcpy(&rec_[0], p1, 16u);
        }
        if (!(x5_->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    GLVertex4fv_a5(context, p1);
}

void a5_GLVertexPointer(GLcontext context, GLint i1, GLenum e2, GLsizei n3, const GLvoid * p4)
{
    e2 = (GLenum)tr((uint32_t)e2);
    GLVertexPointer(context, i1, e2, n3, p4);
}

void a5_GLViewport(GLcontext context, GLint i1, GLint i2, GLsizei n3, GLsizei n4)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLViewport, 4u);
        if (rec_)
        {
            rec_[0] = (uint32_t)i1;
            rec_[1] = (uint32_t)i2;
            rec_[2] = (uint32_t)n3;
            rec_[3] = (uint32_t)n4;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLViewport(context, i1, i2, n3, n4);
}

void a5_GLPolygonOffset(GLcontext context, GLfloat f1, GLfloat f2)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLPolygonOffset, 2u);
        if (rec_)
        {
            rec_[0] = fbits((float)f1);
            rec_[1] = fbits((float)f2);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLPolygonOffset(context, f1, f2);
}

void a5_GLBlendEquation(GLcontext context, GLenum e1)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLBlendEquation, 1u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLBlendEquation(context, e1);
}

void a5_GLBlendFuncSeparate(GLcontext context, GLenum e1, GLenum e2, GLenum e3, GLenum e4)
{
    e1 = (GLenum)tr_blend((uint32_t)e1);
    e2 = (GLenum)tr_blend((uint32_t)e2);
    e3 = (GLenum)tr_blend((uint32_t)e3);
    e4 = (GLenum)tr_blend((uint32_t)e4);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLBlendFuncSeparate, 4u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            rec_[1] = (uint32_t)e2;
            rec_[2] = (uint32_t)e3;
            rec_[3] = (uint32_t)e4;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLBlendFuncSeparate(context, e1, e2, e3, e4);
}

void a5_GLLineWidth(GLcontext context, GLfloat f1)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLLineWidth, 1u);
        if (rec_)
        {
            rec_[0] = fbits((float)f1);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLLineWidth(context, f1);
}

void a5_GLTexGenfv(GLcontext context, GLenum e1, GLenum e2, const GLfloat * p3)
{
    e1 = (GLenum)tr((uint32_t)e1);
    e2 = (GLenum)tr((uint32_t)e2);
    if (!p3) { mgl_seterr(context, GL_INVALID_VALUE); return; }
    GLfloat p3_l[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    const uint32_t p3_n = cnt_texgen((uint32_t)e2);
    for (uint32_t k_ = 0; k_ < p3_n; k_++) p3_l[k_] = p3[k_];
    if (pfmode_is_token((uint32_t)e2)) p3_l[0] = (GLfloat)tr((uint32_t)p3_l[0]);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLTexGenfv, 6u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            rec_[1] = (uint32_t)e2;
            memcpy(&rec_[2], p3_l, 16u);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLTexGenfv_a5(context, e1, e2, p3_l);
}

void a5_GLCopyTexImage2D(GLcontext context, GLenum e1, GLint i2, GLenum e3, GLint i4, GLint i5, GLsizei n6, GLsizei n7, GLint i8)
{
    e1 = (GLenum)tr((uint32_t)e1);
    e3 = (GLenum)tr_ifmt((uint32_t)e3);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLCopyTexImage2D, 8u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            rec_[1] = (uint32_t)i2;
            rec_[2] = (uint32_t)e3;
            rec_[3] = (uint32_t)i4;
            rec_[4] = (uint32_t)i5;
            rec_[5] = (uint32_t)n6;
            rec_[6] = (uint32_t)n7;
            rec_[7] = (uint32_t)i8;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLCopyTexImage2D(context, e1, i2, e3, i4, i5, n6, n7, i8);
}

void a5_GLCopyTexSubImage2D(GLcontext context, GLenum e1, GLint i2, GLint i3, GLint i4, GLint i5, GLint i6, GLsizei n7, GLsizei n8)
{
    e1 = (GLenum)tr((uint32_t)e1);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLCopyTexSubImage2D, 8u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            rec_[1] = (uint32_t)i2;
            rec_[2] = (uint32_t)i3;
            rec_[3] = (uint32_t)i4;
            rec_[4] = (uint32_t)i5;
            rec_[5] = (uint32_t)i6;
            rec_[6] = (uint32_t)n7;
            rec_[7] = (uint32_t)n8;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLCopyTexSubImage2D(context, e1, i2, i3, i4, i5, i6, n7, n8);
}

void a5_GLEdgeFlag(GLcontext context, GLboolean b1)
{
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLEdgeFlag, 1u);
        if (rec_)
        {
            rec_[0] = (uint32_t)b1;
        }
        if (!(x5_->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    GLEdgeFlag(context, b1);
}

void a5_GLLightf(GLcontext context, GLenum e1, GLenum e2, GLfloat f3)
{
    e1 = (GLenum)tr((uint32_t)e1);
    e2 = (GLenum)tr((uint32_t)e2);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLLightf, 3u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            rec_[1] = (uint32_t)e2;
            rec_[2] = fbits((float)f3);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLLightf(context, e1, e2, f3);
}

void a5_GLLightfv(GLcontext context, GLenum e1, GLenum e2, const GLfloat * p3)
{
    e1 = (GLenum)tr((uint32_t)e1);
    e2 = (GLenum)tr((uint32_t)e2);
    if (!p3) { mgl_seterr(context, GL_INVALID_VALUE); return; }
    GLfloat p3_l[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    const uint32_t p3_n = cnt_light((uint32_t)e2);
    for (uint32_t k_ = 0; k_ < p3_n; k_++) p3_l[k_] = p3[k_];
    if (pfmode_is_token((uint32_t)e2)) p3_l[0] = (GLfloat)tr((uint32_t)p3_l[0]);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLLightfv, 6u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            rec_[1] = (uint32_t)e2;
            memcpy(&rec_[2], p3_l, 16u);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLLightfv(context, e1, e2, p3_l);
}

void a5_GLMaterialf(GLcontext context, GLenum e1, GLenum e2, GLfloat f3)
{
    e1 = (GLenum)tr((uint32_t)e1);
    e2 = (GLenum)tr((uint32_t)e2);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLMaterialf, 3u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            rec_[1] = (uint32_t)e2;
            rec_[2] = fbits((float)f3);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLMaterialf(context, e1, e2, f3);
}

void a5_GLMaterialfv(GLcontext context, GLenum e1, GLenum e2, const GLfloat * p3)
{
    e1 = (GLenum)tr((uint32_t)e1);
    e2 = (GLenum)tr((uint32_t)e2);
    if (!p3) { mgl_seterr(context, GL_INVALID_VALUE); return; }
    GLfloat p3_l[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    const uint32_t p3_n = cnt_mat((uint32_t)e2);
    for (uint32_t k_ = 0; k_ < p3_n; k_++) p3_l[k_] = p3[k_];
    if (pfmode_is_token((uint32_t)e2)) p3_l[0] = (GLfloat)tr((uint32_t)p3_l[0]);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLMaterialfv, 6u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            rec_[1] = (uint32_t)e2;
            memcpy(&rec_[2], p3_l, 16u);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLMaterialfv(context, e1, e2, p3_l);
}

void a5_GLLightModelf(GLcontext context, GLenum e1, GLfloat f2)
{
    e1 = (GLenum)tr((uint32_t)e1);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLLightModelf, 2u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            rec_[1] = fbits((float)f2);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLLightModelf(context, e1, f2);
}

void a5_GLLightModelfv(GLcontext context, GLenum e1, const GLfloat * p2)
{
    e1 = (GLenum)tr((uint32_t)e1);
    if (!p2) { mgl_seterr(context, GL_INVALID_VALUE); return; }
    GLfloat p2_l[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    const uint32_t p2_n = cnt_lm((uint32_t)e1);
    for (uint32_t k_ = 0; k_ < p2_n; k_++) p2_l[k_] = p2[k_];
    if (pfmode_is_token((uint32_t)e1)) p2_l[0] = (GLfloat)tr((uint32_t)p2_l[0]);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLLightModelfv, 5u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            memcpy(&rec_[1], p2_l, 16u);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLLightModelfv(context, e1, p2_l);
}

void a5_GLGetLightfv(GLcontext context, GLenum e1, GLenum e2, GLfloat * p3)
{
    e1 = (GLenum)tr((uint32_t)e1);
    e2 = (GLenum)tr((uint32_t)e2);
    GLGetLightfv(context, e1, e2, p3);
}

void a5_GLGetMaterialfv(GLcontext context, GLenum e1, GLenum e2, GLfloat * p3)
{
    e1 = (GLenum)tr((uint32_t)e1);
    e2 = (GLenum)tr((uint32_t)e2);
    GLGetMaterialfv(context, e1, e2, p3);
}

void a5_GLColorMaterial(GLcontext context, GLenum e1, GLenum e2)
{
    e1 = (GLenum)tr((uint32_t)e1);
    e2 = (GLenum)tr((uint32_t)e2);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLColorMaterial, 2u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            rec_[1] = (uint32_t)e2;
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLColorMaterial(context, e1, e2);
}

void a5_GLTexEnvfv(GLcontext context, GLenum e1, GLenum e2, const GLfloat * p3)
{
    e1 = (GLenum)tr((uint32_t)e1);
    e2 = (GLenum)tr((uint32_t)e2);
    if (!p3) { mgl_seterr(context, GL_INVALID_VALUE); return; }
    GLfloat p3_l[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    const uint32_t p3_n = cnt_texenv((uint32_t)e2);
    for (uint32_t k_ = 0; k_ < p3_n; k_++) p3_l[k_] = p3[k_];
    if (pfmode_is_token((uint32_t)e2)) p3_l[0] = (GLfloat)tr((uint32_t)p3_l[0]);
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLTexEnvfv, 6u);
        if (rec_)
        {
            rec_[0] = (uint32_t)e1;
            rec_[1] = (uint32_t)e2;
            memcpy(&rec_[2], p3_l, 16u);
        }
        if (!(x5_->dl_exec & MGL_DLX_STATE))
            return;
    }
    GLTexEnvfv_a5(context, e1, e2, p3_l);
}

void a5_GLNormal3fv(GLcontext context, GLfloat * p1)
{
    if (!p1) return;                          /* 29.1: NULL is ignored */
    mgl_ctx5 *const x5_ = X5(context);
    if (x5_->dl_cur)
    {
        uint32_t *rec_ = M2B_PUT(context, x5_, OP5_GLNormal3fv, 3u);
        if (rec_)
        {
            memcpy(&rec_[0], p1, 12u);
        }
        if (!(x5_->dl_exec & MGL_DLX_VERTEX))
            return;
    }
    GLNormal3fv(context, p1);
}

/* one recorded call of a generated op, its arguments in w (1 = handled) */
static int dl_replay_gen(GLcontext c, uint32_t op, const uint32_t *w)
{
    switch (op)
    {
    case OP5_GLActiveTextureARB: GLActiveTextureARB(c, (GLenum)w[0]); break;
    case OP5_GLAlphaFunc: GLAlphaFunc(c, (GLenum)w[0], bitsf(w[1])); break;
    case OP5_GLBegin: GLBegin(c, (GLenum)w[0]); break;
    case OP5_GLBindTexture: GLBindTexture_a5(c, (GLenum)w[0], (GLuint)w[1]); break;
    case OP5_GLBlendFunc: GLBlendFunc(c, (GLenum)w[0], (GLenum)w[1]); break;
    case OP5_GLClear: GLClear_a5(c, (GLbitfield)w[0]); break;
    case OP5_GLClearColor: GLClearColor(c, bitsf(w[0]), bitsf(w[1]), bitsf(w[2]), bitsf(w[3])); break;
    case OP5_GLClearDepth: GLClearDepth(c, get_d(&w[0])); break;
    case OP5_GLColor3fv: GLColor3fv(c, (GLfloat *)(void *)&w[0]); break;
    case OP5_GLColor3ubv: GLColor3ubv(c, (GLubyte *)(void *)&w[0]); break;
    case OP5_GLColor4f: GLColor4f(c, bitsf(w[0]), bitsf(w[1]), bitsf(w[2]), bitsf(w[3])); break;
    case OP5_GLColor4fv: GLColor4fv(c, (GLfloat *)(void *)&w[0]); break;
    case OP5_GLColor4ub: GLColor4ub(c, (GLubyte)w[0], (GLubyte)w[1], (GLubyte)w[2], (GLubyte)w[3]); break;
    case OP5_GLColor4ubv: GLColor4ubv(c, (GLubyte *)(void *)&w[0]); break;
    case OP5_GLColorMask: GLColorMask(c, (GLboolean)w[0], (GLboolean)w[1], (GLboolean)w[2], (GLboolean)w[3]); break;
    case OP5_GLCullFace: GLCullFace(c, (GLenum)w[0]); break;
    case OP5_GLDepthFunc: GLDepthFunc(c, (GLenum)w[0]); break;
    case OP5_GLDepthMask: GLDepthMask(c, (GLboolean)w[0]); break;
    case OP5_GLDepthRange: GLDepthRange(c, get_d(&w[0]), get_d(&w[2])); break;
    case OP5_GLDrawBuffer: GLDrawBuffer(c, (GLenum)w[0]); break;
    case OP5_GLEnd: GLEnd(c); break;
    case OP5_GLFogf: GLFogf(c, (GLenum)w[0], bitsf(w[1])); break;
    case OP5_GLFogfv: GLFogfv(c, (GLenum)w[0], (GLfloat *)(void *)&w[1]); break;
    case OP5_GLFrontFace: GLFrontFace(c, (GLenum)w[0]); break;
    case OP5_GLFrustum: GLFrustum(c, get_d(&w[0]), get_d(&w[2]), get_d(&w[4]), get_d(&w[6]), get_d(&w[8]), get_d(&w[10])); break;
    case OP5_GLHint: GLHint(c, (GLenum)w[0], (GLenum)w[1]); break;
    case OP5_GLLoadIdentity: GLLoadIdentity_a5(c); break;
    case OP5_GLLoadMatrixd: GLLoadMatrixd_a5(c, (const GLdouble *)(void *)&w[0]); break;
    case OP5_GLLoadMatrixf: GLLoadMatrixf_a5(c, (const GLfloat *)(void *)&w[0]); break;
    case OP5_GLMatrixMode: GLMatrixMode(c, (GLenum)w[0]); break;
    case OP5_GLMultMatrixd: GLMultMatrixd(c, (const GLdouble *)(void *)&w[0]); break;
    case OP5_GLMultMatrixf: GLMultMatrixf(c, (const GLfloat *)(void *)&w[0]); break;
    case OP5_GLMultiTexCoord2fARB: GLMultiTexCoord2fARB(c, (GLenum)w[0], bitsf(w[1]), bitsf(w[2])); break;
    case OP5_GLMultiTexCoord2fvARB: GLMultiTexCoord2fvARB(c, (GLenum)w[0], (GLfloat *)(void *)&w[1]); break;
    case OP5_GLNormal3f: GLNormal3f(c, bitsf(w[0]), bitsf(w[1]), bitsf(w[2])); break;
    case OP5_GLOrtho: GLOrtho(c, get_d(&w[0]), get_d(&w[2]), get_d(&w[4]), get_d(&w[6]), get_d(&w[8]), get_d(&w[10])); break;
    case OP5_GLPointSize: GLPointSize(c, bitsf(w[0])); break;
    case OP5_GLPolygonMode: GLPolygonMode(c, (GLenum)w[0], (GLenum)w[1]); break;
    case OP5_GLPopMatrix: GLPopMatrix_a5(c); break;
    case OP5_GLPushMatrix: GLPushMatrix_a5(c); break;
    case OP5_GLRotated: GLRotated(c, get_d(&w[0]), get_d(&w[2]), get_d(&w[4]), get_d(&w[6])); break;
    case OP5_GLRotatef: GLRotatef(c, bitsf(w[0]), bitsf(w[1]), bitsf(w[2]), bitsf(w[3])); break;
    case OP5_GLRotatefEXT: GLRotatefEXT(c, bitsf(w[0]), (const GLint)w[1]); break;
    case OP5_GLRotatefEXTs: GLRotatefEXTs(c, bitsf(w[0]), bitsf(w[1]), (const GLint)w[2]); break;
    case OP5_GLScaled: GLScaled(c, get_d(&w[0]), get_d(&w[2]), get_d(&w[4])); break;
    case OP5_GLScalef: GLScalef(c, bitsf(w[0]), bitsf(w[1]), bitsf(w[2])); break;
    case OP5_GLScissor: GLScissor(c, (GLint)w[0], (GLint)w[1], (GLsizei)w[2], (GLsizei)w[3]); break;
    case OP5_GLShadeModel: GLShadeModel(c, (GLenum)w[0]); break;
    case OP5_GLTexCoord2f: GLTexCoord2f(c, bitsf(w[0]), bitsf(w[1])); break;
    case OP5_GLTexCoord2fv: GLTexCoord2fv(c, (GLfloat *)(void *)&w[0]); break;
    case OP5_GLTexCoord4f: GLTexCoord4f(c, bitsf(w[0]), bitsf(w[1]), bitsf(w[2]), bitsf(w[3])); break;
    case OP5_GLTexCoord4fv: GLTexCoord4fv(c, (GLfloat *)(void *)&w[0]); break;
    case OP5_GLTexEnvi: GLTexEnvi_a5(c, (GLenum)w[0], (GLenum)w[1], (GLint)w[2]); break;
    case OP5_GLTexGeni: GLTexGeni_a5(c, (GLenum)w[0], (GLenum)w[1], (GLenum)w[2]); break;
    case OP5_GLTexParameteri: GLTexParameteri(c, (GLenum)w[0], (GLenum)w[1], (GLint)w[2]); break;
    case OP5_GLTranslated: GLTranslated(c, get_d(&w[0]), get_d(&w[2]), get_d(&w[4])); break;
    case OP5_GLTranslatef: GLTranslatef(c, bitsf(w[0]), bitsf(w[1]), bitsf(w[2])); break;
    case OP5_GLVertex2fv: GLVertex2fv_a5(c, (GLfloat *)(void *)&w[0]); break;
    case OP5_GLVertex3fv: GLVertex3fv_a5(c, (GLfloat *)(void *)&w[0]); break;
    case OP5_GLVertex4f: GLVertex4f_a5(c, bitsf(w[0]), bitsf(w[1]), bitsf(w[2]), bitsf(w[3])); break;
    case OP5_GLVertex4fv: GLVertex4fv_a5(c, (GLfloat *)(void *)&w[0]); break;
    case OP5_GLViewport: GLViewport(c, (GLint)w[0], (GLint)w[1], (GLsizei)w[2], (GLsizei)w[3]); break;
    case OP5_GLPolygonOffset: GLPolygonOffset(c, bitsf(w[0]), bitsf(w[1])); break;
    case OP5_GLBlendEquation: GLBlendEquation(c, (GLenum)w[0]); break;
    case OP5_GLBlendFuncSeparate: GLBlendFuncSeparate(c, (GLenum)w[0], (GLenum)w[1], (GLenum)w[2], (GLenum)w[3]); break;
    case OP5_GLLineWidth: GLLineWidth(c, bitsf(w[0])); break;
    case OP5_GLTexGenfv: GLTexGenfv_a5(c, (GLenum)w[0], (GLenum)w[1], (const GLfloat *)(void *)&w[2]); break;
    case OP5_GLCopyTexImage2D: GLCopyTexImage2D(c, (GLenum)w[0], (GLint)w[1], (GLenum)w[2], (GLint)w[3], (GLint)w[4], (GLsizei)w[5], (GLsizei)w[6], (GLint)w[7]); break;
    case OP5_GLCopyTexSubImage2D: GLCopyTexSubImage2D(c, (GLenum)w[0], (GLint)w[1], (GLint)w[2], (GLint)w[3], (GLint)w[4], (GLint)w[5], (GLsizei)w[6], (GLsizei)w[7]); break;
    case OP5_GLEdgeFlag: GLEdgeFlag(c, (GLboolean)w[0]); break;
    case OP5_GLLightf: GLLightf(c, (GLenum)w[0], (GLenum)w[1], bitsf(w[2])); break;
    case OP5_GLLightfv: GLLightfv(c, (GLenum)w[0], (GLenum)w[1], (const GLfloat *)(void *)&w[2]); break;
    case OP5_GLMaterialf: GLMaterialf(c, (GLenum)w[0], (GLenum)w[1], bitsf(w[2])); break;
    case OP5_GLMaterialfv: GLMaterialfv(c, (GLenum)w[0], (GLenum)w[1], (const GLfloat *)(void *)&w[2]); break;
    case OP5_GLLightModelf: GLLightModelf(c, (GLenum)w[0], bitsf(w[1])); break;
    case OP5_GLLightModelfv: GLLightModelfv(c, (GLenum)w[0], (const GLfloat *)(void *)&w[1]); break;
    case OP5_GLColorMaterial: GLColorMaterial(c, (GLenum)w[0], (GLenum)w[1]); break;
    case OP5_GLTexEnvfv: GLTexEnvfv_a5(c, (GLenum)w[0], (GLenum)w[1], (const GLfloat *)(void *)&w[2]); break;
    case OP5_GLNormal3fv: GLNormal3fv(c, (GLfloat *)(void *)&w[0]); break;
    default: return 0;
    }
    return 1;
}
#include "mgl_abi5_patch.h"
