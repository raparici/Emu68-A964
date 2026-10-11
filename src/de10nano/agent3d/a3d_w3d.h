/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: the Warp3D V4/V5 structures as the engine reads them from guest memory. */

#ifndef A3D_W3D_H
#define A3D_W3D_H

/* ---- W3D_Context (V5 layout, 712 bytes; V4 = the first 636) ---------------------------------------- */
#define W3D_CTX_DRIVER              0u
#define W3D_CTX_GFXDRIVER           4u
#define W3D_CTX_DRIVERTYPE          8u
#define W3D_CTX_REGBASE             12u
#define W3D_CTX_VMEMBASE            16u
#define W3D_CTX_ZBUFFER             20u
#define W3D_CTX_STENCILBUFFER       24u
#define W3D_CTX_STATE               28u
#define W3D_CTX_DRAWREGION          32u
#define W3D_CTX_SUPPORTEDFMT        36u
#define W3D_CTX_FORMAT              40u
#define W3D_CTX_YOFFSET             44u
#define W3D_CTX_BPROW               48u
#define W3D_CTX_WIDTH               52u
#define W3D_CTX_HEIGHT              56u
#define W3D_CTX_DEPTH               60u
#define W3D_CTX_CHUNKY              64u
#define W3D_CTX_DESTALPHA           66u
#define W3D_CTX_ZBUFFERALLOC        68u
#define W3D_CTX_STBUFFERALLOC       70u
#define W3D_CTX_HWLOCKED            72u
#define W3D_CTX_W3DBITMAP           74u
#define W3D_CTX_ZBUFFERLOST         76u
#define W3D_CTX_RESERVED3           78u
#define W3D_CTX_RESTEX              80u     /* struct MinList, 12 bytes                           */
#define W3D_CTX_TEX                 92u
#define W3D_CTX_MAXTEXWIDTH         104u
#define W3D_CTX_MAXTEXHEIGHT        108u
#define W3D_CTX_MAXTEXWIDTHP        112u
#define W3D_CTX_MAXTEXHEIGHTP       116u
#define W3D_CTX_SCISSOR             120u    /* W3D_Scissor: left, top, width, height (ints)       */
#define W3D_CTX_FOG                 136u    /* W3D_Fog: start, end, density, colour r g b         */
#define W3D_FG_START                0u      /* W3D_Fog: floats, 'w-space' (1.0 = the front plane, */
#define W3D_FG_END                  4u      /* 0.0 = the back plane)                              */
#define W3D_FG_DENSITY              8u
#define W3D_FG_COLOR                12u     /* W3D_Color3: r g b floats                            */
#define W3D_FG_SIZE                 24u
#define W3D_CTX_ENVSUPMASK          160u
#define W3D_CTX_QUEUE               164u
#define W3D_CTX_DRAWMEM             168u
#define W3D_CTX_GLOBALTEXENVMODE    172u
#define W3D_CTX_GLOBALTEXENVCOLOR   176u    /* 4 floats                                           */
#define W3D_CTX_DRIVERBASE          192u    /* V4 DriverBase / V5 DriverIFace                     */
#define W3D_CTX_ENABLEMASK          196u
#define W3D_CTX_DISABLEMASK         200u
#define W3D_CTX_CURRENTCHIP         204u
#define W3D_CTX_DRIVERVERSION       208u
#define W3D_CTX_VERTEXPOINTER       212u
#define W3D_CTX_VPSTRIDE            216u
#define W3D_CTX_VPMODE              220u
#define W3D_CTX_VPFLAGS             224u
#define W3D_CTX_TEXCOORDPOINTER     228u    /* [16]                                               */
#define W3D_CTX_TPSTRIDE            292u    /* [16]                                               */
#define W3D_CTX_CURRENTTEX          356u    /* [16]                                               */
#define W3D_CTX_TPVOFFS             420u    /* [16]                                               */
#define W3D_CTX_TPWOFFS             484u    /* [16]                                               */
#define W3D_CTX_TPFLAGS             548u    /* [16]                                               */
#define W3D_CTX_COLORPOINTER        612u
#define W3D_CTX_CPSTRIDE            616u
#define W3D_CTX_CPMODE              620u
#define W3D_CTX_CPFLAGS             624u
#define W3D_CTX_FRONTFACEORDER      628u
#define W3D_CTX_SPECIALBUFFER       632u
#define W3D_CTX_SIZE_V4             636u
#define W3D_CTX_SIZE                712u    /* the stub allocates the V5 size                     */
#define W3D_MAX_TMU                 16u

