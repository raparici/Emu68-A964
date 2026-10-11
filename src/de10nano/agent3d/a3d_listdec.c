/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: band-list decoder. */

#include "a3d_agent.h"

int a3d_list_next(const uint8_t *list, uint32_t bytes, uint32_t *pos, a3d_cmd *c)
{
    if (*pos + 4u > bytes)
        return *pos == bytes ? 0 : -1;
    const uint32_t op = rix_get32(list + *pos);
    uint32_t n;
    switch (op & RIX_OP_MASK)
    {
    case RIX_OP_NOP:
    case RIX_OP_FRAMEBUFFER:
        n = 0;
        break;
    case RIX_OP_RENDER_CONFIG:
        n = 1;
        break;
    case RIX_OP_FOG_LUT_STREAM:
        n = RIX_FOG_LUT_WORDS;
        break;
    case RIX_OP_TEXTURE_STREAM:
        n = op & RIX_TEX_PAGES_MASK;
        if (n > A3D_MAX_PAGES)
            return -1;
        break;
    case A3D_OP_TRIANGLE_VERTS:
        if (op != A3D_TV_HDR)
            return -1;
        n = A3D_TV_WORDS;
        break;
    case A3D_OP_LINK:
        n = 1;                  /* the next chunk's address: a3d_walk_next() follows it */
        break;
    default:
        return -1;
    }
    if (*pos + 4u + 4u * n > bytes)
        return -1;
    c->op = op;
    c->payload = list + *pos + 4u;
    c->n = n;
    *pos += 4u + 4u * n;
    return 1;
}

const uint8_t *a3d_ls_va(const a3d_listset *ls, uint32_t phys, uint32_t bytes)
{
    for (uint32_t i = 0; i < ls->nregions; i++)
    {
        const a3d_region *r = &ls->region[i];
        if (phys >= r->phys && bytes <= r->bytes && phys - r->phys <= r->bytes - bytes)
            return r->va + (phys - r->phys);
    }
    return 0;
}

int a3d_walk_begin(a3d_walk *w, const a3d_listset *ls, uint32_t band)
{
    memset(w, 0, sizeof(*w));
    w->ls = ls;
    const uint32_t phys = rix_get32(ls->dir + 32u + 8u * band);
    const uint32_t bytes = rix_get32(ls->dir + 36u + 8u * band);
    if (bytes == 0)
        return 0;
    if ((phys & 3u) || (bytes & 3u))
        return -1;              /* the fabric skips such a band and sets ERR bit 2 */
    w->l = a3d_ls_va(ls, phys, bytes);
    if (w->l == 0)
        return -1;
    w->bytes = bytes;
    w->chunks = 1;
    return 1;
}

int a3d_walk_next(a3d_walk *w, a3d_cmd *c)
{
    for (;;)
    {
        if (w->l == 0)
            return 0;
        const int r = a3d_list_next(w->l, w->bytes, &w->pos, c);
        if (r <= 0)
            return r;
        if ((c->op & RIX_OP_MASK) != A3D_OP_LINK)
            return 1;
        const uint32_t nb = c->op & A3D_LINK_BYTES_MASK, np = rix_get32(c->payload);
        if (w->pos != w->bytes || (nb & 3u) || (np & 3u) || w->chunks >= 4096u)
            return -1;
        if (nb == 0)
        {
            w->l = 0;
            return 0;
        }
        w->l = a3d_ls_va(w->ls, np, nb);
        if (w->l == 0)
            return -1;
        w->bytes = nb;
        w->pos = 0;
        w->chunks++;
    }
}
