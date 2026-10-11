/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2 GL core: the minigl.library binary interface. */

#ifndef MGL_ABI_H
#define MGL_ABI_H

#include <stdint.h>

typedef void            GLvoid;
typedef char            GLbyte;
typedef unsigned char   GLubyte;
typedef short           GLshort;
typedef unsigned short  GLushort;
typedef int             GLint;
typedef unsigned int    GLuint;
typedef unsigned int    GLboolean;
typedef long            GLsizei;
typedef unsigned long   GLbitfield;
typedef float           GLfloat;
typedef double          GLdouble;
typedef float           GLclampf;
typedef double          GLclampd;
typedef unsigned int    GLenum;

typedef struct mgl_context *GLcontext;      /* opaque to programs: the standard's mglmacros.h is a no-op */

struct Window;                              /* AmigaOS types the context-creation slots take */
struct BitMap;

typedef enum
{
    MGLKEY_F1, MGLKEY_F2, MGLKEY_F3, MGLKEY_F4, MGLKEY_F5, MGLKEY_F6, MGLKEY_F7, MGLKEY_F8, MGLKEY_F9, MGLKEY_F10,
    MGLKEY_CUP, MGLKEY_CDOWN, MGLKEY_CLEFT, MGLKEY_CRIGHT
} MGLspecial;

typedef void (*KeyHandlerFn)(char key);
typedef void (*SpecialHandlerFn)(MGLspecial special_key);
typedef void (*MouseHandlerFn)(GLint x, GLint y, GLbitfield buttons);
typedef void (*IdleFn)(void);

#define MGL_MAX_MODE 80
typedef struct
{
    GLint id;                               /* the id mglCreateContextFromID() takes */
    GLint width, height;
    GLint bit_depth;
    char  mode_name[MGL_MAX_MODE];
} MGLScreenMode;                            /* 96 bytes (tests/abi) */

typedef struct
{
    uint32_t width, height, depth;
    uint32_t pixel_format;
    void    *base_address;
    uint32_t pitch;
} MGLLockInfo;                              /* 24 bytes (tests/abi) */

typedef GLboolean (*MGLScreenModeCallback)(MGLScreenMode *);

typedef struct mgl_quadric GLUquadricObj;
typedef void (*MGLUfuncptr)();

#include "mgl_enums.h"
#include "mgl_enums_ext.h"
#include "mgl_table.h"
#include "mgl_table5.h"

#define MINIGL_DISPATCH_ABI_VERSION     3u
#define MINIGL_BACKEND_FLAG_STUB        (1u << 0)
#define MINIGL_BACKEND_FLAG_CLASSIC     (1u << 1)
#define MINIGL_BACKEND_FLAG_PISTORM3D   (1u << 2)
#define MINIGL_LVO_GETDISPATCH          (-30)
#define MINIGL_OPEN_VERSION             14

#define MGL_SLOT_MEMBER(i, ret, name, args) ret (*name) args;
typedef struct MGLDispatchTable
{
    uint32_t abiVersion;
    uint32_t structSize;
    uint32_t backendFlags;
    uint32_t reserved;
    GLcontext *currentContext;              /* the program reads its context through this, before every call */
    MGL_TABLE(MGL_SLOT_MEMBER)
} MGLDispatchTable;
#undef MGL_SLOT_MEMBER

#define MGL_TABLE_HEADER_BYTES  20u
#define MGL_TABLE_BYTES         (MGL_TABLE_HEADER_BYTES + 4u * MGL_TABLE_SLOTS)    /* 632 with 153 slots */

#define MINIGL_DISPATCH_ABI_VERSION5    5u
#define MINIGL_DISPATCH_ABI_VERSION4    4u
#define MINIGL_OPEN_VERSION5            29
#define MINIGL_OPEN_VERSION4            28
#define MGL_SLOT_MEMBER5(i, ret, name, args) ret (*name) args;
typedef struct MGLDispatchTable5
{
    uint32_t abiVersion;
    uint32_t structSize;
    uint32_t backendFlags;
    uint32_t reserved;
    GLcontext *currentContext;
    MGL_TABLE5_ALL(MGL_SLOT_MEMBER5)
} MGLDispatchTable5;
#undef MGL_SLOT_MEMBER5

#endif
