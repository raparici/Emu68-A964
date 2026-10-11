/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2 GL core: GLUT. */

#ifndef MGL_GLUT_H
#define MGL_GLUT_H
#include <stdint.h>

typedef struct mgl_glut
{
    uint32_t mode;                          /* glutInitDisplayMode                                               */
    int32_t  init_x, init_y, init_w, init_h;/* glutInitWindowPosition / Size (GLUT's defaults: -1 -1, 300 x 300) */
    uint32_t window;                        /* the one window exists (glutCreateWindow / glutEnterGameMode)      */
    uint32_t game;                          /* ... and it is game mode's full screen                             */
    int32_t  win_x, win_y, win_w, win_h;    /* its place and drawable size                                       */
    int32_t  scr_w, scr_h;                  /* the screen it is on                                               */
    int32_t  gm_w, gm_h, gm_bpp, gm_hz;     /* glutGameModeString's fields (0 = not given)                       */
} mgl_glut;

void mgl_glut_defaults(mgl_glut *g);
int  mgl_glut_mode_possible(uint32_t mode);         /* 1: an RGB(A) mode, single or double, with or without depth */
void mgl_glut_gamemode_parse(mgl_glut *g, const char *s);
int  mgl_glut_gamemode_possible(const mgl_glut *g);
int  mgl_glut_get(const mgl_glut *g, uint32_t state, uint32_t elapsed_ms);
int  mgl_glut_gamemode_get(const mgl_glut *g, uint32_t query);
#endif
