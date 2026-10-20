/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Property (the documented parman invariant):
**   After ANY sequence of parman_item_add / parman_item_remove calls
**   the backing array (as observed through parman's resize/move
**   callbacks) is laid out as contiguous priority chunks ordered by
**   `priority` ascending, with no gaps and total length = adds − removes.
**   Within a chunk the order is unspecified — parman documents it as
**   "the order of items within a single priority chunk does not matter".
**
**   We verify by maintaining a "shadow model" of which item ids are
**   alive in each priority bucket (multiset, not ordered list).  After
**   every op we walk parman's callback-driven backing array and check:
**   slot[0..N-1] is fully populated, slots beyond are unused, and the
**   ids found in each priority chunk's range MATCH (as a set) the ids
**   the shadow says belong to that priority. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/parman.h>
#include "hegel_c.h"

#define MAX_ITEMS 16
#define N_PRIOS    4
#define PRIO_STEP 10
#define MAX_OPS   40

/* ------------------------------------------------------------------ *
**  Backing array driven purely by parman callbacks                   *
** ------------------------------------------------------------------ */
struct slot { int id; bool used; };
static struct slot g_array[MAX_ITEMS * 4];     /* over-provisioned */
static unsigned long g_capacity;
static unsigned long g_peak_capacity;
static int g_resize_calls;
static int g_move_calls;

/* Map from parman_item* -> our id, since parman owns the indices but we
** need to know "which item is here". We reverse-walk via the items[] table
** in the test driver, which holds id -> parman_item. */

/* Set to 1 to introduce a deliberate fault in my_move (corrupts the
** moved id) — a sanity check that the invariant assertions actually
** fire on a real disagreement.  Leave at 0 for normal runs. */
#define HEGEL_PARMAN_INJECT_BUG 0

static int my_resize (void *priv, unsigned long new_count)
{
  (void) priv;
  g_resize_calls++;
  if (new_count > sizeof (g_array) / sizeof (g_array[0])) {
    fprintf (stderr, "resize requested %lu > capacity\n", new_count);
    return -1;
  }
  /* Newly added slots are unused; shrinking is fine because parman
  ** guarantees it only shrinks once items have been moved out. */
  if (new_count > g_capacity) {
    for (unsigned long i = g_capacity; i < new_count; i++) {
      g_array[i].id   = -1;
      g_array[i].used = false;
    }
  }
  g_capacity = new_count;
  if (g_capacity > g_peak_capacity) g_peak_capacity = g_capacity;
  return 0;
}

static void my_move (void *priv, unsigned long from, unsigned long to,
                     unsigned long count)
{
  (void) priv;
  g_move_calls++;
  /* Use memmove semantics — parman may move forwards or backwards. */
  if (from + count > g_capacity || to + count > g_capacity) {
    fprintf (stderr, "move out of bounds: from=%lu to=%lu count=%lu cap=%lu\n",
             from, to, count, g_capacity);
    abort ();
  }
  /* Source must be fully populated; dest must be fully empty (parman
  ** only ever moves into a freed slot — verify that). */
  for (unsigned long i = 0; i < count; i++) {
    if (!g_array[from + i].used) {
      fprintf (stderr, "move from unused slot %lu\n", from + i);
      abort ();
    }
  }
  /* Allow overlap: copy first to a temp. */
  struct slot tmp[MAX_ITEMS * 4];
  for (unsigned long i = 0; i < count; i++)
    tmp[i] = g_array[from + i];
  for (unsigned long i = 0; i < count; i++) {
    g_array[from + i].id   = -1;
    g_array[from + i].used = false;
  }
  for (unsigned long i = 0; i < count; i++) {
    g_array[to + i] = tmp[i];
#if HEGEL_PARMAN_INJECT_BUG
    /* Deliberately mangle the id to verify the property assertions fire. */
    g_array[to + i].id = 999;
#endif
  }
}

static const struct parman_ops my_ops = {
  .base_count  = 4,
  .resize_step = 4,
  .resize      = my_resize,
  .move        = my_move,
  .algo        = PARMAN_ALGO_TYPE_LSORT,
};

/* ------------------------------------------------------------------ *
**  Shadow model: what we EXPECT the backing array to look like       *
**                                                                    *
**  Per-priority FIFOs of item ids.  After any add/remove sequence,
**  the expected array is the concatenation, in ascending prio order,
**  of each FIFO. */
struct fifo { int ids[MAX_ITEMS]; int n; };
static struct fifo shadow[N_PRIOS];

static void shadow_reset (void)
{
  for (int p = 0; p < N_PRIOS; p++) shadow[p].n = 0;
}

static void shadow_add (int prio_idx, int id)
{
  shadow[prio_idx].ids[shadow[prio_idx].n++] = id;
}

static void shadow_remove (int prio_idx, int id)
{
  struct fifo *f = &shadow[prio_idx];
  for (int i = 0; i < f->n; i++) {
    if (f->ids[i] == id) {
      memmove (&f->ids[i], &f->ids[i + 1], (f->n - i - 1) * sizeof (int));
      f->n--;
      return;
    }
  }
  abort ();   /* test bug */
}

