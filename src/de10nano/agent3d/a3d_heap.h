/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: the GPU heap. */

#ifndef A3D_HEAP_H
#define A3D_HEAP_H

#include <stdint.h>

#ifdef A3D_HEAP_LIB68K
#define A3D_HEAP_BYTES          (4u << 20)
#define A3D_STAGE_OFF           0u
#define A3D_STAGE_BYTES         (1u << 20)
#define A3D_CHUNK_BYTES         (16u << 10)
#define A3D_CHUNKS              (A3D_STAGE_BYTES / A3D_CHUNK_BYTES)     /* 64 */
#define A3D_AREA_OFF            (1u << 20)
#define A3D_AREA_BYTES          4096u
#define A3D_POOL_OFF            ((1u << 20) + 8192u)
#define A3D_POOL_BYTES          ((3u << 20) - 8192u)
#else
#define A3D_HEAP_BYTES          (28u << 20)
#define A3D_STAGE_OFF           0u
#define A3D_STAGE_BYTES         (3u << 20)
#define A3D_CHUNK_BYTES         (16u << 10)
#define A3D_CHUNKS              (A3D_STAGE_BYTES / A3D_CHUNK_BYTES)     /* 192 */
#define A3D_AREA_OFF            (3u << 20)
#define A3D_AREA_BYTES          (1u << 19)                              /* 512 KB */
#define A3D_POOL_OFF            (4u << 20)
#define A3D_POOL_BYTES          (24u << 20)
#if defined(EMU68_A3D)
#define A3D_HEAP_SPAN0_BYTES    (16u << 20)     /* the firmware: the first physical region's bytes (a3d_heapb.c) */
#endif
#endif
#ifndef A3D_HEAP_SPAN0_BYTES
#define A3D_HEAP_SPAN0_BYTES    A3D_HEAP_BYTES
#endif
#define A3D_POOL_ALIGN          4096u                                   /* RasterIX's texture page */
#define A3D_POOL_BLOCKS         1024u

int a3d_heap_init(uint8_t *va, uint32_t phys, uint32_t bytes);
int a3d_heap_ready(void);
uint8_t *a3d_heap_va_base(void);
uint32_t a3d_heap_phys_base(void);

/* any pointer inside the heap -> its physical address (0 if outside); and back (0 if outside) */
uint32_t a3d_heap_phys(const void *va);
uint8_t *a3d_heap_va(uint32_t phys);

/* staging chunks */
uint8_t *a3d_chunk_get(void);
void a3d_chunk_put(uint8_t *chunk);
uint32_t a3d_chunks_free(void);

/* list areas: 0 = A, 1 = B */
uint8_t *a3d_area(uint32_t i);

/* the pool: 4 KB-aligned blocks, sizes rounded up to 4 KB; NULL when no block fits */
void *a3d_pool_alloc(uint32_t bytes);
void a3d_pool_free(void *p);
uint32_t a3d_pool_free_bytes(void);
uint32_t a3d_pool_largest_free(void);
/* integrity walk for the tests: blocks cover the pool exactly, sorted, no two free neighbours */
int a3d_pool_check(void);

#endif /* A3D_HEAP_H */
