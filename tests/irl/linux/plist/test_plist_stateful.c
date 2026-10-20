/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Property-based stateful test for lib/plist.c — the priority-sorted
** doubly-linked list used by rt-mutex priority inheritance.
**
** Model: a shadow `Vec<(node*, prio, ins_order)>` where ins_order is a
** monotonic counter incremented on each successful add.  The shadow is
** never sorted internally — it just records insertion order.
**
** Rules:
**   - rule_add(prio)              : always applicable.  Allocate a node,
**                                   plist_node_init + plist_add, append
**                                   (node, prio, ins_order) to shadow.
**   - rule_remove_existing        : precondition: shadow non-empty.  Pick
**                                   an ordinal, plist_del, drop from shadow.
**
** Invariant after every rule:
**   - Walk plist in iteration order; collect (prio, ins_order) tuples.
**   - The walked tuples must be the same MULTISET as the shadow's
**     {(prio, ins_order) : entry in shadow}.
**   - The walked tuples must be NON-DECREASING in lex (prio, ins_order)
**     — proves both sort order AND tie-stability (FIFO at equal prio).
**
** Plist documents itself as descending-priority-sorted in the file
** comment, but the iteration order semantics are: lower `prio` int
** value comes first (INT_MIN is "highest priority", served first).
** So the lex comparator is straightforward integer-ascending on prio.
*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/plist.h>
#include "hegel_c.h"

#define MAX_NODES   32
#define PRIO_LO   (-3)
#define PRIO_HI    (3)

typedef struct {
	struct plist_node node;
	int  ins_order;   /* monotonic; copy in shadow for cross-check */
	bool live;
} plnode_t;

typedef struct {
	struct plist_head head;
	plnode_t  pool[MAX_NODES];
	int  shadow_idx[MAX_NODES];   /* ordered by insertion; values are pool indices */
	int  shadow_n;
	int  next_ins_order;
} plstate_t;

/* ---- setup / teardown ---- */

static void *
pl_setup (void)
{
	plstate_t *s = calloc (1, sizeof (*s));
	if (!s) return NULL;
	plist_head_init (&s->head);
	for (int i = 0; i < MAX_NODES; i++) {
		s->pool[i].live = false;
	}
	s->shadow_n = 0;
	s->next_ins_order = 0;
	return s;
}

static void
pl_teardown (void *state)
{
	if (!state) return;
	plstate_t *s = state;
	/* Drain remaining nodes for hygiene (catches any state leak between
	** cases on top of the per-case calloc). */
	for (int k = 0; k < s->shadow_n; k++) {
		int id = s->shadow_idx[k];
		plist_del (&s->pool[id].node, &s->head);
	}
	free (s);
}

/* ---- preconditions ---- */

static bool
have_nodes (const void *state)
{
	const plstate_t *s = state;
	return s->shadow_n > 0 && s->shadow_n < MAX_NODES;
	/* The MAX_NODES upper guard isn't strictly precondition-required,
	** but pairing the bound here keeps the state machine well-defined
	** in the (unlikely) full case — see rule_add. */
}

static bool
have_room (const void *state)
{
	const plstate_t *s = state;
	return s->shadow_n < MAX_NODES;
}

/* ---- rules ---- */

static void
rule_add (void *state, hegel_testcase *tc)
{
	plstate_t *s = state;

	/* Find a free pool slot. */
	int id = -1;
	for (int i = 0; i < MAX_NODES; i++) {
		if (!s->pool[i].live) { id = i; break; }
	}
	HEGEL_ASSERT (id >= 0, "rule_add: pool full but precond passed");

	int prio = hegel_draw_int (tc, PRIO_LO, PRIO_HI);
	plist_node_init (&s->pool[id].node, prio);
	s->pool[id].ins_order = s->next_ins_order++;
	s->pool[id].live      = true;

	plist_add (&s->pool[id].node, &s->head);

	s->shadow_idx[s->shadow_n++] = id;
}

