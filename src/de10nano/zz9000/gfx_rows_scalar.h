/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: GPL-3.0-or-later
*/

/* ZZ9000: scalar row primitive bodies. */

#ifndef ZZROW_LINKAGE
#error "gfx_rows_scalar.h is an implementation template - include gfx_rows.h instead"
#endif

ZZROW_LINKAGE void ZZROW_NAME(zzrow_copy)(void *dst, const void *src, uint32_t nbytes)
{
	uint8_t *d = (uint8_t *)dst;
	const uint8_t *s = (const uint8_t *)src;

	while (nbytes--)
		*d++ = *s++;
}

ZZROW_LINKAGE void ZZROW_NAME(zzrow_copy_rev)(void *dst, const void *src, uint32_t nbytes)
{
	uint8_t *d = (uint8_t *)dst + nbytes;
	const uint8_t *s = (const uint8_t *)src + nbytes;

	while (nbytes--)
		*--d = *--s;
}

ZZROW_LINKAGE void ZZROW_NAME(zzrow_move)(void *dst, const void *src, uint32_t nbytes)
{
	/* Exact memmove direction rule (support.c): descending iff dst > src. */
	if ((uintptr_t)dst > (uintptr_t)src)
		ZZROW_NAME(zzrow_copy_rev)(dst, src, nbytes);
	else
		ZZROW_NAME(zzrow_copy)(dst, src, nbytes);
}

ZZROW_LINKAGE void ZZROW_NAME(zzrow_fill8)(void *dst, uint8_t val, uint32_t count)
{
	uint8_t *d = (uint8_t *)dst;

	while (count--)
		*d++ = val;
}

ZZROW_LINKAGE void ZZROW_NAME(zzrow_fill16)(uint16_t *dst, uint16_t val, uint32_t count)
{
	while (count--)
		*dst++ = val;
}

ZZROW_LINKAGE void ZZROW_NAME(zzrow_fill32)(uint32_t *dst, uint32_t val, uint32_t count)
{
	while (count--)
		*dst++ = val;
}

ZZROW_LINKAGE void ZZROW_NAME(zzrow_fill8_masked)(uint8_t *dst, uint8_t fg, uint32_t count, uint8_t mask)
{
	/* SET_FG_PIXEL8_MASK, hoisted: dst = (dst & ~mask) | (fg & mask). */
	uint8_t fgm = (uint8_t)(fg & mask);
	uint8_t keep = (uint8_t)~mask;

	while (count--) {
		*dst = (uint8_t)((*dst & keep) | fgm);
		dst++;
	}
}

ZZROW_LINKAGE void ZZROW_NAME(zzrow_copy_masked)(uint8_t *dst, const uint8_t *src, uint32_t count, uint8_t mask)
{
	uint8_t keep = (uint8_t)~mask;

	while (count--) {
		*dst = (uint8_t)((*dst & keep) | (*src & mask));
		dst++;
		src++;
	}
}

ZZROW_LINKAGE void ZZROW_NAME(zzrow_copy_masked_rev)(uint8_t *dst, const uint8_t *src, uint32_t count, uint8_t mask)
{
	uint8_t keep = (uint8_t)~mask;

	dst += count;
	src += count;
	while (count--) {
		--dst;
		--src;
		*dst = (uint8_t)((*dst & keep) | (*src & mask));
	}
}

ZZROW_LINKAGE void ZZROW_NAME(zzrow_blit_keyed)(uint8_t *dst, const uint8_t *src, uint32_t count, uint8_t key)
{
	while (count--) {
		if (*src != key)
			*dst = *src;
		dst++;
		src++;
	}
}
