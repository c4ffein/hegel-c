/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Negative control for btree PBT: deliberately corrupt the shadow on
** insert so the visitor invariant ALWAYS fails after any insert.
** We expect Hegel to shrink to a 1-step counterexample (single insert).
**
** Invocation: builds and is *expected to fail*.  Runner uses exit-code
** == 1 as success here. */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"

#include <linux/types.h>
#include <linux/btree.h>

#define KEYSPACE 32

typedef struct {
        struct btree_headl bh;
        unsigned long shadow[KEYSPACE + 1];
        int           present[KEYSPACE + 1];
        int           n_present;
} st_t;

static void grim_noop (void *elem, unsigned long opaque,
                       unsigned long key, size_t index)
{
        (void) elem; (void) opaque; (void) key; (void) index;
}

static void *setup_fn (void)
{
        st_t *st = calloc (1, sizeof (*st));
        if (!st) return NULL;
        if (btree_initl (&st->bh) != 0) { free (st); return NULL; }
        return st;
}

static void teardown_fn (void *s)
{
        st_t *st = s;
        if (!st) return;
        btree_grim_visitorl (&st->bh, 0, grim_noop);
        btree_destroyl (&st->bh);
        free (st);
}

static bool any_present (const void *s)
{
        return ((const st_t *) s)->n_present > 0;
}

/* DELIBERATE BUG: shadow gets the value, but the tree gets value+1.
** Lookup will mismatch on the FIRST inserted key.  Expected shrink:
** 1-step counterexample (single insert with k>=1, v>=1). */
static void rule_insert_buggy (void *s, hegel_testcase *tc)
{
        st_t *st = s;
        unsigned long k = (unsigned long) hegel_draw_int (tc, 1, KEYSPACE);
        unsigned long v = (unsigned long) hegel_draw_int (tc, 1, 1000);
        if (st->present[k]) {
                /* Update path — keep behavior simple, no bug here. */
                int rc = btree_updatel (&st->bh, k, (void *) (uintptr_t) v);
                HEGEL_ASSERT (rc == 0, "update");
                st->shadow[k] = v;
        } else {
                /* Insert wrong value into tree, shadow gets correct. */
                int rc = btree_insertl (&st->bh, k,
                                        (void *) (uintptr_t) (v + 0xDEAD),
                                        GFP_KERNEL);
                HEGEL_ASSERT (rc == 0, "insert");
                st->present[k] = 1;
                st->shadow[k] = v;
                st->n_present++;
        }
}

static void rule_lookup (void *s, hegel_testcase *tc)
{
        st_t *st = s;
        unsigned long k = (unsigned long) hegel_draw_int (tc, 1, KEYSPACE);
        void *got = btree_lookupl (&st->bh, k);
        if (st->present[k]) {
                HEGEL_ASSERT ((unsigned long) (uintptr_t) got == st->shadow[k],
                              "lookup(%lu) got=%lu shadow=%lu",
                              k, (unsigned long) (uintptr_t) got, st->shadow[k]);
        } else {
                HEGEL_ASSERT (got == NULL, "lookup(%lu) leak", k);
        }
}

int main (int argc, char *argv[])
{
        unsigned long n_cases = 50;
        if (argc > 1) n_cases = strtoul (argv[1], NULL, 10);

        hegel_state_machine *sm = hegel_state_machine_new (setup_fn, teardown_fn);
        if (!sm) return 1;
        if (hegel_state_machine_add_rule (sm, "insert", rule_insert_buggy) != 0) return 1;
        if (hegel_state_machine_add_rule (sm, "lookup", rule_lookup) != 0) return 1;
        (void) any_present;
        hegel_state_machine_run_n (sm, n_cases);
        hegel_state_machine_free (sm);
        printf ("UNEXPECTED PASS\n");
        return 0;
}
