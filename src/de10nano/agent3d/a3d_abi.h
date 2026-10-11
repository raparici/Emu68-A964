/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: the ONCALL interface between the guest's Warp3D / MiniGL libraries and the 3D engine. */

#ifndef A3D_ABI_H
#define A3D_ABI_H

#define A3D_ABI_VERSION         1       /* bumped when anything below changes meaning            */
#define A3D_ONCALL_W1           0xFD3C
#define A3D_ONCALL_W2           0x00E1

#define A3D_ID_FIRST            0x0300
#define A3D_ID_LAST             0x033F
#define A3D_HELLO               0x0300
#define A3D_CTX_CREATE          0x0301
#define A3D_CTX_DESTROY         0x0302  /* a0 -> d0                                                      */
#define A3D_TARGET              0x0303
#define A3D_TEX_ALLOC           0x0304  /* a0, a1 = W3D_Texture *, d0 = A3D_TA_* flags -> d0              */
#define A3D_TEX_UPDATE          0x0305
#define A3D_TEX_FREE            0x0306  /* a0, a1 = W3D_Texture * (d0 = 0) or all (d0 = 1) -> d0         */
#define A3D_DRAW                0x0307  /* a0, d0 = kind (A3D_DK_*), a1 = structure or block -> d0      */
#define A3D_CLEAR               0x0308  /* a0, d0 = 1 colour (d1 = ARGB) / 2 Z (a1 = W3D_Double *)      */
#define A3D_FLUSH               0x0309  /* a0 -> d0, d1 = fence                                          */
#define A3D_WAIT                0x030A  /* a0, d0 = fence (0 = all) -> d0 = 0 done / 1 not yet           */
#define A3D_FRAME               0x030B  /* a0 -> d0, d1 = fence (end of frame)                           */
#define A3D_ZSPAN               0x030C
#define A3D_STATS               0x030D
#define A3D_UNLOCK              0x030E
#define A3D_MGL_TRIS            0x0310
#define A3D_MGLC_HELLO          0x0311  /* a0 -> d0 = 0, d1 = A3D_MGLC_VERSION (any other answer: not there)      */
#define A3D_MGLC_CTX            0x0312
#define A3D_MGLC_SUBMIT         0x0313
#define A3D_MGLC_QUERY          0x0314
#define A3D_MGLC_VERSION        1u
#define A3D_MGLC_F_SWAP         0x01u   /* the buffer switch follows (the kept unit-1 pass drawn)                     */
#define A3D_MGLC_F_SYNC         0x02u   /* the caller waits for the drawing                                          */
#define A3D_MGLC_MAXWORDS       16384u

/* HELLO capability word (d1) */
#define A3D_CAP_BACKEND_MASK    0x0000000F  /* A3D_BE_*                                              */
#define A3D_CAP_UNITS_SHIFT     4           /* fabric raster units (0 for NULL/SOFT)                 */
#define A3D_CAP_HEAP_MB_SHIFT   8           /* bits 15:8 = heap size in MB                           */
#define A3D_CAP_TRAPPATH        0x00010000  /* this call arrived through the trap path               */
#define A3D_CAP_MGL             0x00020000
#define A3D_BE_NULL             0
#define A3D_BE_SOFT             1
#define A3D_BE_FABRIC           2

/* DRAW kinds (d0) and what a1 points at */
#define A3D_DK_TRIANGLE         1   /* W3D_Triangle *                                         */
#define A3D_DK_TRIFAN           2   /* W3D_Triangles *                                        */
#define A3D_DK_TRISTRIP         3   /* W3D_Triangles *                                        */
#define A3D_DK_TRIANGLEV        4   /* W3D_TriangleV *                                        */
#define A3D_DK_TRIFANV          5   /* W3D_TrianglesV *                                       */
#define A3D_DK_TRISTRIPV        6   /* W3D_TrianglesV *                                       */
#define A3D_DK_LINE             7   /* W3D_Line *                                             */
#define A3D_DK_POINT            8   /* W3D_Point *                                            */
#define A3D_DK_LINESTRIP        9   /* W3D_Lines *                                            */
#define A3D_DK_LINELOOP         10  /* W3D_Lines *                                            */
#define A3D_DK_ARRAY            11
#define A3D_DK_ELEMENTS         12

#define A3D_TA_MIPMAP           0x00000001

