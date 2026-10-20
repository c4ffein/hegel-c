/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Stateful PBT against lib/btree.c (Linux's simple in-memory B+Tree
** used by drivers/staging/, drivers/scsi/, etc.).
**
** Different shape from xarray/maple_tree:
**   - btree.c uses an explicit mempool (we shim it — see include/linux/mempool.h)
**     and an internal kmem_cache for nodes.
**   - keys are arrays of unsigned long (geo->keylen), but we use the
**     `unsigned long` type-instantiated wrappers (btree_*l) for
**     simplicity.
**   - storing NULL is illegal — btree_insert BUG_ON(!val).  Our shadow
**     uses non-zero unsigned longs.
**   - key 0 is a legal key but interacts oddly with btree_get_prev
**     (which uses keyzero as a "no smaller key exists" sentinel).
**
** State machine rules:
**   rule_insert        — btree_insertl(k, v): always applicable
**   rule_lookup        — btree_lookupl(k) match shadow: always applicable
**   rule_remove_existing — btree_removel(k): PRECOND shadow non-empty
**   rule_last          — btree_lastl(&k): asserts shadow_max() match
**
** Invariant (post-step):
**   inv_visitor_sorted_matches_shadow — btree_visitorl walks every key
**     in some order (the visitor calls our callback for each entry);
**     we collect them, sort them, and compare against the sorted
**     shadow keys + values.
**
** Friction notes are gathered in the running comments below — see end
** of main() for the writeup. */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hegel_c.h"

/* The harness's bug.h, types.h, gfp.h pulled in (also via -include
** shared.h in the Makefile).  btree.h pulls in mempool.h — our local
** include/linux/mempool.h provides a userspace shim. */
#include <linux/types.h>
#include <linux/btree.h>

#define KEYSPACE   256          /* keys are unsigned long; we draw 1..KEYSPACE (avoid 0 — see note) */
#define MAX_OPS    96

typedef struct {
        struct btree_headl bh;
        unsigned long shadow[KEYSPACE + 1];     /* shadow[k] = value or 0 if absent */
        int           present[KEYSPACE + 1];
        int           n_present;

        long c_insert;
        long c_lookup_hit;
        long c_lookup_miss;
        long c_remove;
        long c_last;
        long c_visitor;
} st_t;

/* ---- coverage globals ---- */
static long g_c_insert;
static long g_c_lookup_hit;
static long g_c_lookup_miss;
static long g_c_remove;
static long g_c_last;
static long g_c_visitor;
static long g_n_cases;
static long g_max_n_present;

/* ---- setup / teardown ---- */

static void *setup_fn (void)
{
        st_t *st = calloc (1, sizeof (*st));
        if (!st) return NULL;
        if (btree_initl (&st->bh) != 0) {
                free (st);
                return NULL;
        }
        return st;
}

/* No-op visitor for grim-walk in teardown.  Friction note E:
** btree_destroy() only frees the root node + the mempool struct —
** it does NOT walk child nodes.  If the tree has height > 1, child
** nodes LEAK unless the caller drains via btree_grim_visitor first
** (which walks-and-frees every node).  This contract is implicit in
** the API; the docstring on btree_destroy only says "destroy mempool"
** but doesn't warn about the leak.  Clients of the API have to know
** to call grim_visitor before destroy.  ASAN flagged this immediately
** in our PBT (199-object leak across 100 cases). */
static void grim_noop (void *elem, unsigned long opaque,
                       unsigned long key, size_t index)
{
        (void) elem; (void) opaque; (void) key; (void) index;
}

static void teardown_fn (void *s)
{
        st_t *st = s;
        if (!st) return;
        /* Drain tree before destroy; grim_visitor walks every node and
        ** frees it back to the mempool. */
        btree_grim_visitorl (&st->bh, 0, grim_noop);
        btree_destroyl (&st->bh);

        g_c_insert      += st->c_insert;
        g_c_lookup_hit  += st->c_lookup_hit;
        g_c_lookup_miss += st->c_lookup_miss;
        g_c_remove      += st->c_remove;
        g_c_last        += st->c_last;
        g_c_visitor     += st->c_visitor;
        g_n_cases++;
        if (st->n_present > g_max_n_present) g_max_n_present = st->n_present;
        free (st);
}

