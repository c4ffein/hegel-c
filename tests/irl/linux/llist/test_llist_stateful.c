/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Property-based stateful test for lib/llist.c — Lock-less NULL-terminated
** singly-linked LIFO list.
**
** SINGLE-THREADED MODEL: we sequentially exercise add / del_first /
** del_all and assert the LIFO ordering, snapshot semantics of del_all,
** llist_empty agreement, and round-trip via llist_reverse_order.  The
** cmpxchg / xchg paths fire without contention — see llist_compat.h.
**
** Shadow:  Vec<int> acting as a stack.  `add(id)` appends to the end;
** `del_first` pops from the end (LIFO).  `del_all` walks the whole stack
** in reverse order (newest first) and clears it.
**
** Rules:
**   - rule_add               : always applicable (precond: have_room).
**                              Push a node carrying a drawn-int identity.
**   - rule_del_first         : precondition: shadow non-empty.
**                              Pop the head; assert it matches shadow's
**                              top (last appended) entry.
**   - rule_del_all           : precondition: shadow non-empty.
**                              Snapshot via llist_del_all; iterate via
**                              llist_for_each_safe; assert the iteration
**                              order is shadow reversed (LIFO); clear
**                              shadow and assert llist is empty.
**
** Invariant after every rule:
**   - llist_empty(&head) == (shadow_n == 0)
**   - The chain length matches shadow_n
**   - Walking the chain via head->first / next yields the IDs in shadow
**     reverse-order (newest first).
*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/llist.h>
#include "hegel_c.h"

#define MAX_NODES   32
#define ID_LO         0
#define ID_HI       999

typedef struct {
	struct llist_node node;
	int id;
	bool live;
} lnode_t;

typedef struct {
	struct llist_head head;
	lnode_t pool[MAX_NODES];
	int     shadow_id[MAX_NODES];   /* stack: append on add, pop from end */
	int     shadow_pool_idx[MAX_NODES]; /* parallel: which pool slot the entry is */
	int     shadow_n;
} lstate_t;

/* ---- setup / teardown ---- */

static void *
ll_setup (void)
{
	lstate_t *s = calloc (1, sizeof (*s));
	if (!s) return NULL;
	init_llist_head (&s->head);
	for (int i = 0; i < MAX_NODES; i++) {
		s->pool[i].live = false;
	}
	s->shadow_n = 0;
	return s;
}

static void
ll_teardown (void *state)
{
	if (!state) return;
	lstate_t *s = state;
	/* Drain any remaining live nodes — the pool is on the heap (calloc'd
	** state), so the nodes themselves go away with `free(s)`, but doing
	** llist_del_all keeps WARN_ON / sanitizer paths happy if they ever
	** check head sanity at teardown. */
	(void) llist_del_all (&s->head);
	free (s);
}

/* ---- preconditions ---- */

static bool
have_room (const void *state)
{
	const lstate_t *s = state;
	return s->shadow_n < MAX_NODES;
}

static bool
have_nodes (const void *state)
{
	const lstate_t *s = state;
	return s->shadow_n > 0;
}

/* ---- rules ---- */

static void
rule_add (void *state, hegel_testcase *tc)
{
	lstate_t *s = state;

	int slot = -1;
	for (int i = 0; i < MAX_NODES; i++) {
		if (!s->pool[i].live) { slot = i; break; }
	}
	HEGEL_ASSERT (slot >= 0, "rule_add: pool full but precond passed");

	int id = hegel_draw_int (tc, ID_LO, ID_HI);
	s->pool[slot].id   = id;
	s->pool[slot].live = true;
	s->pool[slot].node.next = NULL;

	bool was_empty_before = (s->shadow_n == 0);
	bool ret = llist_add (&s->pool[slot].node, &s->head);

	HEGEL_ASSERT (ret == was_empty_before,
	              "llist_add return %d != was_empty_before %d (n=%d)",
	              (int) ret, (int) was_empty_before, s->shadow_n);

	s->shadow_id      [s->shadow_n] = id;
	s->shadow_pool_idx[s->shadow_n] = slot;
	s->shadow_n++;
}

static void
rule_del_first (void *state, hegel_testcase *tc)
{
	(void) tc;
	lstate_t *s = state;

	struct llist_node *got = llist_del_first (&s->head);
	HEGEL_ASSERT (got != NULL,
	              "llist_del_first returned NULL but shadow_n=%d", s->shadow_n);

	/* Top of stack — most-recently added. */
	int top    = s->shadow_n - 1;
	int exp_id = s->shadow_id [top];
	int exp_sl = s->shadow_pool_idx[top];

	lnode_t *got_entry = container_of (got, lnode_t, node);
	HEGEL_ASSERT (got_entry == &s->pool[exp_sl],
	              "del_first wrong node: got pool[%ld], expected pool[%d]",
	              (long)(got_entry - s->pool), exp_sl);
	HEGEL_ASSERT (got_entry->id == exp_id,
	              "del_first wrong id: got %d, expected %d",
	              got_entry->id, exp_id);

	got_entry->live = false;
	s->shadow_n--;
}