#define W3D_TEX_RESIDENT            14u     /* W3D_Bool                                           */
#define W3D_TEX_MIPMAP              16u
#define W3D_TEX_DIRTY               18u
#define W3D_TEX_MATCHFMT            20u
#define W3D_TEX_MIPMAPMASK          26u     /* ULONG on a 2-aligned offset                        */
#define W3D_TEX_TEXSOURCE           30u
#define W3D_TEX_MIPMAPS             34u     /* void *[16]                                         */
#define W3D_TEX_TEXFMTSRC           98u
#define W3D_TEX_PALETTE             102u
#define W3D_TEX_TEXDATA             106u
#define W3D_TEX_TEXDEST             110u
#define W3D_TEX_TEXDESTSIZE         114u
#define W3D_TEX_TEXWIDTH            118u
#define W3D_TEX_TEXWIDTHEXP         122u
#define W3D_TEX_TEXHEIGHT           126u
#define W3D_TEX_TEXHEIGHTEXP        130u
#define W3D_TEX_BYTESPERPIX         134u
#define W3D_TEX_BYTESPERROW         138u
#define W3D_TEX_DRIVER              142u
#define W3D_TEX_SIZE                146u

#define W3D_V_X                     0u
#define W3D_V_Y                     4u
#define W3D_V_Z                     8u      /* double                                             */
#define W3D_V_W                     16u
#define W3D_V_U                     20u
#define W3D_V_V                     24u
#define W3D_V_TEX3D                 28u
#define W3D_V_COLOR                 32u     /* r g b a floats                                     */
#define W3D_V_SPEC                  48u
#define W3D_V_L                     60u
#define W3D_V_SIZE                  64u

#define W3D_TRI_V1                  0u      /* W3D_Triangle: 3 vertices, tex, st_pattern          */
#define W3D_TRI_TEX                 192u
#define W3D_TRI_SIZE                200u
#define W3D_TRIV_V1                 0u      /* W3D_TriangleV: 3 vertex pointers, tex              */
#define W3D_TRIV_TEX                12u
#define W3D_TRIV_SIZE               20u
#define W3D_TRIS_COUNT              0u      /* W3D_Triangles / W3D_TrianglesV                     */
#define W3D_TRIS_V                  4u
#define W3D_TRIS_TEX                8u
#define W3D_TRIS_SIZE               16u
#define W3D_POINT_V1                0u      /* W3D_Point                                          */
#define W3D_POINT_TEX               64u
#define W3D_POINT_SIZE_F            68u
#define W3D_POINT_SIZE              72u
#define W3D_LINE_V1                 0u      /* W3D_Line                                           */
#define W3D_LINE_V2                 64u
#define W3D_LINE_TEX                128u
#define W3D_LINE_WIDTH              132u
#define W3D_LINE_SIZE               144u
#define W3D_LINES_COUNT             0u      /* W3D_Lines                                          */
#define W3D_LINES_V                 4u
#define W3D_LINES_TEX               8u
#define W3D_LINES_WIDTH             12u
#define W3D_LINES_SIZE              24u
#define W3D_BM_BPROW                0u      /* W3D_Bitmap                                         */
#define W3D_BM_WIDTH                4u
#define W3D_BM_HEIGHT               8u
#define W3D_BM_FORMAT               12u
#define W3D_BM_DEST                 16u
#define W3D_BM_SIZE                 20u

#define W3D_AUTOTEXMANAGEMENT       (1u << 1)
#define W3D_SYNCHRON                (1u << 2)
#define W3D_INDIRECT                (1u << 3)
#define W3D_GLOBALTEXENV            (1u << 4)
#define W3D_DOUBLEHEIGHT            (1u << 5)
#define W3D_FAST                    (1u << 6)
#define W3D_AUTOCLIP                (1u << 7)
#define W3D_TEXMAPPING              (1u << 8)
#define W3D_PERSPECTIVE             (1u << 9)
#define W3D_GOURAUD                 (1u << 10)
#define W3D_ZBUFFER                 (1u << 11)
#define W3D_ZBUFFERUPDATE           (1u << 12)
#define W3D_BLENDING                (1u << 13)
#define W3D_FOGGING                 (1u << 14)
#define W3D_DITHERING               (1u << 19)
#define W3D_LOGICOP                 (1u << 20)
#define W3D_STENCILBUFFER           (1u << 21)
#define W3D_ALPHATEST               (1u << 22)
#define W3D_SCISSOR                 (1u << 25)
#define W3D_CHROMATEST              (1u << 26)
#define W3D_CULLFACE                (1u << 27)
#define W3D_MULTITEXTURE            (1u << 28)

