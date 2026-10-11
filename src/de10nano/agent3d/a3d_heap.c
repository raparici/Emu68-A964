/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: the GPU heap. */

#include <string.h>
#include "a3d_heap.h"

typedef struct a3d_blk
{
    uint32_t off;       /* from the pool start */
    uint32_t size;
    uint32_t used;
} a3d_blk;

static struct
{
    uint8_t *va;
    uint32_t phys;
    uint32_t bytes;
    uint16_t free_chunk[A3D_CHUNKS];
    uint32_t nfree;
    a3d_blk blk[A3D_POOL_BLOCKS];
    uint32_t nblk;
} H;

int a3d_heap_init(uint8_t *va, uint32_t phys, uint32_t bytes)
{
    memset(&H, 0, sizeof(H));
    if (va == 0 || bytes < A3D_HEAP_BYTES || (phys & (A3D_POOL_ALIGN - 1u)) != 0
        || (((uintptr_t)va) & (A3D_POOL_ALIGN - 1u)) != 0)
        return 0;
    H.va = va;
    H.phys = phys;
    H.bytes = A3D_HEAP_BYTES;
    for (uint32_t i = 0; i < A3D_CHUNKS; i++)
        H.free_chunk[i] = (uint16_t)(A3D_CHUNKS - 1u - i);   /* chunk 0 is handed out first */
    H.nfree = A3D_CHUNKS;
    H.blk[0].off = 0;
    H.blk[0].size = A3D_POOL_BYTES;
    H.blk[0].used = 0;
    H.nblk = 1;
    return 1;
}

int a3d_heap_ready(void)
{
    return H.va != 0;
}

uint8_t *a3d_heap_va_base(void)
{
    return H.va;
}

uint32_t a3d_heap_phys_base(void)
{
    return H.phys;
}

uint32_t a3d_heap_phys(const void *va)
{
    const uint8_t *p = (const uint8_t *)va;
    if (H.va == 0 || p < H.va || p >= H.va + H.bytes)
        return 0;
    return H.phys + (uint32_t)(p - H.va);
}

uint8_t *a3d_heap_va(uint32_t phys)
{
    if (H.va == 0 || phys < H.phys || phys - H.phys >= H.bytes)
        return 0;
    return H.va + (phys - H.phys);
}

/* ---- staging chunks ------------------------------------------------------------------------------- */

uint8_t *a3d_chunk_get(void)
{
    if (H.nfree == 0)
        return 0;
    const uint32_t c = H.free_chunk[--H.nfree];
    return H.va + A3D_STAGE_OFF + c * A3D_CHUNK_BYTES;
}

void a3d_chunk_put(uint8_t *chunk)
{
    if (chunk == 0 || H.va == 0 || chunk < H.va + A3D_STAGE_OFF)
        return;
    const uint32_t off = (uint32_t)(chunk - (H.va + A3D_STAGE_OFF));
    if (off >= A3D_STAGE_BYTES || (off % A3D_CHUNK_BYTES) != 0 || H.nfree >= A3D_CHUNKS)
        return;
    H.free_chunk[H.nfree++] = (uint16_t)(off / A3D_CHUNK_BYTES);
}

uint32_t a3d_chunks_free(void)
{
    return H.nfree;
}

uint8_t *a3d_area(uint32_t i)
{
    return H.va ? H.va + A3D_AREA_OFF + (i & 1u) * A3D_AREA_BYTES : 0;
}

/* ---- the pool: first fit over an address-sorted block table ---------------------------------------- */

void *a3d_pool_alloc(uint32_t bytes)
{
    if (H.va == 0 || bytes == 0 || bytes > A3D_POOL_BYTES)
        return 0;
    const uint32_t size = (bytes + A3D_POOL_ALIGN - 1u) & ~(A3D_POOL_ALIGN - 1u);
    for (uint32_t i = 0; i < H.nblk; i++)
    {
        a3d_blk *b = &H.blk[i];
        if (b->used || b->size < size)
            continue;
        if (b->size > size)
        {
            if (H.nblk >= A3D_POOL_BLOCKS)
                return 0;               /* the table is full: refuse rather than split */
            memmove(&H.blk[i + 2u], &H.blk[i + 1u], (H.nblk - i - 1u) * sizeof(a3d_blk));
            H.blk[i + 1u].off = b->off + size;
            H.blk[i + 1u].size = b->size - size;
            H.blk[i + 1u].used = 0;
            H.nblk++;
            b->size = size;
        }
        b->used = 1;
        return H.va + A3D_POOL_OFF + b->off;
    }
    return 0;
}

void a3d_pool_free(void *p)
{
    const uint8_t *q = (const uint8_t *)p;
    if (H.va == 0 || q < H.va + A3D_POOL_OFF || q >= H.va + A3D_POOL_OFF + A3D_POOL_BYTES)
        return;
    const uint32_t off = (uint32_t)(q - (H.va + A3D_POOL_OFF));
    for (uint32_t i = 0; i < H.nblk; i++)
    {
        if (H.blk[i].off != off || !H.blk[i].used)
            continue;
        H.blk[i].used = 0;
        if (i + 1u < H.nblk && !H.blk[i + 1u].used)          /* merge with the next */
        {
            H.blk[i].size += H.blk[i + 1u].size;
            memmove(&H.blk[i + 1u], &H.blk[i + 2u], (H.nblk - i - 2u) * sizeof(a3d_blk));
            H.nblk--;
        }
        if (i > 0 && !H.blk[i - 1u].used)                      /* merge with the previous */
        {
            H.blk[i - 1u].size += H.blk[i].size;
            memmove(&H.blk[i], &H.blk[i + 1u], (H.nblk - i - 1u) * sizeof(a3d_blk));
            H.nblk--;
        }
        return;
    }
}

uint32_t a3d_pool_free_bytes(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < H.nblk; i++)
        if (!H.blk[i].used)
            n += H.blk[i].size;
    return n;
}

uint32_t a3d_pool_largest_free(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < H.nblk; i++)
        if (!H.blk[i].used && H.blk[i].size > n)
            n = H.blk[i].size;
    return n;
}

int a3d_pool_check(void)
{
    uint32_t off = 0;
    for (uint32_t i = 0; i < H.nblk; i++)
    {
        if (H.blk[i].off != off || H.blk[i].size == 0 || (H.blk[i].size % A3D_POOL_ALIGN) != 0)
            return 0;
        if (i > 0 && !H.blk[i].used && !H.blk[i - 1u].used)
            return 0;
        off += H.blk[i].size;
    }
    return off == A3D_POOL_BYTES;
}