/* ---- preconditions ---- */

static bool any_present (const void *s)
{
        const st_t *st = s;
        return st->n_present > 0;
}

/* ---- rule: insert ----
**
** btree_insert BUGs on duplicate keys (see btree_insert_level: BUG_ON
** if pos < fill && keycmp == 0).  We must filter duplicates BEFORE
** calling — but a dup-insert is an interesting case to exercise
** btree_update's idempotence.  Pattern: if shadow has the key, route
** to btree_updatel instead.  Friction note A. */
static void rule_insert (void *s, hegel_testcase *tc)
{
        st_t *st = s;
        unsigned long k = (unsigned long) hegel_draw_int (tc, 1, KEYSPACE);
        /* Values must be non-NULL.  We use small non-zero unsigned longs
        ** and store them as `void *` via a cast.  Using the key+1 packed
        ** as a tagged value (lower bit set) avoids confusing the GC of
        ** any future enhancement; for now plain integer-as-pointer is
        ** fine — btree never derefs values. */
        unsigned long v = (unsigned long) hegel_draw_int (tc, 1, 1000);
        void *val = (void *) (uintptr_t) v;

        if (st->present[k]) {
                int rc = btree_updatel (&st->bh, k, val);
                HEGEL_ASSERT (rc == 0,
                              "btree_updatel(k=%lu) rc=%d (shadow has it)",
                              k, rc);
                st->shadow[k] = v;
        } else {
                int rc = btree_insertl (&st->bh, k, val, GFP_KERNEL);
                HEGEL_ASSERT (rc == 0,
                              "btree_insertl(k=%lu v=%lu) rc=%d", k, v, rc);
                st->present[k] = 1;
                st->shadow[k] = v;
                st->n_present++;
        }
        st->c_insert++;
}

/* ---- rule: lookup random key ---- */
static void rule_lookup (void *s, hegel_testcase *tc)
{
        st_t *st = s;
        unsigned long k = (unsigned long) hegel_draw_int (tc, 1, KEYSPACE);
        void *got = btree_lookupl (&st->bh, k);
        if (st->present[k]) {
                HEGEL_ASSERT (got != NULL,
                              "btree_lookupl(%lu) NULL but shadow has it (=%lu)",
                              k, st->shadow[k]);
                HEGEL_ASSERT ((unsigned long) (uintptr_t) got == st->shadow[k],
                              "btree_lookupl(%lu) got=%lu shadow=%lu",
                              k, (unsigned long) (uintptr_t) got, st->shadow[k]);
                st->c_lookup_hit++;
        } else {
                HEGEL_ASSERT (got == NULL,
                              "btree_lookupl(%lu) returned %lu but shadow says absent",
                              k, (unsigned long) (uintptr_t) got);
                st->c_lookup_miss++;
        }
}

/* ---- rule: remove an existing key ----
**
** Friction note B: still no draw_index_of primitive.  We draw an
** ordinal in [0, n_present-1], walk the dense shadow table to find
** the ordinal-th present key.  Same pattern maple_tree had. */
static void rule_remove_existing (void *s, hegel_testcase *tc)
{
        st_t *st = s;
        int ord = hegel_draw_int (tc, 0, st->n_present - 1);
        int seen = 0;
        unsigned long k = 0;
        for (int i = 1; i <= KEYSPACE; i++) {
                if (st->present[i]) {
                        if (seen == ord) { k = (unsigned long) i; break; }
                        seen++;
                }
        }
        void *prev = btree_removel (&st->bh, k);
        unsigned long expected = st->shadow[k];
        HEGEL_ASSERT (prev != NULL,
                      "btree_removel(%lu) NULL but shadow had it (=%lu)",
                      k, expected);
        HEGEL_ASSERT ((unsigned long) (uintptr_t) prev == expected,
                      "btree_removel(%lu) prev=%lu shadow=%lu",
                      k, (unsigned long) (uintptr_t) prev, expected);
        st->present[k] = 0;
        st->shadow[k] = 0;
        st->n_present--;
        st->c_remove++;
}

