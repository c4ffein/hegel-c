/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
*/

/* Property-based stateful test for kfifo (lib/kfifo.c).
**
** Model: a shadow ring (just a flat unsigned char array with head/tail
** indices) drives the oracle; we run kfifo against the same operation
** sequence and assert byte-for-byte equality after every step.
**
** Rules:
**   - rule_in     : push N random bytes; assert returned-count matches
**                   what the shadow can accept.
**   - rule_out    : (precond: kfifo_len > 0) read up to N bytes;
**                   assert byte sequence matches shadow.
**   - rule_peek   : (precond: kfifo_len > 0) peek without consuming;
**                   assert peek bytes == shadow front.
**   - rule_skip   : (precond: kfifo_len > 0) drop N bytes from head.
**   - rule_reset  : kfifo_reset + clear shadow.
**
** Invariant after each step:
**   - kfifo_len   == shadow_len
**   - kfifo_avail == shadow_avail
**   - byte-by-byte equality via repeated peek+skip on a clone of fifo
**     (we use kfifo_out_peek into a scratch buffer in chunks).
*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/kfifo.h>
#include "hegel_c.h"

/* Power-of-two; kfifo rounds the allocation request up. */
#define KFIFO_CAP   64
#define MAX_OP_LEN  72   /* > capacity, so we exercise the "len gets
                          * clamped to free" path */

/* The kfifo type: a byte-typed kfifo (esize=1, recsize=0). */
typedef STRUCT_KFIFO_PTR (unsigned char) kfifo_bytes_t;

typedef struct {
        kfifo_bytes_t fifo;
        unsigned char shadow[KFIFO_CAP];
        unsigned int  shadow_head;   /* read position */
        unsigned int  shadow_len;    /* live bytes */
        unsigned int  shadow_cap;    /* effective capacity (after rounding) */
} kf_state;

/* ---- shadow ring helpers ---- */

static unsigned int
shadow_avail (kf_state * s)
{
        return s->shadow_cap - s->shadow_len;
}

static unsigned int
shadow_push (kf_state * s, const unsigned char * buf, unsigned int n)
{
        unsigned int free = shadow_avail (s);
        if (n > free) n = free;
        for (unsigned int i = 0; i < n; i++) {
                unsigned int idx = (s->shadow_head + s->shadow_len + i) % s->shadow_cap;
                s->shadow[idx] = buf[i];
        }
        s->shadow_len += n;
        return n;
}

static unsigned int
shadow_peek (kf_state * s, unsigned char * out, unsigned int n)
{
        if (n > s->shadow_len) n = s->shadow_len;
        for (unsigned int i = 0; i < n; i++) {
                unsigned int idx = (s->shadow_head + i) % s->shadow_cap;
                out[i] = s->shadow[idx];
        }
        return n;
}

static unsigned int
shadow_pop (kf_state * s, unsigned char * out, unsigned int n)
{
        n = shadow_peek (s, out, n);
        s->shadow_head = (s->shadow_head + n) % s->shadow_cap;
        s->shadow_len -= n;
        return n;
}

static void
shadow_skip (kf_state * s, unsigned int n)
{
        if (n > s->shadow_len) n = s->shadow_len;
        s->shadow_head = (s->shadow_head + n) % s->shadow_cap;
        s->shadow_len -= n;
}

static void
shadow_reset (kf_state * s)
{
        s->shadow_head = 0;
        s->shadow_len  = 0;
}

/* ---- setup / teardown ---- */

static void *
kf_setup (void)
{
        kf_state * s = calloc (1, sizeof (*s));
        if (!s) return NULL;
        if (kfifo_alloc (&s->fifo, KFIFO_CAP, GFP_KERNEL) != 0) {
                free (s);
                return NULL;
        }
        s->shadow_cap  = kfifo_size (&s->fifo); /* may differ from KFIFO_CAP */
        s->shadow_head = 0;
        s->shadow_len  = 0;
        return s;
}

static void
kf_teardown (void * state)
{
        if (!state) return;
        kf_state * s = state;
        kfifo_free (&s->fifo);
        free (s);
}

/* ---- preconditions ---- */

static bool
have_data (const void * state)
{
        const kf_state * s = state;
        return s->shadow_len > 0;
}

/* ---- rules ---- */

static void
rule_in (void * state, hegel_testcase * tc)
{
        kf_state * s = state;
        unsigned char buf[MAX_OP_LEN];
        int n = hegel_draw_int (tc, 0, MAX_OP_LEN);
        for (int i = 0; i < n; i++) {
                buf[i] = (unsigned char) hegel_draw_int (tc, 0, 255);
        }
        unsigned int kf_in    = kfifo_in    (&s->fifo, buf, n);
        unsigned int sh_in    = shadow_push (s, buf, n);
        HEGEL_ASSERT (kf_in == sh_in,
                      "kfifo_in returned %u, shadow accepted %u (n=%d)",
                      kf_in, sh_in, n);
}

static void
rule_out (void * state, hegel_testcase * tc)
{
        kf_state * s = state;
        unsigned char kbuf[MAX_OP_LEN];
        unsigned char sbuf[MAX_OP_LEN];
        int n = hegel_draw_int (tc, 0, MAX_OP_LEN);
        unsigned int kf_out = kfifo_out (&s->fifo, kbuf, n);
        unsigned int sh_out = shadow_pop (s, sbuf, n);
        HEGEL_ASSERT (kf_out == sh_out,
                      "kfifo_out returned %u, shadow popped %u (n=%d)",
                      kf_out, sh_out, n);
        HEGEL_ASSERT (memcmp (kbuf, sbuf, kf_out) == 0,
                      "byte mismatch after kfifo_out (n_returned=%u)", kf_out);
}