#define A3D_TU_WHOLE            0u      /* W3D_UpdateTexImage: level `level` from `image`, the whole level    */
#define A3D_TU_RECT             1u
#define A3D_TUB_TEX             0       /* W3D_Texture *                                                     */
#define A3D_TUB_IMAGE           4       /* the new texels (the texture's source format)                      */
#define A3D_TUB_LEVEL           8
#define A3D_TUB_X               12
#define A3D_TUB_Y               16
#define A3D_TUB_W               20
#define A3D_TUB_H               24
#define A3D_TUB_BPR             28
#define A3D_TUB_SIZE            32

/* ZSPAN ops (d0) */
#define A3D_ZS_READPIXEL        0
#define A3D_ZS_READSPAN         1
#define A3D_ZS_WRITEPIXEL       2
#define A3D_ZS_WRITESPAN        3

#define A3D_PRIV_MAGIC          0x41334431  /* 'A3D1'                                      */
#define A3D_PRIV_SIZE           256
#define A3D_P_MAGIC             0
#define A3D_P_VERSION           4
#define A3D_P_HANDLE            8
#define A3D_P_DIRTY             12
#define A3D_P_BLEND_SRC         16  /* W3D_* factor                                                      */
#define A3D_P_BLEND_DST         20
#define A3D_P_ALPHA_FUNC        24  /* W3D_A_*                                                           */
#define A3D_P_ALPHA_REF         28  /* float bits                                                        */
#define A3D_P_ZCOMPARE          32  /* W3D_Z_*                                                           */
#define A3D_P_LOGICOP           36  /* W3D_LO_*                                                          */
#define A3D_P_COLORMASK         40  /* bit 0 r, 1 g, 2 b, 3 a                                            */
#define A3D_P_CURCOLOR          44  /* 4 floats r g b a (W3D_SetCurrentColor)                            */
#define A3D_P_FOGMODE           60
#define A3D_P_TBASE             64  /* target: first pixel of the bitmap (guest address)                 */
#define A3D_P_TBPROW            68
#define A3D_P_TWIDTH            72
#define A3D_P_THEIGHT           76
#define A3D_P_TFORMAT           80  /* W3D_FMT_*                                                         */
#define A3D_P_TYOFFSET          84
#define A3D_P_TFLAGS            88  /* A3D_TF_*                                                          */
#define A3D_P_POLICY            92  /* A3D_POL_* from ENVARC:BigMig/3D.prefs                             */
#define A3D_P_PENDING           96
#define A3D_P_FENCE_SUB         100
#define A3D_P_FENCE_DONE        104
#define A3D_P_FRAMES            108
#define A3D_P_ERRORS            112
#define A3D_P_NAME              128 /* stub: the program name the policy matched (32 bytes, NUL-padded)  */
#define A3D_P_SCRATCH           160
#define A3D_P_ZCLEAR            192 /* stub: ClearZBuffer's W3D_Double (8 bytes, 8-aligned in the block)    */
#define A3D_P_STUB              200

#define A3D_TF_VALID            0x00000001  /* the stub resolved the target                            */
#define A3D_TF_DOUBLEHEIGHT     0x00000002
#define A3D_TF_W3DBITMAP        0x00000004  /* raw W3D_Bitmap, no Picasso96 lock                       */
#define A3D_POL_UNLOCK_WAIT     0x00000001
#define A3D_POL_ID_UNKNOWN      0x00000002  /* report W3D_CHIP_UNKNOWN (open question 1)               */

/* ---- the per-texture private block: W3D_Texture->driver ------------------------------------------- */
#define A3D_TPRIV_MAGIC         0x41335431  /* 'A3T1' */
#define A3D_TPRIV_SIZE          64
#define A3D_T_MAGIC             0
#define A3D_T_HANDLE            4
#define A3D_T_GEN               8
#define A3D_T_MINFILTER         12  /* stub: W3D_NEAREST..   */
#define A3D_T_MAGFILTER         16
#define A3D_T_WRAP_S            20  /* W3D_REPEAT / W3D_CLAMP */
#define A3D_T_WRAP_T            24
#define A3D_T_ENVMODE           28
#define A3D_T_ENVCOLOR          32  /* 4 floats */
#define A3D_T_CHROMA_LO         48
#define A3D_T_CHROMA_HI         52
#define A3D_T_CHROMA_MODE       56

#endif /* A3D_ABI_H */
