/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2 GL core: the dispatch table (generated from the minigl.library interface tables). */

#define MGL_TABLE_SLOTS 153
#define MGL_TABLE(X) \
    X(  0, void,          GLActiveTextureARB,          (GLcontext context, GLenum e1)) \
    X(  1, void,          GLAlphaFunc,                 (GLcontext context, GLenum e1, GLclampf f2)) \
    X(  2, void,          GLArrayElement,              (GLcontext context, GLint i1)) \
    X(  3, void,          GLBegin,                     (GLcontext context, GLenum e1)) \
    X(  4, void,          GLBindTexture,               (GLcontext context, GLenum e1, GLuint u2)) \
    X(  5, void,          GLBlendFunc,                 (GLcontext context, GLenum e1, GLenum e2)) \
    X(  6, void,          GLClear,                     (GLcontext context, GLbitfield m1)) \
    X(  7, void,          GLClearColor,                (GLcontext context, GLclampf f1, GLclampf f2, GLclampf f3, GLclampf f4)) \
    X(  8, void,          GLClearDepth,                (GLcontext context, GLclampd d1)) \
    X(  9, void,          GLColor3fv,                  (GLcontext context, GLfloat *p1)) \
    X( 10, void,          GLColor3ubv,                 (GLcontext context, GLubyte *p1)) \
    X( 11, void,          GLColor4f,                   (GLcontext context, GLfloat f1, GLfloat f2, GLfloat f3, GLfloat f4)) \
    X( 12, void,          GLColor4fv,                  (GLcontext context, GLfloat *p1)) \
    X( 13, void,          GLColor4ub,                  (GLcontext context, GLubyte ub1, GLubyte ub2, GLubyte ub3, GLubyte ub4)) \
    X( 14, void,          GLColor4ubv,                 (GLcontext context, GLubyte *p1)) \
    X( 15, void,          GLColorMask,                 (GLcontext context, GLboolean b1, GLboolean b2, GLboolean b3, GLboolean b4)) \
    X( 16, void,          GLColorPointer,              (GLcontext context, GLint i1, GLenum e2, GLsizei n3, const GLvoid *p4)) \
    X( 17, void,          GLColorTable,                (GLcontext context, GLenum e1, GLenum e2, GLint i3, GLenum e4, GLenum e5, GLvoid *p6)) \
    X( 18, void,          GLCullFace,                  (GLcontext context, GLenum e1)) \
    X( 19, void,          GLDeleteTextures,            (GLcontext context, GLsizei n1, const GLuint *p2)) \
    X( 20, void,          GLDepthFunc,                 (GLcontext context, GLenum e1)) \
    X( 21, void,          GLDepthMask,                 (GLcontext context, GLboolean b1)) \
    X( 22, void,          GLDepthRange,                (GLcontext context, GLclampd d1, GLclampd d2)) \
    X( 23, void,          GLDisableClientState,        (GLcontext context, GLenum e1)) \
    X( 24, void,          GLDrawArrays,                (GLcontext context, GLenum e1, GLint i2, GLsizei n3)) \
    X( 25, void,          GLDrawBuffer,                (GLcontext context, GLenum e1)) \
    X( 26, void,          GLDrawElements,              (GLcontext context, GLenum e1, GLsizei n2, GLenum e3, const GLvoid *p4)) \
    X( 27, void,          GLEnableClientState,         (GLcontext context, GLenum e1)) \
    X( 28, void,          GLEnd,                       (GLcontext context)) \
    X( 29, void,          GLFinish,                    (GLcontext context)) \
    X( 30, void,          GLFlush,                     (GLcontext context)) \
    X( 31, void,          GLFogf,                      (GLcontext context, GLenum e1, GLfloat f2)) \
    X( 32, void,          GLFogfv,                     (GLcontext context, GLenum e1, GLfloat *p2)) \
    X( 33, void,          GLFrontFace,                 (GLcontext context, GLenum e1)) \
    X( 34, void,          GLFrustum,                   (GLcontext context, GLdouble d1, GLdouble d2, GLdouble d3, GLdouble d4, GLdouble d5, GLdouble d6)) \
    X( 35, void,          GLGenTextures,               (GLcontext context, GLsizei n1, GLuint *p2)) \
    X( 36, void,          GLGetBooleanv,               (GLcontext context, GLenum e1, GLboolean *p2)) \
    X( 37, GLenum,        GLGetError,                  (GLcontext context)) \
    X( 38, void,          GLGetFloatv,                 (GLcontext context, GLenum e1, GLfloat *p2)) \
    X( 39, void,          GLGetIntegerv,               (GLcontext context, GLenum e1, GLint *p2)) \
    X( 40, const GLubyte*, GLGetString,                 (GLcontext context, GLenum e1)) \
    X( 41, void,          GLHint,                      (GLcontext context, GLenum e1, GLenum e2)) \
    X( 42, GLboolean,     GLIsEnabled,                 (GLcontext context, GLenum e1)) \
    X( 43, void,          GLLoadIdentity,              (GLcontext context)) \
    X( 44, void,          GLLoadMatrixd,               (GLcontext context, const GLdouble *p1)) \
    X( 45, void,          GLLoadMatrixf,               (GLcontext context, const GLfloat *p1)) \
    X( 46, void,          GLLockArrays,                (GLcontext context, GLuint u1, GLsizei n2)) \
    X( 47, void,          GLMatrixMode,                (GLcontext context, GLenum e1)) \
    X( 48, void,          GLMultMatrixd,               (GLcontext context, const GLdouble *p1)) \
    X( 49, void,          GLMultMatrixf,               (GLcontext context, const GLfloat *p1)) \
    X( 50, void,          GLMultiTexCoord2fARB,        (GLcontext context, GLenum e1, GLfloat f2, GLfloat f3)) \
    X( 51, void,          GLMultiTexCoord2fvARB,       (GLcontext context, GLenum e1, GLfloat *p2)) \
    X( 52, void,          GLNormal3f,                  (GLcontext context, GLfloat f1, GLfloat f2, GLfloat f3)) \
    X( 53, void,          GLOrtho,                     (GLcontext context, GLdouble d1, GLdouble d2, GLdouble d3, GLdouble d4, GLdouble d5, GLdouble d6)) \
    X( 54, void,          GLPixelStorei,               (GLcontext context, GLenum e1, GLint i2)) \
    X( 55, void,          GLPointSize,                 (GLcontext context, GLfloat f1)) \
    X( 56, void,          GLPolygonMode,               (GLcontext context, GLenum e1, GLenum e2)) \
    X( 57, void,          GLPopMatrix,                 (GLcontext context)) \
    X( 58, void,          GLPushMatrix,                (GLcontext context)) \
    X( 59, void,          GLReadPixels,                (GLcontext context, GLint i1, GLint i2, GLsizei n3, GLsizei n4, GLenum e5, GLenum e6, GLvoid *p7)) \
    X( 60, void,          GLRotated,                   (GLcontext context, GLdouble d1, GLdouble d2, GLdouble d3, GLdouble d4)) \
    X( 61, void,          GLRotatef,                   (GLcontext context, GLfloat f1, GLfloat f2, GLfloat f3, GLfloat f4)) \
    X( 62, void,          GLRotatefEXT,                (GLcontext context, GLfloat f1, const GLint i2)) \
    X( 63, void,          GLRotatefEXTs,               (GLcontext context, GLfloat f1, GLfloat f2, const GLint i3)) \
    X( 64, void,          GLScaled,                    (GLcontext context, GLdouble d1, GLdouble d2, GLdouble d3)) \
    X( 65, void,          GLScalef,                    (GLcontext context, GLfloat f1, GLfloat f2, GLfloat f3)) \
    X( 66, void,          GLScissor,                   (GLcontext context, GLint i1, GLint i2, GLsizei n3, GLsizei n4)) \
    X( 67, void,          GLShadeModel,                (GLcontext context, GLenum e1)) \
    X( 68, void,          GLTexCoord2f,                (GLcontext context, GLfloat f1, GLfloat f2)) \
    X( 69, void,          GLTexCoord2fv,               (GLcontext context, GLfloat *p1)) \
    X( 70, void,          GLTexCoord4f,                (GLcontext context, GLfloat f1, GLfloat f2, GLfloat f3, GLfloat f4)) \
    X( 71, void,          GLTexCoord4fv,               (GLcontext context, GLfloat *p1)) \
    X( 72, void,          GLTexCoordPointer,           (GLcontext context, GLint i1, GLenum e2, GLsizei n3, const GLvoid *p4)) \
    X( 73, void,          GLTexEnvi,                   (GLcontext context, GLenum e1, GLenum e2, GLint i3)) \
    X( 74, void,          GLTexGeni,                   (GLcontext context, GLenum e1, GLenum e2, GLenum e3)) \
    X( 75, void,          GLTexImage2D,                (GLcontext context, GLenum e1, GLint i2, GLint i3, GLsizei n4, GLsizei n5, GLint i6, GLenum e7, GLenum e8, const GLvoid *p9)) \
    X( 76, void,          GLTexParameteri,             (GLcontext context, GLenum e1, GLenum e2, GLint i3)) \
    X( 77, void,          GLTexSubImage2D,             (GLcontext context, GLenum e1, GLint i2, GLint i3, GLint i4, GLsizei n5, GLsizei n6, GLenum e7, GLenum e8, const GLvoid *p9)) \
    X( 78, void,          GLTranslated,                (GLcontext context, GLdouble d1, GLdouble d2, GLdouble d3)) \
    X( 79, void,          GLTranslatef,                (GLcontext context, GLfloat f1, GLfloat f2, GLfloat f3)) \
    X( 80, void,          GLULookAt,                   (GLfloat f1, GLfloat f2, GLfloat f3, GLfloat f4, GLfloat f5, GLfloat f6, GLfloat f7, GLfloat f8, GLfloat f9)) \
    X( 81, void,          GLUPerspective,              (GLfloat f1, GLfloat f2, GLfloat f3, GLfloat f4)) \
    X( 82, void,          GLUnlockArrays,              (GLcontext context)) \
    X( 83, void,          GLVertex2fv,                 (GLcontext context, GLfloat *p1)) \
    X( 84, void,          GLVertex3fv,                 (GLcontext context, GLfloat *p1)) \
    X( 85, void,          GLVertex4f,                  (GLcontext context, GLfloat f1, GLfloat f2, GLfloat f3, GLfloat f4)) \
    X( 86, void,          GLVertex4fv,                 (GLcontext context, GLfloat *p1)) \
    X( 87, void,          GLVertexPointer,             (GLcontext context, GLint i1, GLenum e2, GLsizei n3, const GLvoid *p4)) \
    X( 88, void,          GLViewport,                  (GLcontext context, GLint i1, GLint i2, GLsizei n3, GLsizei n4)) \
    X( 89, void,          MGLClearPointer,             (GLcontext context)) \
    X( 90, void *,        MGLCreateContext,            (int i1, int i2, int i3, int i4)) \
    X( 91, void *,        MGLCreateContextFromID,      (GLint i1, GLint *p2, GLint *p3)) \
    X( 92, void,          MGLDeleteContext,            (GLcontext context)) \
    X( 93, void,          MGLDrawMultitexBuffer,       (GLcontext context, GLenum e1, GLenum e2, GLenum e3)) \
    X( 94, void,          MGLEnableSync,               (GLcontext context, GLboolean b1)) \
    X( 95, void,          MGLExit,                     (GLcontext context)) \
    X( 96, void *,        MGLGetInputWindowHandle,     (GLcontext context)) \
    X( 97, void *,        MGLGetWindowHandle,          (GLcontext context)) \
    X( 98, void,          MGLIdleFunc,                 (GLcontext context, IdleFn fn1)) \
    X( 99, void,          MGLKeyFunc,                  (GLcontext context, KeyHandlerFn fn1)) \
    X(100, GLboolean,     MGLLockBack,                 (GLcontext context, MGLLockInfo *p1)) \
    X(101, GLboolean,     MGLLockDisplay,              (GLcontext context)) \
    X(102, void,          MGLLockMode,                 (GLcontext context, GLenum e1)) \
    X(103, void,          MGLMainLoop,                 (GLcontext context)) \
    X(104, void,          MGLMinTriArea,               (GLcontext context, GLfloat f1)) \
    X(105, void,          MGLMouseFunc,                (GLcontext context, MouseHandlerFn fn1)) \
    X(106, void,          MGLPrintMatrix,              (GLcontext context, int i1)) \
    X(107, void,          MGLPrintMatrixStack,         (GLcontext context, int i1)) \
    X(108, void,          MGLResizeContext,            (GLcontext context, GLsizei n1, GLsizei n2)) \
    X(109, void,          MGLSetPointer,               (GLcontext context)) \
    X(110, void,          MGLSetState,                 (GLcontext context, GLenum e1, GLboolean b2)) \
    X(111, void,          MGLSetZOffset,               (GLcontext context, GLfloat f1)) \
    X(112, void,          MGLSpecialFunc,              (GLcontext context, SpecialHandlerFn fn1)) \
    X(113, void,          MGLSwitchDisplay,            (GLcontext context)) \
    X(114, void,          MGLTexMemStat,               (GLcontext context, GLint *p1, GLint *p2)) \
    X(115, void,          MGLUnlockDisplay,            (GLcontext context)) \
    X(116, void,          MGLWriteShotPPM,             (GLcontext context, char *p1)) \
    X(117, void,          mglChooseGuardBand,          (GLboolean b1)) \
    X(118, void,          mglChooseMtexBufferSize,     (int i1)) \
    X(119, void,          mglChooseNumberOfBuffers,    (int i1)) \
    X(120, void,          mglChoosePixelDepth,         (int i1)) \
    X(121, void,          mglChooseTextureBufferSize,  (int i1)) \
    X(122, void,          mglChooseVertexBufferSize,   (int i1)) \
    X(123, void,          mglChooseWindowMode,         (GLboolean b1)) \
    X(124, GLint,         mglGetSupportedScreenModes,  (MGLScreenModeCallback fn1)) \
    X(125, void,          mglProhibitAlphaFallback,    (GLboolean b1)) \
    X(126, void,          mglProhibitMipMapping,       (GLboolean b1)) \
    X(127, void,          mglProposeCloseDesktop,      (GLboolean b1)) \
    X(128, void,          GLPolygonOffset,             (GLcontext context, GLfloat f1, GLfloat f2)) \
    X(129, void,          GLClientActiveTextureARB,    (GLcontext context, GLenum e1)) \
    X(130, void,          GLInterleavedArrays,         (GLcontext context, GLenum e1, GLsizei n2, const GLvoid *p3)) \
    X(131, void,          GLMultiDrawArrays,           (GLcontext context, GLenum e1, const GLint *p2, const GLsizei *p3, GLsizei n4)) \
    X(132, void,          GLBlendEquation,             (GLcontext context, GLenum e1)) \
    X(133, void,          GLBlendFuncSeparate,         (GLcontext context, GLenum e1, GLenum e2, GLenum e3, GLenum e4)) \
    X(134, GLboolean,     GLIsTexture,                 (GLcontext context, GLuint u1)) \
    X(135, void,          mglChooseZBufferDepth,       (int i1)) \
    X(136, void,          GLLineWidth,                 (GLcontext context, GLfloat f1)) \
    X(137, void,          GLTexGenfv,                  (GLcontext context, GLenum e1, GLenum e2, const GLfloat *p3)) \
    X(138, void,          GLCopyTexImage2D,            (GLcontext context, GLenum e1, GLint i2, GLenum e3, GLint i4, GLint i5, GLsizei n6, GLsizei n7, GLint i8)) \
    X(139, void,          GLCopyTexSubImage2D,         (GLcontext context, GLenum e1, GLint i2, GLint i3, GLint i4, GLint i5, GLint i6, GLsizei n7, GLsizei n8)) \
    X(140, GLboolean,     GLAreTexturesResident,       (GLcontext context, GLsizei n1, const GLuint *p2, GLboolean *p3)) \
    X(141, void,          GLEdgeFlag,                  (GLcontext context, GLboolean b1)) \
    X(142, void,          GLEdgeFlagPointer,           (GLcontext context, GLsizei n1, const GLvoid *p2)) \
    X(143, void,          GLEdgeFlagv,                 (GLcontext context, const GLboolean *p1)) \
    X(144, void,          GLGetDoublev,                (GLcontext context, GLenum e1, GLdouble *p2)) \
    X(145, void,          GLGetPointerv,               (GLcontext context, GLenum e1, GLvoid **p2)) \
    X(146, void,          GLIndexi,                    (GLcontext context, GLint i1)) \
    X(147, void,          GLIndexiv,                   (GLcontext context, const GLint *p1)) \
    X(148, void,          GLIndexPointer,              (GLcontext context, GLenum e1, GLsizei n2, const GLvoid *p3)) \
    X(149, void,          GLPrioritizeTextures,        (GLcontext context, GLsizei n1, const GLuint *p2, const GLclampf *p3)) \
    X(150, void,          GLReadBuffer,                (GLcontext context, GLenum e1)) \
    X(151, void *,        MGLCreateContextFromWindow,  (struct Window *p1)) \
    X(152, void *,        MGLCreateContextFromBitMap,  (struct BitMap *p1)) \