#define W3D_CW                      0u
#define W3D_CCW                     1u
/* texture formats */
#define W3D_CHUNKY                  1u
#define W3D_A1R5G5B5                2u
#define W3D_R5G6B5                  3u
#define W3D_R8G8B8                  4u
#define W3D_A4R4G4B4                5u
#define W3D_A8R8G8B8                6u
#define W3D_A8                      7u
#define W3D_L8                      8u
#define W3D_L8A8                    9u
#define W3D_I8                      10u
#define W3D_R8G8B8A8                11u
#define W3D_FMT_CLUT                (1u << 0)
#define W3D_FMT_R5G5B5              (1u << 1)
#define W3D_FMT_B5G5R5              (1u << 2)
#define W3D_FMT_R5G5B5PC            (1u << 3)
#define W3D_FMT_B5G5R5PC            (1u << 4)
#define W3D_FMT_R5G6B5              (1u << 5)
#define W3D_FMT_B5G6R5              (1u << 6)
#define W3D_FMT_R5G6B5PC            (1u << 7)
#define W3D_FMT_B5G6R5PC            (1u << 8)
#define W3D_FMT_R8G8B8              (1u << 9)
#define W3D_FMT_B8G8R8              (1u << 10)
#define W3D_FMT_A8R8G8B8            (1u << 11)
#define W3D_FMT_A8B8G8R8            (1u << 12)
#define W3D_FMT_R8G8B8A8            (1u << 13)
#define W3D_FMT_B8G8R8A8            (1u << 14)
/* blend factors */
#define W3D_ZERO                    1u
#define W3D_ONE                     2u
#define W3D_SRC_COLOR               3u
#define W3D_DST_COLOR               4u
#define W3D_ONE_MINUS_SRC_COLOR     5u
#define W3D_ONE_MINUS_DST_COLOR     6u
#define W3D_SRC_ALPHA               7u
#define W3D_ONE_MINUS_SRC_ALPHA     8u
#define W3D_DST_ALPHA               9u
#define W3D_ONE_MINUS_DST_ALPHA     10u
#define W3D_SRC_ALPHA_SATURATE      11u
/* filters, wrap, texenv, compare, logic op */
#define W3D_NEAREST                 1u
#define W3D_LINEAR                  2u
#define W3D_NEAREST_MIP_NEAREST     3u
#define W3D_NEAREST_MIP_LINEAR      4u
#define W3D_LINEAR_MIP_NEAREST      5u
#define W3D_LINEAR_MIP_LINEAR       6u
#define W3D_REPEAT                  1u
#define W3D_CLAMP                   2u
#define W3D_REPLACE                 1u
#define W3D_DECAL                   2u
#define W3D_MODULATE                3u
#define W3D_BLEND                   4u
#define W3D_ADD                     5u
#define W3D_SUB                     6u
#define W3D_OFF                     7u
#define W3D_Z_NEVER                 1u      /* W3D_A_* use the same numbering */
#define W3D_Z_LESS                  2u
#define W3D_Z_GEQUAL                3u
#define W3D_Z_LEQUAL                4u
#define W3D_Z_GREATER               5u
#define W3D_Z_NOTEQUAL              6u
#define W3D_Z_EQUAL                 7u
#define W3D_Z_ALWAYS                8u
#define W3D_LO_COPY                 4u
/* fog modes (W3D_SetFogParams) */
#define W3D_FOG_LINEAR              1u
#define W3D_FOG_EXP                 2u
#define W3D_FOG_EXP_2               3u
#define W3D_FOG_INTERPOLATED        4u
/* arrays */
#define W3D_VERTEX_F_F_F            0u
#define W3D_VERTEX_F_F_D            1u
#define W3D_VERTEX_D_D_D            2u
#define W3D_COLOR_FLOAT             (1u << 30)
#define W3D_COLOR_UBYTE             (2u << 30)
#define W3D_CMODE_RGB               0x01u
#define W3D_CMODE_BGR               0x02u
#define W3D_CMODE_RGBA              0x04u
#define W3D_CMODE_ARGB              0x08u
#define W3D_CMODE_BGRA              0x10u
#define W3D_PRIMITIVE_TRIANGLES     0u
#define W3D_PRIMITIVE_TRIFAN        1u
#define W3D_PRIMITIVE_TRISTRIP      2u
#define W3D_PRIMITIVE_POINTS        3u
#define W3D_PRIMITIVE_LINES         4u
#define W3D_PRIMITIVE_LINELOOP      5u
#define W3D_PRIMITIVE_LINESTRIP     6u
#define W3D_INDEX_UBYTE             0u
#define W3D_INDEX_UWORD             1u
#define W3D_INDEX_ULONG             2u
#define W3D_TEXCOORD_NORMALIZED     (1u << 0)

#define W3D_SUCCESS                 0
#define W3D_BUSY                    (-1)
#define W3D_ILLEGALINPUT            (-2)
#define W3D_NOMEMORY                (-3)
#define W3D_NODRIVER                (-4)
#define W3D_NOTEXTURE               (-5)
#define W3D_NOTVISIBLE              (-9)
#define W3D_UNSUPPORTEDTEXENV       (-11)
#define W3D_UNSUPPORTEDBLEND        (-15)
#define W3D_UNSUPPORTEDSTATE        (-17)
#define W3D_UNSUPPORTEDFMT          (-18)
#define W3D_UNSUPPORTEDTEXSIZE      (-19)
#define W3D_ILLEGALBITMAP           (-22)
#define W3D_NOZBUFFER               (-23)
#define W3D_NOPALETTE               (-24)
#define W3D_QUEUEFAILED             (-27)
#define W3D_UNSUPPORTEDTEXFMT       (-28)

/* ---- identity (V4 numbering - the one 68k programs are compiled against) --------------------------- */
#define W3D_CHIP_UNKNOWN            1u
#define W3D_CHIP_AVENGER_BE         6u      /* "a Voodoo 3, big-endian"; V5 renumbered it to 8 */
#define W3D_DRIVER_3DHW             (1u << 2)

#endif /* A3D_W3D_H */
