/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2 GL core: GLUT's event and timer logic. */

#include "mgl_core.h"
#include "mgl_abi.h"
#include "mgl_gl5.h"
#include "mgl_glut.h"

void mgl_glut_defaults(mgl_glut *g)
{
    g->mode = 0;                            /* GLUT_RGBA | GLUT_SINGLE: 0 */
    g->init_x = g->init_y = -1;
    g->init_w = g->init_h = 300;
    g->window = g->game = 0;
    g->win_x = g->win_y = g->win_w = g->win_h = 0;
    g->scr_w = g->scr_h = 0;
    g->gm_w = g->gm_h = g->gm_bpp = g->gm_hz = 0;
}

int mgl_glut_mode_possible(uint32_t mode)
{
    const uint32_t no = GLUT5_INDEX | GLUT5_ACCUM | GLUT5_STENCIL | GLUT5_MULTISAMPLE | GLUT5_STEREO | GLUT5_LUMINANCE;
    return (mode & no) == 0u;
}

static const char *num(const char *s, int32_t *v)       /* digits -> *v (none: *v untouched, s as it was) */
{
    int32_t n = 0;
    const char *p = s;
    while (*p >= '0' && *p <= '9' && n < 100000)
        n = n * 10 + (*p++ - '0');
    if (p != s)
        *v = n;
    return p;
}

void mgl_glut_gamemode_parse(mgl_glut *g, const char *s)
{
    g->gm_w = g->gm_h = g->gm_bpp = g->gm_hz = 0;
    if (!s)
        return;
    while (*s == ' ')
        s++;
    int32_t w = 0, h = 0;
    const char *p = num(s, &w);
    if (p != s && (*p == 'x' || *p == 'X'))
    {
        const char *q = num(p + 1, &h);
        if (q != p + 1)
        {
            g->gm_w = w;
            g->gm_h = h;
            p = q;
        }
    }
    else
        p = s;                              /* no "WxH" (a lone number is not a size) */
    if (*p == ':')
        p = num(p + 1, &g->gm_bpp);
    if (*p == '@')
        p = num(p + 1, &g->gm_hz);
    if (*p != 0 && *p != ' ')               /* anything else: GLUT's rule -- the string is not understood, nothing set */
        g->gm_w = g->gm_h = g->gm_bpp = g->gm_hz = 0;
}

int mgl_glut_gamemode_possible(const mgl_glut *g)
{
    return (g->gm_bpp == 0 || g->gm_bpp == 15 || g->gm_bpp == 16) && g->gm_w >= 0 && g->gm_h >= 0;
}

int mgl_glut_get(const mgl_glut *g, uint32_t state, uint32_t elapsed_ms)
{
    switch (state)
    {
    case GLUT5_WINDOW_X:                return g->window ? g->win_x : 0;
    case GLUT5_WINDOW_Y:                return g->window ? g->win_y : 0;
    case GLUT5_WINDOW_WIDTH:            return g->window ? g->win_w : 0;
    case GLUT5_WINDOW_HEIGHT:           return g->window ? g->win_h : 0;
    case GLUT5_WINDOW_BUFFER_SIZE:      return 16;
    case GLUT5_WINDOW_STENCIL_SIZE:     return 0;
    case GLUT5_WINDOW_DEPTH_SIZE:       return 16;
    case GLUT5_WINDOW_RED_SIZE:         return 5;
    case GLUT5_WINDOW_GREEN_SIZE:       return 6;
    case GLUT5_WINDOW_BLUE_SIZE:        return 5;
    case GLUT5_WINDOW_ALPHA_SIZE:       return 0;
    case GLUT5_WINDOW_ACCUM_RED_SIZE:
    case GLUT5_WINDOW_ACCUM_GREEN_SIZE:
    case GLUT5_WINDOW_ACCUM_BLUE_SIZE:
    case GLUT5_WINDOW_ACCUM_ALPHA_SIZE: return 0;
    case GLUT5_WINDOW_DOUBLEBUFFER:     return 1;
    case GLUT5_WINDOW_RGBA:             return 1;
    case GLUT5_WINDOW_PARENT:           return 0;
    case GLUT5_WINDOW_NUM_CHILDREN:     return 0;
    case GLUT5_WINDOW_COLORMAP_SIZE:    return 0;
    case GLUT5_WINDOW_NUM_SAMPLES:      return 0;
    case GLUT5_WINDOW_STEREO:           return 0;
    case GLUT5_WINDOW_CURSOR:           return GLUT5_CURSOR_INHERIT;    /* no glutSetCursor here: GLUT's initial value */
    case GLUT5_WINDOW_FORMAT_ID:        return 0;
    case GLUT5_SCREEN_WIDTH:            return g->scr_w;
    case GLUT5_SCREEN_HEIGHT:           return g->scr_h;
    case GLUT5_SCREEN_WIDTH_MM:
    case GLUT5_SCREEN_HEIGHT_MM:        return 0;           /* unknown: the screen's size in mm is nowhere */
    case GLUT5_MENU_NUM_ITEMS:          return 0;           /* no menus */
    case GLUT5_DISPLAY_MODE_POSSIBLE:   return mgl_glut_mode_possible(g->mode);
    case GLUT5_INIT_WINDOW_X:           return g->init_x;
    case GLUT5_INIT_WINDOW_Y:           return g->init_y;
    case GLUT5_INIT_WINDOW_WIDTH:       return g->init_w;
    case GLUT5_INIT_WINDOW_HEIGHT:      return g->init_h;
    case GLUT5_INIT_DISPLAY_MODE:       return (int)g->mode;
    case GLUT5_ELAPSED_TIME:            return (int)elapsed_ms;
    default:                            return -1;
    }
}

int mgl_glut_gamemode_get(const mgl_glut *g, uint32_t query)
{
    switch (query)
    {
    case GLUT5_GAME_MODE_ACTIVE:          return g->game ? 1 : 0;
    case GLUT5_GAME_MODE_POSSIBLE:        return mgl_glut_gamemode_possible(g);
    case GLUT5_GAME_MODE_WIDTH:           return g->game ? g->win_w : g->gm_w;
    case GLUT5_GAME_MODE_HEIGHT:          return g->game ? g->win_h : g->gm_h;
    case GLUT5_GAME_MODE_PIXEL_DEPTH:     return g->game ? 16 : g->gm_bpp;
    case GLUT5_GAME_MODE_REFRESH_RATE:    return g->gm_hz;
    case GLUT5_GAME_MODE_DISPLAY_CHANGED: return g->game ? 1 : 0;
    default:                              return -1;
    }
}