/* ---- rule: btree_last ----
**
** Friction note C (PBT WIN — caught my reading of the code in 3 steps):
** btree_last returns the LARGEST key in the tree (despite "lowest
** values to the right" — slot 0 is the LEFTMOST slot which holds the
** LARGEST key in this layout).  The docstring just says "last entry"
** which is ambiguous given btree.c's reverse layout convention.
** The canonical client (drivers/staging/most/) confirms this — they
** use it as "max key still present".  PBT minimized this to a 2-step
** counterexample: insert(1,1), insert(2,1), btree_last returns key=2.
** Tightens up the docstring as an unambiguous "returns largest key". */
static void rule_last (void *s, hegel_testcase *tc)
{
        st_t *st = s;
        (void) tc;
        unsigned long k = 0;
        void *got = btree_lastl (&st->bh, &k);
        if (st->n_present == 0) {
                HEGEL_ASSERT (got == NULL,
                              "btree_lastl on empty tree returned %lu (key=%lu)",
                              (unsigned long) (uintptr_t) got, k);
        } else {
                /* Find the LARGEST present key in the shadow. */
                unsigned long want_k = 0;
                for (int i = KEYSPACE; i >= 1; i--) {
                        if (st->present[i]) { want_k = (unsigned long) i; break; }
                }
                HEGEL_ASSERT (got != NULL,
                              "btree_lastl returned NULL but shadow has %d entries",
                              st->n_present);
                HEGEL_ASSERT (k == want_k,
                              "btree_lastl key=%lu but largest shadow key is %lu",
                              k, want_k);
                HEGEL_ASSERT ((unsigned long) (uintptr_t) got == st->shadow[want_k],
                              "btree_lastl val=%lu but shadow[%lu]=%lu",
                              (unsigned long) (uintptr_t) got, want_k,
                              st->shadow[want_k]);
        }
        st->c_last++;
}

/* ---- visitor callback for invariant ---- */

#define VISITOR_BUF_MAX (KEYSPACE + 16)
struct visit_buf {
        unsigned long keys[VISITOR_BUF_MAX];
        unsigned long vals[VISITOR_BUF_MAX];
        int           n;
};

static void visit_collect (void *elem, unsigned long opaque,
                           unsigned long key, size_t index)
{
        (void) index;
        struct visit_buf *b = (struct visit_buf *) (uintptr_t) opaque;
        if (b->n >= VISITOR_BUF_MAX) return;
        b->keys[b->n] = key;
        b->vals[b->n] = (unsigned long) (uintptr_t) elem;
        b->n++;
}

static int cmp_ulong (const void *a, const void *b)
{
        unsigned long x = *(const unsigned long *) a;
        unsigned long y = *(const unsigned long *) b;
        if (x < y) return -1;
        if (x > y) return 1;
        return 0;
}

/* Visitor produces (key, val) pairs.  We need a stable sort by key, but
** also need to keep vals aligned — use parallel arrays + index sort. */
static void sort_pairs_by_key (unsigned long *k, unsigned long *v, int n)
{
        /* Insertion sort — n is bounded by KEYSPACE+16 ≤ 272.  Fine for
        ** PBT.  Could use qsort with a struct but simpler to be
        ** explicit here. */
        for (int i = 1; i < n; i++) {
                unsigned long ki = k[i];
                unsigned long vi = v[i];
                int j = i - 1;
                while (j >= 0 && k[j] > ki) {
                        k[j + 1] = k[j];
                        v[j + 1] = v[j];
                        j--;
                }
                k[j + 1] = ki;
                v[j + 1] = vi;
        }
        (void) cmp_ulong;       /* silence unused-fn warning */
}