static void
rule_peek (void * state, hegel_testcase * tc)
{
        kf_state * s = state;
        unsigned char kbuf[MAX_OP_LEN];
        unsigned char sbuf[MAX_OP_LEN];
        int n = hegel_draw_int (tc, 0, MAX_OP_LEN);

        /* kfifo_out_peek is a record-mode helper but works for byte
        ** kfifos too — call __kfifo_out_peek on the inner. */
        unsigned int kf_p = __kfifo_out_peek (&s->fifo.kfifo, kbuf, n);
        unsigned int sh_p = shadow_peek (s, sbuf, n);
        HEGEL_ASSERT (kf_p == sh_p,
                      "peek returned %u, shadow %u (n=%d, len=%u)",
                      kf_p, sh_p, n, s->shadow_len);
        HEGEL_ASSERT (memcmp (kbuf, sbuf, kf_p) == 0,
                      "byte mismatch in peek (n_returned=%u)", kf_p);
        /* peek must NOT have consumed anything */
        HEGEL_ASSERT (kfifo_len (&s->fifo) == s->shadow_len,
                      "peek mutated fifo: len=%u shadow=%u",
                      (unsigned) kfifo_len (&s->fifo), s->shadow_len);
}

static void
rule_skip (void * state, hegel_testcase * tc)
{
        kf_state * s = state;
        int n = hegel_draw_int (tc, 0, MAX_OP_LEN);
        /* kfifo's kfifo_skip is "skip 1"; for n bytes use the inner
        ** out-pointer advance, which kfifo internally clamps. */
        unsigned int before = kfifo_len (&s->fifo);
        unsigned int will   = (unsigned int) n;
        if (will > before) will = before;
        s->fifo.kfifo.out += will;
        shadow_skip (s, will);
}

static void
rule_reset (void * state, hegel_testcase * tc)
{
        (void) tc;
        kf_state * s = state;
        kfifo_reset (&s->fifo);
        shadow_reset (s);
}

/* ---- invariants ---- */

static void
inv_len_matches (const void * state, hegel_testcase * tc)
{
        (void) tc;
        const kf_state * s = state;
        unsigned int kf_len = kfifo_len (&s->fifo);
        HEGEL_ASSERT (kf_len == s->shadow_len,
                      "kfifo_len=%u shadow_len=%u",
                      kf_len, s->shadow_len);
}

static void
inv_avail_matches (const void * state, hegel_testcase * tc)
{
        (void) tc;
        const kf_state * s = state;
        unsigned int kf_av = kfifo_avail (&s->fifo);
        unsigned int sh_av = (unsigned int)(s->shadow_cap - s->shadow_len);
        HEGEL_ASSERT (kf_av == sh_av,
                      "kfifo_avail=%u shadow_avail=%u",
                      kf_av, sh_av);
}

static void
inv_bytewise_eq (const void * state, hegel_testcase * tc)
{
        (void) tc;
        const kf_state * s = state;
        unsigned int len = s->shadow_len;
        if (len == 0) return;
        unsigned char kb[KFIFO_CAP * 2];
        unsigned char sb[KFIFO_CAP * 2];
        /* Cast away const for kfifo's macros (they don't actually mutate
        ** here — kfifo_out_peek doesn't advance pointers). */
        kfifo_bytes_t * f = (kfifo_bytes_t *) &s->fifo;
        unsigned int got_k = __kfifo_out_peek (&f->kfifo, kb, len);
        unsigned int got_s = shadow_peek ((kf_state *) s, sb, len);
        HEGEL_ASSERT (got_k == got_s,
                      "full-peek len mismatch %u vs %u", got_k, got_s);
        HEGEL_ASSERT (memcmp (kb, sb, got_k) == 0,
                      "full-peek byte mismatch over %u bytes", got_k);
}

/* ---- main ---- */

int
main (int argc, char * argv[])
{
        (void) argc; (void) argv;

        printf ("kfifo stateful PBT...\n");

        hegel_state_machine * sm = hegel_state_machine_new (kf_setup, kf_teardown);
        if (!sm) { fprintf (stderr, "state_machine_new failed\n"); return 1; }

        if (hegel_state_machine_add_rule (sm, "in", rule_in)        != 0) return 1;
        if (hegel_state_machine_add_rule_with_precondition
              (sm, "out",  have_data, rule_out)                     != 0) return 1;
        if (hegel_state_machine_add_rule_with_precondition
              (sm, "peek", have_data, rule_peek)                    != 0) return 1;
        if (hegel_state_machine_add_rule_with_precondition
              (sm, "skip", have_data, rule_skip)                    != 0) return 1;
        if (hegel_state_machine_add_rule (sm, "reset", rule_reset)  != 0) return 1;

        if (hegel_state_machine_add_invariant (sm, "len",     inv_len_matches)   != 0) return 1;
        if (hegel_state_machine_add_invariant (sm, "avail",   inv_avail_matches) != 0) return 1;
        if (hegel_state_machine_add_invariant (sm, "bytes",   inv_bytewise_eq)   != 0) return 1;

        hegel_state_machine_run_n (sm, 200);
        hegel_state_machine_free (sm);

        printf ("PASSED\n");
        return 0;
}