static void
rule_remove_existing (void *state, hegel_testcase *tc)
{
	plstate_t *s = state;
	int pick = hegel_draw_int (tc, 0, s->shadow_n - 1);
	int id   = s->shadow_idx[pick];

	plist_del (&s->pool[id].node, &s->head);
	s->pool[id].live = false;

	/* Compact shadow_idx (preserves the relative order of the others —
	** doesn't matter, but keeps the array dense). */
	memmove (&s->shadow_idx[pick], &s->shadow_idx[pick + 1],
	         (s->shadow_n - pick - 1) * sizeof (int));
	s->shadow_n--;
}

/* ---- invariants ---- */

/* Walk the plist; collect (prio, ins_order) tuples; check both:
**   (a) same multiset as shadow's tuples
**   (b) iteration order is non-decreasing in (prio, ins_order) lex
*/
static void
inv_sorted_and_stable (const void *state, hegel_testcase *tc)
{
	(void) tc;
	const plstate_t *s = state;

	/* Walk plist. */
	int walk_prio[MAX_NODES];
	int walk_ord[MAX_NODES];
	int walk_n = 0;

	plnode_t *pos;
	plist_for_each_entry (pos, &s->head, node) {
		HEGEL_ASSERT (walk_n < MAX_NODES,
		              "plist walk exceeded MAX_NODES — possible cycle");
		walk_prio[walk_n] = pos->node.prio;
		walk_ord [walk_n] = pos->ins_order;
		walk_n++;
	}

	/* (1) length matches shadow. */
	HEGEL_ASSERT (walk_n == s->shadow_n,
	              "plist length %d != shadow length %d", walk_n, s->shadow_n);

	/* (2) iteration order is non-decreasing in (prio, ins_order) lex.
	**     equal-prio: ins_order must increase strictly (FIFO stability). */
	for (int i = 1; i < walk_n; i++) {
		if (walk_prio[i] < walk_prio[i - 1]) {
			HEGEL_ASSERT (0,
			              "plist out of order at %d: prio[%d]=%d < prio[%d]=%d",
			              i, i, walk_prio[i], i - 1, walk_prio[i - 1]);
		}
		if (walk_prio[i] == walk_prio[i - 1]) {
			HEGEL_ASSERT (walk_ord[i] > walk_ord[i - 1],
			              "tie-stability broken at %d: same prio=%d but "
			              "ins_order[%d]=%d <= ins_order[%d]=%d",
			              i, walk_prio[i],
			              i,     walk_ord[i],
			              i - 1, walk_ord[i - 1]);
		}
	}

	/* (3) walked multiset equals shadow multiset.
	**     Cheapest equality: for every shadow node, find a matching
	**     entry in walk[] and mark it.  Use a small bitmap by index. */
	bool matched[MAX_NODES] = {false};
	for (int k = 0; k < s->shadow_n; k++) {
		int id   = s->shadow_idx[k];
		int sp   = s->pool[id].node.prio;
		int so   = s->pool[id].ins_order;
		bool hit = false;
		for (int i = 0; i < walk_n; i++) {
			if (!matched[i] && walk_prio[i] == sp && walk_ord[i] == so) {
				matched[i] = true;
				hit = true;
				break;
			}
		}
		HEGEL_ASSERT (hit, "shadow node id=%d (prio=%d, ord=%d) missing from plist walk",
		              id, sp, so);
	}
}

/* ---- main ---- */

int
main (int argc, char *argv[])
{
	(void) argc; (void) argv;

	printf ("plist stateful PBT...\n");

	hegel_state_machine *sm = hegel_state_machine_new (pl_setup, pl_teardown);
	if (!sm) { fprintf (stderr, "state_machine_new failed\n"); return 1; }

	if (hegel_state_machine_add_rule_with_precondition
	      (sm, "add", have_room, rule_add)                            != 0) return 1;
	if (hegel_state_machine_add_rule_with_precondition
	      (sm, "remove_existing", have_nodes, rule_remove_existing)   != 0) return 1;

	if (hegel_state_machine_add_invariant
	      (sm, "sorted_and_stable", inv_sorted_and_stable)            != 0) return 1;

	hegel_state_machine_run_n (sm, 300);
	hegel_state_machine_free (sm);

	printf ("PASSED\n");
	return 0;
}