static void
rule_del_all (void *state, hegel_testcase *tc)
{
	(void) tc;
	lstate_t *s = state;

	struct llist_node *chain = llist_del_all (&s->head);
	HEGEL_ASSERT (chain != NULL,
	              "llist_del_all returned NULL but shadow_n=%d", s->shadow_n);

	/* Walk the snapshot chain.  Order: newest-first (LIFO).  That
	** corresponds to shadow walked top -> bottom. */
	struct llist_node *pos, *next;
	int walk_n = 0;
	int expect_top = s->shadow_n - 1;

	llist_for_each_safe (pos, next, chain) {
		HEGEL_ASSERT (walk_n < s->shadow_n,
		              "del_all chain longer than shadow (%d > %d) — cycle?",
		              walk_n + 1, s->shadow_n);

		int idx = expect_top - walk_n;
		int exp_sl = s->shadow_pool_idx[idx];
		int exp_id = s->shadow_id     [idx];

		lnode_t *e = container_of (pos, lnode_t, node);
		HEGEL_ASSERT (e == &s->pool[exp_sl],
		              "del_all step %d wrong node: got pool[%ld], "
		              "expected pool[%d]", walk_n,
		              (long)(e - s->pool), exp_sl);
		HEGEL_ASSERT (e->id == exp_id,
		              "del_all step %d wrong id: got %d, expected %d",
		              walk_n, e->id, exp_id);
		e->live = false;
		walk_n++;
	}

	HEGEL_ASSERT (walk_n == s->shadow_n,
	              "del_all yielded %d entries, shadow had %d", walk_n, s->shadow_n);

	s->shadow_n = 0;
	HEGEL_ASSERT (llist_empty (&s->head),
	              "after del_all, llist not empty");
}

/* ---- invariant ---- */

static void
inv_lifo_shape (const void *state, hegel_testcase *tc)
{
	(void) tc;
	const lstate_t *s = state;

	/* (1) llist_empty agrees with shadow. */
	bool emp = llist_empty (&s->head);
	HEGEL_ASSERT (emp == (s->shadow_n == 0),
	              "llist_empty=%d but shadow_n=%d", (int) emp, s->shadow_n);

	/* (2) traverse head; expect length and reverse-order id match. */
	int walk_n = 0;
	int expect_top = s->shadow_n - 1;
	for (struct llist_node *p = s->head.first; p != NULL; p = p->next) {
		HEGEL_ASSERT (walk_n < s->shadow_n,
		              "llist longer than shadow (>=%d > %d) — cycle?",
		              walk_n + 1, s->shadow_n);
		int idx = expect_top - walk_n;
		int exp_sl = s->shadow_pool_idx[idx];
		int exp_id = s->shadow_id     [idx];
		lnode_t *e = container_of (p, lnode_t, node);
		HEGEL_ASSERT (e == &s->pool[exp_sl],
		              "invariant: step %d wrong node (pool[%ld] vs [%d])",
		              walk_n, (long)(e - s->pool), exp_sl);
		HEGEL_ASSERT (e->id == exp_id,
		              "invariant: step %d wrong id (got %d, expected %d)",
		              walk_n, e->id, exp_id);
		walk_n++;
	}
	HEGEL_ASSERT (walk_n == s->shadow_n,
	              "invariant: walk_n=%d shadow_n=%d", walk_n, s->shadow_n);
}

/* ---- main ---- */

int
main (int argc, char *argv[])
{
	(void) argc; (void) argv;

	printf ("llist stateful PBT...\n");

	hegel_state_machine *sm = hegel_state_machine_new (ll_setup, ll_teardown);
	if (!sm) { fprintf (stderr, "state_machine_new failed\n"); return 1; }

	if (hegel_state_machine_add_rule_with_precondition
	      (sm, "add",        have_room,  rule_add)        != 0) return 1;
	if (hegel_state_machine_add_rule_with_precondition
	      (sm, "del_first",  have_nodes, rule_del_first)  != 0) return 1;
	if (hegel_state_machine_add_rule_with_precondition
	      (sm, "del_all",    have_nodes, rule_del_all)    != 0) return 1;

	if (hegel_state_machine_add_invariant
	      (sm, "lifo_shape", inv_lifo_shape) != 0) return 1;

	hegel_state_machine_run_n (sm, 300);
	hegel_state_machine_free (sm);

	printf ("PASSED\n");
	return 0;
}
