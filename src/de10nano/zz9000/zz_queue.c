/*
    Copyright © 2026 Ruben Aparicio (@raparici)

    SPDX-License-Identifier: GPL-3.0-or-later
*/

/* ZZ9000: the blit queue. */

#include <string.h>
#include "zz_queue.h"

void zz_queue_init(struct zz_queue *q, zz_qexec_fn exec, void *ud)
{
    memset(q, 0, sizeof(*q));
    q->exec = exec;
    q->ud = ud;
}

void zz_queue_set_gate(struct zz_queue *q, zz_qgate_fn gate)
{
    q->gate = gate;
}

static int zz_qop_gated(const struct zz_queue *q, const struct zz_qop *op)
{
    return q->gate && op->rows_done == 0 &&
           (op->dep[0] != ZZ_QDEP_NONE || op->dep[1] != ZZ_QDEP_NONE);
}

static void zz_qop_ungate(struct zz_qop *op)
{
    op->dep[0] = op->dep[1] = ZZ_QDEP_NONE;
}

static void zz_queue_finish_head(struct zz_queue *q)
{
    struct zz_qop *op = &q->ops[q->head];
    if (zz_qop_gated(q, op)) {
        q->gate(q->ud, op, 1);
        q->n_gate_blocks++;
        zz_qop_ungate(op);
    }
    if (op->rows_done < op->rows_total)
        q->exec(q->ud, op, op->rows_done, op->rows_total - op->rows_done);
    q->head = (q->head + 1u) % ZZ_QUEUE_DEPTH;
    q->count--;
}

void zz_queue_push(struct zz_queue *q, uint16_t trig, const void *payload,
                   uint32_t rows_total, uint32_t slice_rows)
{
    zz_queue_push_dep(q, trig, payload, rows_total, slice_rows, ZZ_QDEP_NONE,
                      ZZ_QDEP_NONE);
}

void zz_queue_push_dep(struct zz_queue *q, uint16_t trig, const void *payload,
                       uint32_t rows_total, uint32_t slice_rows,
                       uint32_t dep0, uint32_t dep1)
{
    if (!rows_total)
        return;
    if (q->count == ZZ_QUEUE_DEPTH) {
        zz_queue_finish_head(q);
    }
    {
        struct zz_qop *op = &q->ops[(q->head + q->count) % ZZ_QUEUE_DEPTH];
        memcpy(op->payload, payload, ZZ_QOP_PAYLOAD_BYTES);
        op->trig = trig;
        op->slice_rows = (uint16_t)(slice_rows ? (slice_rows > 0xFFFFu
                                                  ? 0xFFFFu : slice_rows)
                                               : 1u);
        op->rows_total = rows_total;
        op->rows_done = 0;
        op->dep[0] = q->gate ? dep0 : ZZ_QDEP_NONE;
        op->dep[1] = q->gate ? dep1 : ZZ_QDEP_NONE;
        if (op->dep[0] != ZZ_QDEP_NONE || op->dep[1] != ZZ_QDEP_NONE)
            q->n_gated++;
        q->count++;
        q->n_enq++;
    }
}

void zz_queue_tick(struct zz_queue *q)
{
    struct zz_qop *op;
    uint32_t n;

    if (!q->count)
        return;
    op = &q->ops[q->head];
    if (zz_qop_gated(q, op)) {
        if (!q->gate(q->ud, op, 0)) {
            q->n_gate_waits++;
            return;                  /* not yet: the op (and FIFO) wait     */
        }
        zz_qop_ungate(op);
    }
    n = op->rows_total - op->rows_done;
    if (n > op->slice_rows)
        n = op->slice_rows;
    q->exec(q->ud, op, op->rows_done, n);
    op->rows_done += n;
    if (op->rows_done == op->rows_total) {
        q->head = (q->head + 1u) % ZZ_QUEUE_DEPTH;
        q->count--;
    }
}

void zz_queue_drain(struct zz_queue *q)
{
    if (!q->count)
        return;
    while (q->count)
        zz_queue_finish_head(q);
}

void zz_queue_run_ready(struct zz_queue *q)
{
    while (q->count) {
        struct zz_qop *op = &q->ops[q->head];
        if (zz_qop_gated(q, op)) {
            if (!q->gate(q->ud, op, 0))
                return;
            zz_qop_ungate(op);
        }
        zz_queue_finish_head(q);
    }
}
