/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/* BigMig 3Dx2: how the engine reads and writes guest (m68k) memory. */

#ifndef A3D_GUEST_H
#define A3D_GUEST_H

#include <stdint.h>
#include <string.h>

#define A3D_GR_READ     0x01u   /* structures, arrays, indices, texture images                   */
#define A3D_GR_WRITE    0x02u
#define A3D_GR_TARGET   0x04u   /* a render target may live there                                */
#define A3D_GR_MAX      8u

typedef struct a3d_grange
{
    uint32_t lo, hi;            /* [lo, hi) guest addresses              */
    uint8_t *host;              /* where lo is on this side              */
    uint32_t flags;
    uint32_t phys;              /* the physical address of lo (the fabric's view; 0 = not reachable) */
} a3d_grange;

void a3d_guest_reset(void);
/* register [lo, lo + size); returns 0 when the table is full or the range is empty or wraps */
int a3d_guest_add(uint32_t lo, uint32_t size, uint8_t *host, uint32_t phys, uint32_t flags);

extern const a3d_grange *g_a3d_glast;           /* the last range a walk found; NULL = no cache (walk) */
const a3d_grange *a3d_g_find(uint32_t addr, uint32_t len);

static inline __attribute__((always_inline)) const a3d_grange *a3d_g_range(uint32_t addr, uint32_t len)
{
    const a3d_grange *r = g_a3d_glast;
    if (r)
    {
        const uint32_t last = addr + (len ? len : 1u) - 1u;
        if (addr >= r->lo && last >= addr && last < r->hi)
            return r;
    }
    return a3d_g_find(addr, len);
}

/* 1 when [addr, addr + len) lies inside ONE range that grants every flag in `need`; len 0 counts as 1 */
static inline __attribute__((always_inline)) int a3d_g_ok(uint32_t addr, uint32_t len, uint32_t need)
{
    const a3d_grange *r = a3d_g_range(addr, len);
    return r != 0 && (r->flags & need) == need;
}

/* the host pointer of a checked address (NULL when no range holds it) */
static inline __attribute__((always_inline)) uint8_t *a3d_g_ptr(uint32_t addr)
{
    const a3d_grange *r = a3d_g_range(addr, 1u);
    return r ? r->host + (addr - r->lo) : 0;
}
/* the physical address of a checked address (0 when its range has no physical view) */
uint32_t a3d_g_phys(uint32_t addr);
/* [addr, addr + rows * bprow) for a render target; also requires the range to be physically linear */
int a3d_g_target_ok(uint32_t addr, uint32_t bprow, uint32_t rows);
const a3d_grange *a3d_guest_ranges(uint32_t *n);

static inline uint32_t a3d_be32(const uint8_t *p)
{
    uint32_t v;
    __builtin_memcpy(&v, p, 4);
#if !(defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__))
    v = __builtin_bswap32(v);
#endif
    return v;
}

static inline uint16_t a3d_be16(const uint8_t *p)
{
    uint16_t v;
    __builtin_memcpy(&v, p, 2);
#if !(defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__))
    v = __builtin_bswap16(v);
#endif
    return v;
}

static inline void a3d_wbe32(uint8_t *p, uint32_t v)
{
#if !(defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__))
    v = __builtin_bswap32(v);
#endif
    __builtin_memcpy(p, &v, 4);
}

static inline void a3d_wbe16(uint8_t *p, uint16_t v)
{
#if !(defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__))
    v = __builtin_bswap16(v);
#endif
    __builtin_memcpy(p, &v, 2);
}

static inline float a3d_bef32(const uint8_t *p)
{
    const uint32_t u = a3d_be32(p);
    float f;
    __builtin_memcpy(&f, &u, 4);
    return f;
}

static inline double a3d_bef64(const uint8_t *p)
{
    const uint64_t u = ((uint64_t)a3d_be32(p) << 32) | a3d_be32(p + 4);
    double d;
    __builtin_memcpy(&d, &u, 8);
    return d;
}

static inline void a3d_wbef64(uint8_t *p, double d)
{
    uint64_t u;
    __builtin_memcpy(&u, &d, 8);
    a3d_wbe32(p, (uint32_t)(u >> 32));
    a3d_wbe32(p + 4, (uint32_t)u);
}

/* little-endian 16-bit (the fabric's texel and depth order) */
static inline void a3d_wle16(uint8_t *p, uint16_t v)
{
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    v = __builtin_bswap16(v);
#endif
    __builtin_memcpy(p, &v, 2);
}

static inline uint16_t a3d_le16(const uint8_t *p)
{
    uint16_t v;
    __builtin_memcpy(&v, p, 2);
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    v = __builtin_bswap16(v);
#endif
    return v;
}

static inline int a3d_g32(uint32_t addr, uint32_t *out)
{
    if (!a3d_g_ok(addr, 4, A3D_GR_READ))
        return 0;
    *out = a3d_be32(a3d_g_ptr(addr));
    return 1;
}

#endif /* A3D_GUEST_H */