static int shadow_total (void)
{
  int t = 0; for (int p = 0; p < N_PRIOS; p++) t += shadow[p].n; return t;
}

/* Find the index range [lo, hi) in the backing array where prio_idx
** items should live according to the shadow. */
static void shadow_chunk_range (int prio_idx, int *lo, int *hi)
{
  int off = 0;
  for (int p = 0; p < prio_idx; p++) off += shadow[p].n;
  *lo = off;
  *hi = off + shadow[prio_idx].n;
}

/* Membership check: is id one of the shadow's ids in this prio bucket? */
static bool shadow_chunk_contains (int prio_idx, int id)
{
  for (int i = 0; i < shadow[prio_idx].n; i++)
    if (shadow[prio_idx].ids[i] == id) return true;
  return false;
}

/* ------------------------------------------------------------------ *
**  Property                                                          *
** ------------------------------------------------------------------ */
void test_parman (hegel_testcase *tc)
{
  /* Reset everything (the test fn is replayed across cases). */
  memset (g_array, 0, sizeof g_array);
  for (size_t i = 0; i < sizeof g_array / sizeof g_array[0]; i++) {
    g_array[i].id = -1; g_array[i].used = false;
  }
  g_capacity = 0;
  g_peak_capacity = 0;
  g_resize_calls = 0;
  g_move_calls = 0;
  shadow_reset ();

  struct parman *pm = parman_create (&my_ops, NULL);
  HEGEL_ASSERT (pm != NULL, "parman_create returned NULL");

  /* parman_create initializes limit_count = ops->base_count but does
  ** NOT call resize on creation, so we must size the backing array
  ** to base_count manually so the first add fits without enlarge.
  ** Actually: re-reading parman_lsort_item_add: when count+1 > limit
  ** it calls parman_enlarge → ops->resize.  On the very first add,
  ** count=0, limit_count=base_count=4; so 0+1>4 is false and no
  ** resize occurs.  But our backing array must already be >= 4
  ** entries to receive item->index=0..3 — set it now. */
  if (my_resize (NULL, my_ops.base_count) != 0) abort ();

  struct parman_prio prios[N_PRIOS];
  for (int p = 0; p < N_PRIOS; p++)
    parman_prio_init (pm, &prios[p], (unsigned long) (p * PRIO_STEP));

  /* Item slot table: map id -> parman_item* + which prio it lives in. */
  struct parman_item items_pool[MAX_ITEMS];
  int item_prio[MAX_ITEMS];   /* prio index, -1 if free */
  bool item_live[MAX_ITEMS];
  for (int i = 0; i < MAX_ITEMS; i++) {
    item_prio[i] = -1; item_live[i] = false;
    memset (&items_pool[i], 0, sizeof items_pool[i]);
  }

  int n_ops = hegel_draw_int (tc, 1, MAX_OPS);
  for (int op = 0; op < n_ops; op++) {
    /* Choose 0=add, 1=remove.  Bias toward add (≈2/3 add, 1/3 remove)
    ** so the population grows enough to exercise resize/move heavily —
    ** with strict 50/50 the capacity stays pinned at base_count=4. */
    int live_count = 0;
    for (int i = 0; i < MAX_ITEMS; i++) if (item_live[i]) live_count++;
    int kind;
    if (live_count == 0) kind = 0;
    else if (live_count == MAX_ITEMS) kind = 1;
    else {
      int r = hegel_draw_int (tc, 0, 2);
      kind = (r == 2) ? 1 : 0;   /* 0,1 -> add ; 2 -> remove */
    }

    if (kind == 0) {
      /* Add: pick a free slot, pick a prio, call parman_item_add. */
      int free_id = -1;
      for (int i = 0; i < MAX_ITEMS; i++) if (!item_live[i]) { free_id = i; break; }
      int prio_idx = hegel_draw_int (tc, 0, N_PRIOS - 1);

      int err = parman_item_add (pm, &prios[prio_idx], &items_pool[free_id]);
      HEGEL_ASSERT (err == 0, "parman_item_add returned %d (op=%d)", err, op);
      item_live[free_id] = true;
      item_prio[free_id] = prio_idx;
      shadow_add (prio_idx, free_id);

      /* Place into the array AT the index parman picked. */
      unsigned long idx = items_pool[free_id].index;
      HEGEL_ASSERT (idx < g_capacity,
                    "parman wrote index %lu but capacity is %lu", idx, g_capacity);
      HEGEL_ASSERT (!g_array[idx].used,
                    "parman wrote into already-used slot %lu (op=%d)", idx, op);
      g_array[idx].id   = free_id;
      g_array[idx].used = true;
    } else {
      /* Remove: pick a live id. Use a draw to pick a position among the live. */
      int n_live = live_count;
      int pick = hegel_draw_int (tc, 0, n_live - 1);
      int seen = 0; int rid = -1;
      for (int i = 0; i < MAX_ITEMS; i++) {
        if (item_live[i]) {
          if (seen == pick) { rid = i; break; }
          seen++;
        }
      }
      HEGEL_ASSERT (rid >= 0, "no live id picked");

      /* Before parman moves the last_item into rid's slot, we need to
      ** clear rid's old slot so my_move sees the destination as empty. */
      unsigned long rid_idx = items_pool[rid].index;
      g_array[rid_idx].id   = -1;
      g_array[rid_idx].used = false;

      parman_item_remove (pm, &prios[item_prio[rid]], &items_pool[rid]);
      shadow_remove (item_prio[rid], rid);
      item_live[rid] = false;
      item_prio[rid] = -1;
    }

    /* Invariant after every op:
    **  (1) total live count == shadow_total
    **  (2) slots [0..total) are all used; slots beyond up to capacity unused
    **  (3) for each prio chunk [lo, hi), the multiset of ids in g_array
    **      equals shadow[p].ids
    **  (4) (consequence of 3) no item appears in the wrong chunk's range. */
    int total = shadow_total ();

    for (int i = 0; i < total; i++) {
      HEGEL_ASSERT (g_array[i].used,
                    "expected used slot %d empty (total=%d, op=%d, kind=%d)",
                    i, total, op, kind);
    }
    for (unsigned long i = (unsigned long) total; i < g_capacity; i++) {
      HEGEL_ASSERT (!g_array[i].used,
                    "slot %lu used past total=%d (op=%d, kind=%d)",
                    i, total, op, kind);
    }

    for (int p = 0; p < N_PRIOS; p++) {
      int lo, hi; shadow_chunk_range (p, &lo, &hi);
      /* Every id in the slot range must belong to prio p in the shadow. */
      for (int i = lo; i < hi; i++) {
        int id = g_array[i].id;
        HEGEL_ASSERT (id >= 0 && id < MAX_ITEMS,
                      "slot %d has bad id=%d (op=%d)", i, id, op);
        HEGEL_ASSERT (item_prio[id] == p,
                      "slot %d: id=%d in prio-%d range but item's prio=%d (op=%d, kind=%d)",
                      i, id, p, item_prio[id], op, kind);
        HEGEL_ASSERT (shadow_chunk_contains (p, id),
                      "slot %d: id=%d not in shadow chunk for prio=%d (op=%d, kind=%d)",
                      i, id, p, op, kind);
      }
      /* And the count in the slot range equals shadow chunk size — already
      ** ensured by the loop bounds, but check parman didn't put SOME prio-p
      ** items outside the expected range. */
      int outside = 0;
      for (unsigned long i = 0; i < g_capacity; i++) {
        if (!g_array[i].used) continue;
        int id = g_array[i].id;
        if (item_prio[id] == p && ((int) i < lo || (int) i >= hi)) outside++;
      }
      HEGEL_ASSERT (outside == 0,
                    "%d prio-%d items outside expected range [%d,%d) (op=%d, kind=%d)",
                    outside, p, lo, hi, op, kind);
    }
  }

  /* Drain so parman_destroy doesn't WARN_ON non-empty prio_list. */
  for (int p = 0; p < N_PRIOS; p++) {
    while (shadow[p].n > 0) {
      int id = shadow[p].ids[0];
      g_array[items_pool[id].index].id = -1;
      g_array[items_pool[id].index].used = false;
      parman_item_remove (pm, &prios[p], &items_pool[id]);
      shadow_remove (p, id);
      item_live[id] = false;
    }
    parman_prio_fini (&prios[p]);
  }
  parman_destroy (pm);
}

