/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: GPL-3.0-or-later
*/

/* ZZ9000: the blit queue. */

#ifndef ZZ_QUEUE_H
#define ZZ_QUEUE_H

#include <stdint.h>

#define ZZ_QOP_PAYLOAD_BYTES 60u

#ifndef ZZ_QUEUE_DEPTH
#define ZZ_QUEUE_DEPTH 8u
#endif

#ifndef ZZ_BLITQ_SLICE_BYTES
#define ZZ_BLITQ_SLICE_BYTES 32768u
#endif

#define ZZ_QDEP_NONE 0xFFFFFFFFu

struct zz_qop {
    uint8_t  payload[ZZ_QOP_PAYLOAD_BYTES];  /* GFXData header snapshot      */
    uint16_t trig;                           /* trigger op code (0x5A value) */
    uint16_t slice_rows;                     /* rows per slice, >= 1         */
    uint32_t rows_total;                     /* op height in rows            */
    uint32_t rows_done;                      /* resume cursor                */
    uint32_t dep[2];
};

typedef void (*zz_qexec_fn)(void *ud, const struct zz_qop *op,
                            uint32_t row0, uint32_t nrows);

typedef int (*zz_qgate_fn)(void *ud, const struct zz_qop *op, int wait);

struct zz_queue {
    struct zz_qop ops[ZZ_QUEUE_DEPTH];
    uint32_t head;                 /* index of the oldest pending op        */
    uint32_t count;                /* pending ops                           */
    uint8_t  armed;
    zz_qexec_fn exec;
    void *ud;
    zz_qgate_fn gate;
    uint32_t n_enq;
    uint32_t n_gated;
    uint32_t n_gate_waits;         /* ticks that found the head's gate shut   */
    uint32_t n_gate_blocks;        /* gates a synchronous path cleared         */
};

void zz_queue_init(struct zz_queue *q, zz_qexec_fn exec, void *ud);

void zz_queue_set_gate(struct zz_queue *q, zz_qgate_fn gate);

static inline int zz_queue_pending(const struct zz_queue *q)
{
    return q->count != 0;
}

static inline const struct zz_qop *zz_queue_op(const struct zz_queue *q,
                                               uint32_t i)
{
    return &q->ops[(q->head + i) % ZZ_QUEUE_DEPTH];
}

static inline int zz_queue_head_gated(const struct zz_queue *q)
{
    const struct zz_qop *op = &q->ops[q->head];
    return q->count && q->gate && op->rows_done == 0 &&
           (op->dep[0] != ZZ_QDEP_NONE || op->dep[1] != ZZ_QDEP_NONE);
}

void zz_queue_push(struct zz_queue *q, uint16_t trig, const void *payload,
                   uint32_t rows_total, uint32_t slice_rows);

void zz_queue_push_dep(struct zz_queue *q, uint16_t trig, const void *payload,
                       uint32_t rows_total, uint32_t slice_rows,
                       uint32_t dep0, uint32_t dep1);

void zz_queue_tick(struct zz_queue *q);

void zz_queue_drain(struct zz_queue *q);

void zz_queue_run_ready(struct zz_queue *q);

#endif /* ZZ_QUEUE_H */
