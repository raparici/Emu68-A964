/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: GPL-3.0-or-later
*/

/* ZZ9000: row copy and fill primitives. */

#ifndef GFX_ROWS_H
#define GFX_ROWS_H

#include <stdint.h>

#if defined(GFX_NEON_ROWS) && defined(__arm__) && !defined(__aarch64__)
#define GFX_ROWS_NEON_ACTIVE 1
#else
#define GFX_ROWS_NEON_ACTIVE 0
#endif

#if GFX_ROWS_NEON_ACTIVE

void zzrow_copy(void *dst, const void *src, uint32_t nbytes);
void zzrow_copy_rev(void *dst, const void *src, uint32_t nbytes);
void zzrow_move(void *dst, const void *src, uint32_t nbytes);
void zzrow_fill8(void *dst, uint8_t val, uint32_t count);
void zzrow_fill16(uint16_t *dst, uint16_t val, uint32_t count);
void zzrow_fill32(uint32_t *dst, uint32_t val, uint32_t count);
void zzrow_fill8_masked(uint8_t *dst, uint8_t fg, uint32_t count, uint8_t mask);
void zzrow_copy_masked(uint8_t *dst, const uint8_t *src, uint32_t count, uint8_t mask);
void zzrow_copy_masked_rev(uint8_t *dst, const uint8_t *src, uint32_t count, uint8_t mask);
void zzrow_blit_keyed(uint8_t *dst, const uint8_t *src, uint32_t count, uint8_t key);

void zzrow_copy_scalar(void *dst, const void *src, uint32_t nbytes);
void zzrow_copy_rev_scalar(void *dst, const void *src, uint32_t nbytes);
void zzrow_move_scalar(void *dst, const void *src, uint32_t nbytes);
void zzrow_fill8_scalar(void *dst, uint8_t val, uint32_t count);
void zzrow_fill16_scalar(uint16_t *dst, uint16_t val, uint32_t count);
void zzrow_fill32_scalar(uint32_t *dst, uint32_t val, uint32_t count);
void zzrow_fill8_masked_scalar(uint8_t *dst, uint8_t fg, uint32_t count, uint8_t mask);
void zzrow_copy_masked_scalar(uint8_t *dst, const uint8_t *src, uint32_t count, uint8_t mask);
void zzrow_copy_masked_rev_scalar(uint8_t *dst, const uint8_t *src, uint32_t count, uint8_t mask);
void zzrow_blit_keyed_scalar(uint8_t *dst, const uint8_t *src, uint32_t count, uint8_t key);

#else /* !GFX_ROWS_NEON_ACTIVE - scalar bodies inline, no extra objects */

#define ZZROW_LINKAGE static inline
#define ZZROW_NAME(n) n
#include "gfx_rows_scalar.h"
#undef ZZROW_LINKAGE
#undef ZZROW_NAME

#endif /* GFX_ROWS_NEON_ACTIVE */

#endif /* GFX_ROWS_H */