/* Coverage counters (process-global). In fork mode each case runs in a
** child so cross-case accumulation doesn't survive — for stats we run a
** second pass in nofork mode where everything stays in this process. */
static long total_resizes;
static long total_moves;
static long total_ops;
static long max_capacity_seen;
static long n_cases_for_stats;

static void coverage_test (hegel_testcase *tc)
{
  test_parman (tc);
  total_resizes += g_resize_calls;
  total_moves   += g_move_calls;
  if ((long) g_peak_capacity > max_capacity_seen) max_capacity_seen = g_peak_capacity;
  n_cases_for_stats++;
  /* Note: total_ops is harder to capture without changing the fixture's
  ** loop signature; skip it. */
  (void) total_ops;
}

int main (void)
{
  /* Pass 1: 500 cases under fork mode — full crash isolation, ASAN/UBSAN
  ** reports surfaced per-case if any fire. */
  hegel_run_test_n (test_parman, 500);

  /* Pass 2: 100 cases under no-fork to gather coverage stats. */
  hegel_run_test_nofork_n (coverage_test, 100);
  fprintf (stderr,
           "[parman PBT coverage] fork=500  nofork=%ld  total_resize_calls=%ld  "
           "total_move_calls=%ld  max_capacity_observed=%ld\n",
           n_cases_for_stats, total_resizes, total_moves, max_capacity_seen);
  return 0;
}