/* ---- invariant: visitor enumerates exactly the shadow ---- */
static void inv_visitor_matches_shadow (const void *s, hegel_testcase *tc)
{
        (void) tc;
        st_t *st = (st_t *) s;
        struct visit_buf vb = { .n = 0 };
        size_t count = btree_visitorl (&st->bh, (unsigned long) (uintptr_t) &vb,
                                       visit_collect);
        HEGEL_ASSERT ((int) count == st->n_present,
                      "btree_visitorl count=%zu but shadow has %d",
                      count, st->n_present);
        HEGEL_ASSERT (vb.n == st->n_present,
                      "visitor callback count=%d but shadow has %d",
                      vb.n, st->n_present);

        /* Build expected sorted key list from shadow. */
        unsigned long want_k[KEYSPACE + 1];
        unsigned long want_v[KEYSPACE + 1];
        int want_n = 0;
        for (int i = 1; i <= KEYSPACE; i++) {
                if (st->present[i]) {
                        want_k[want_n] = (unsigned long) i;
                        want_v[want_n] = st->shadow[i];
                        want_n++;
                }
        }

        /* Sort visitor output by key (visitor doesn't promise an order
        ** in btree.c — it walks slots left-to-right, but slots store
        ** larger keys on the left; for our flat visitor we sort
        ** explicitly to make the comparison order-independent). */
        sort_pairs_by_key (vb.keys, vb.vals, vb.n);

        for (int i = 0; i < want_n; i++) {
                HEGEL_ASSERT (vb.keys[i] == want_k[i],
                              "visitor key[%d]=%lu shadow=%lu",
                              i, vb.keys[i], want_k[i]);
                HEGEL_ASSERT (vb.vals[i] == want_v[i],
                              "visitor val at k=%lu got=%lu shadow=%lu",
                              vb.keys[i], vb.vals[i], want_v[i]);
        }
        st->c_visitor++;
}

/* ---- main ---- */

int main (int argc, char *argv[])
{
        unsigned long n_cases = 200;
        if (argc > 1) n_cases = strtoul (argv[1], NULL, 10);

        printf ("btree stateful PBT (n_cases=%lu, keyspace=%d)\n",
                n_cases, KEYSPACE);

        hegel_state_machine *sm = hegel_state_machine_new (setup_fn, teardown_fn);
        if (!sm) { fprintf (stderr, "state_machine_new failed\n"); return 1; }

        /* Always-applicable rules. Insert is registered twice to bias
        ** toward growing the tree (so visitor invariant exercises real
        ** structure beyond a few entries).  Friction note D: still no
        ** per-rule weight knob, same as maple_tree. */
        if (hegel_state_machine_add_rule (sm, "insert",   rule_insert) != 0) return 1;
        if (hegel_state_machine_add_rule (sm, "insert_2", rule_insert) != 0) return 1;
        if (hegel_state_machine_add_rule (sm, "lookup",   rule_lookup) != 0) return 1;
        if (hegel_state_machine_add_rule (sm, "last",     rule_last)   != 0) return 1;

        if (hegel_state_machine_add_rule_with_precondition
                (sm, "remove_existing", any_present, rule_remove_existing) != 0)
                return 1;

        if (hegel_state_machine_add_invariant
                (sm, "visitor_matches_shadow", inv_visitor_matches_shadow) != 0)
                return 1;

        hegel_state_machine_run_n (sm, n_cases);
        hegel_state_machine_free (sm);

        fprintf (stderr,
                 "[btree PBT coverage]\n"
                 "  cases=%ld  max_n_present=%ld\n"
                 "  insert=%ld  lookup_hit=%ld  lookup_miss=%ld\n"
                 "  remove=%ld  last=%ld  visitor=%ld\n",
                 g_n_cases, g_max_n_present,
                 g_c_insert, g_c_lookup_hit, g_c_lookup_miss,
                 g_c_remove, g_c_last, g_c_visitor);
        printf ("PASSED\n");
        return 0;
}
