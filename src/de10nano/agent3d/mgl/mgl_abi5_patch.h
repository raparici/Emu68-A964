/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2 GL core: ABI-5 display-list slots (generated from the minigl.library interface tables). */

#define MGL_A5_PATCH(X) \
    X(0, GLActiveTextureARB, a5_GLActiveTextureARB) \
    X(7, GLClearColor, a5_GLClearColor) \
    X(8, GLClearDepth, a5_GLClearDepth) \
    X(9, GLColor3fv_a5, a5_GLColor3fv) \
    X(10, GLColor3ubv_a5, a5_GLColor3ubv) \
    X(11, GLColor4f, a5_GLColor4f) \
    X(12, GLColor4fv_a5, a5_GLColor4fv) \
    X(13, GLColor4ub, a5_GLColor4ub) \
    X(14, GLColor4ubv_a5, a5_GLColor4ubv) \
    X(15, GLColorMask, a5_GLColorMask) \
    X(21, GLDepthMask, a5_GLDepthMask) \
    X(22, GLDepthRange, a5_GLDepthRange) \
    X(25, GLDrawBuffer, a5_GLDrawBuffer) \
    X(28, GLEnd, a5_GLEnd) \
    X(34, GLFrustum, a5_GLFrustum) \
    X(43, GLLoadIdentity_a5, a5_GLLoadIdentity) \
    X(44, GLLoadMatrixd_a5, a5_GLLoadMatrixd) \
    X(45, GLLoadMatrixf_a5, a5_GLLoadMatrixf) \
    X(48, GLMultMatrixd, a5_GLMultMatrixd) \
    X(49, GLMultMatrixf, a5_GLMultMatrixf) \
    X(50, GLMultiTexCoord2fARB, a5_GLMultiTexCoord2fARB) \
    X(51, GLMultiTexCoord2fvARB_a5, a5_GLMultiTexCoord2fvARB) \
    X(52, GLNormal3f, a5_GLNormal3f) \
    X(53, GLOrtho, a5_GLOrtho) \
    X(55, GLPointSize, a5_GLPointSize) \
    X(57, GLPopMatrix_a5, a5_GLPopMatrix) \
    X(58, GLPushMatrix_a5, a5_GLPushMatrix) \
    X(60, GLRotated, a5_GLRotated) \
    X(61, GLRotatef, a5_GLRotatef) \
    X(62, GLRotatefEXT, a5_GLRotatefEXT) \
    X(63, GLRotatefEXTs, a5_GLRotatefEXTs) \
    X(64, GLScaled, a5_GLScaled) \
    X(65, GLScalef, a5_GLScalef) \
    X(66, GLScissor, a5_GLScissor) \
    X(68, GLTexCoord2f, a5_GLTexCoord2f) \
    X(69, GLTexCoord2fv_a5, a5_GLTexCoord2fv) \
    X(70, GLTexCoord4f, a5_GLTexCoord4f) \
    X(71, GLTexCoord4fv_a5, a5_GLTexCoord4fv) \
    X(78, GLTranslated, a5_GLTranslated) \
    X(79, GLTranslatef, a5_GLTranslatef) \
    X(83, GLVertex2fv_a5, a5_GLVertex2fv) \
    X(84, GLVertex3fv_a5, a5_GLVertex3fv) \
    X(85, GLVertex4f_a5, a5_GLVertex4f) \
    X(86, GLVertex4fv_a5, a5_GLVertex4fv) \
    X(88, GLViewport, a5_GLViewport) \
    X(128, GLPolygonOffset, a5_GLPolygonOffset) \
    X(132, GLBlendEquation, a5_GLBlendEquation) \
    X(136, GLLineWidth, a5_GLLineWidth) \
    X(141, GLEdgeFlag, a5_GLEdgeFlag) \
    X(204, GLNormal3fv_a5, a5_GLNormal3fv) \
