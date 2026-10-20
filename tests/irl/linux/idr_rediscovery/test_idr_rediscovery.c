/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Stateful PBT for lib/idr.c — the kernel IDR (id -> pointer) allocator.
**
** Covers: idr_alloc, idr_alloc_u32, idr_alloc_cyclic, idr_find,
** idr_remove, idr_replace, idr_get_next, idr_init_base / IDR_INIT_BASE,
** idr_is_empty, idr_for_each, idr_for_each_entry.
**
** State machine:
**
**   rule_alloc           — idr_alloc(idr, ptr, start, end, GFP).  Draws
**                          start in [-3, ID_HI_CAP] and end across an
**                          interesting domain that includes 0 (means
**                          INT_MAX), values below start, values much
**                          larger than start, and negative ints.
**                          Shadow records (id -> ptr).
**   rule_alloc_cyclic    — idr_alloc_cyclic with same parameter ranges.
**   rule_alloc_u32       — idr_alloc_u32 with a u32 nextid drawn near
**                          base and a max chosen across a wide u32
**                          domain.
**   rule_find_existing   — pick an allocated id; idr_find must return
**                          the stored ptr.
**   rule_find_missing    — pick an id NOT in the shadow; idr_find must
**                          return NULL.
**   rule_remove          — remove an allocated id; idr_remove must
**                          return the stored ptr; afterwards idr_find
**                          must return NULL.
**   rule_remove_missing  — remove a never-allocated id; idr_remove
**                          must return NULL and not crash.
**   rule_replace         — replace an allocated id with a new pointer;
**                          returned old pointer must equal the prior
**                          one; later idr_find returns the new pointer.
**
** Invariants checked after every rule:
**   1. idr_is_empty() iff shadow empty.
**   2. For every (id, ptr) in shadow: idr_find(id) == ptr.
**   3. For every (id, ptr) in shadow: id >= base, id <= INT_MAX, id >= 0.
**   4. idr_for_each visits exactly the shadow set.
*/

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hegel_c.h"

#include <linux/idr.h>
#include <linux/xarray.h>

/* Provided by radix-tree harness init code. */
extern void rcu_register_thread (void);
extern void radix_tree_init (void);

/* Test parameters. Keep the test ID space small but not tiny so the
** tree grows multiple levels. */
#define MAX_TRACK   128
#define ID_HI_CAP   500

typedef struct {
  int   id;
  void *ptr;
} entry_t;

typedef struct {
  struct idr idr;
  unsigned int base;            /* the configured base of this idr */
  bool initialized;             /* lazy init on first rule */
  entry_t entries[MAX_TRACK];   /* shadow set, sorted by id */
  int n;

  /* Coverage counters. */
  long c_alloc_ok;
  long c_alloc_err;
  long c_alloc_cyc_ok;
  long c_alloc_cyc_err;
  long c_alloc_u32_ok;
  long c_alloc_u32_err;
  long c_find;
  long c_find_missing;
  long c_remove;
  long c_remove_missing;
  long c_replace;
  long c_inv;
} st_t;

/* Aggregate counters across cases. */
static long g_alloc_ok;
static long g_alloc_err;
static long g_alloc_cyc_ok;
static long g_alloc_cyc_err;
static long g_alloc_u32_ok;
static long g_alloc_u32_err;
static long g_find;
static long g_find_missing;
static long g_remove;
static long g_remove_missing;
static long g_replace;
static long g_inv;

/* ---- shadow helpers ---- */

static int
shadow_find_idx (const st_t *s, int id)
{
  /* Sorted by id; small enough that linear scan is fine. */
  for (int i = 0; i < s->n; i++) {
    if (s->entries[i].id == id) return i;
    if (s->entries[i].id > id) return -1;
  }
  return -1;
}

static void
shadow_insert (st_t *s, int id, void *ptr)
{
  HEGEL_ASSERT (s->n < MAX_TRACK,
                "shadow_insert: full (n=%d) — increase MAX_TRACK", s->n);
  HEGEL_ASSERT (shadow_find_idx (s, id) < 0,
                "shadow_insert: duplicate id=%d", id);
  int i = s->n;
  while (i > 0 && s->entries[i - 1].id > id) {
    s->entries[i] = s->entries[i - 1];
    i--;
  }
  s->entries[i].id = id;
  s->entries[i].ptr = ptr;
  s->n++;
}

static void
shadow_remove_at (st_t *s, int idx)
{
  memmove (&s->entries[idx], &s->entries[idx + 1],
           (s->n - idx - 1) * sizeof (entry_t));
  s->n--;
}

/* Produce a non-NULL distinct pointer for each call. We use a monotonic
** counter cast to a pointer — values are tagged in the low bits with 1
** to avoid colliding with the xarray's internal-entry encoding (the
** xarray reserves a few low bits as flags). Pointers are never
** dereferenced in this test. */
static void *
make_ptr (st_t *s)
{
  /* Use a per-instance counter so successive draws within a case get
  ** distinct pointers (lets idr_replace's old-ptr check be meaningful). */
  (void) s;
  static uintptr_t global_counter = 0x1000;
  uintptr_t v = global_counter += 16;
  return (void *) v;
}

/* ---- setup / teardown ---- */

static void *
idr_setup (void)
{
  st_t *s = calloc (1, sizeof (*s));
  if (!s) return NULL;
  /* base & init are deferred to the first rule so they can be drawn by
  ** hegel and shrunk. We initialize with base=0 here as a safety net in
  ** case some path queries the idr before any rule runs. */
  s->base = 0;
  s->initialized = false;
  idr_init_base (&s->idr, 0);
  return s;
}

/* Lazy init: every rule starts with this so the base is drawn from hegel
** (and therefore shrinkable). After the first rule, subsequent rules
** keep the same base. */
static void
ensure_init (st_t *s, hegel_testcase *tc)
{
  if (s->initialized) return;
  /* Draw base across an interesting domain. */
  int pick = hegel_draw_int (tc, 0, 5);
  unsigned int b;
  switch (pick) {
  case 0: b = 0; break;
  case 1: b = 1; break;
  case 2: b = (unsigned int) hegel_draw_int (tc, 2, 100); break;
  case 3: b = (unsigned int) hegel_draw_int (tc, 100, 100000); break;
  case 4: b = (unsigned int) (INT_MAX - hegel_draw_int (tc, 0, 1000)); break;
  default: b = (unsigned int) hegel_draw_int (tc, 0, INT_MAX); break;
  }
  s->base = b;
  /* Re-init (idr is fresh from setup, but idr_destroy + idr_init_base is
  ** safe). */
  idr_destroy (&s->idr);
  idr_init_base (&s->idr, (int) b);
  s->initialized = true;
}

static void
idr_teardown (void *state)
{
  if (!state) return;
  st_t *s = state;
  g_alloc_ok        += s->c_alloc_ok;
  g_alloc_err       += s->c_alloc_err;
  g_alloc_cyc_ok    += s->c_alloc_cyc_ok;
  g_alloc_cyc_err   += s->c_alloc_cyc_err;
  g_alloc_u32_ok    += s->c_alloc_u32_ok;
  g_alloc_u32_err   += s->c_alloc_u32_err;
  g_find            += s->c_find;
  g_find_missing    += s->c_find_missing;
  g_remove          += s->c_remove;
  g_remove_missing  += s->c_remove_missing;
  g_replace         += s->c_replace;
  g_inv             += s->c_inv;
  idr_destroy (&s->idr);
  free (s);
}

/* ---- preconditions ---- */

static bool
have_entries (const void *state)
{
  return ((const st_t *) state)->n > 0;
}

static bool
have_room (const void *state)
{
  return ((const st_t *) state)->n < MAX_TRACK;
}

/* ---- helpers ---- */

/* Draw a "start" value that includes negatives (illegal, expect EINVAL),
** zero, small, and the base region. */
static int
draw_start (hegel_testcase *tc, const st_t *s)
{
  /* Spread across: -3..-1 (illegal), 0..ID_HI_CAP, near base, near INT_MAX. */
  int pick = hegel_draw_int (tc, 0, 6);
  switch (pick) {
  case 0: return -3 + hegel_draw_int (tc, 0, 2);             /* negative */
  case 1: return 0;
  case 2: return hegel_draw_int (tc, 0, ID_HI_CAP);
  case 3: {
    /* Near base — covers base-1, base, base+small. */
    int delta = hegel_draw_int (tc, -3, ID_HI_CAP);
    long v = (long) s->base + delta;
    if (v < INT_MIN) v = INT_MIN;
    if (v > INT_MAX) v = INT_MAX;
    return (int) v;
  }
  case 4: return INT_MAX - hegel_draw_int (tc, 0, 200);
  case 5: return hegel_draw_int (tc, 0, 100000);
  default: return hegel_draw_int (tc, INT_MIN, INT_MAX);
  }
}

/* Draw an "end" value that covers: 0 (means INT_MAX), negative (also
** treated as INT_MAX), 1..high, and values *below* a typical start
** (which is the interesting under-flow corner). */
static int
draw_end (hegel_testcase *tc, int start, const st_t *s)
{
  int pick = hegel_draw_int (tc, 0, 6);
  switch (pick) {
  case 0: return 0;                                          /* means INT_MAX */
  case 1: return -1 - hegel_draw_int (tc, 0, 3);             /* negative => INT_MAX */
  case 2: {
    /* end slightly above start. */
    long v = (long) start + hegel_draw_int (tc, 1, 32);
    if (v > INT_MAX) v = INT_MAX;
    return (int) v;
  }
  case 3: {
    /* end much above start. */
    long v = (long) start + hegel_draw_int (tc, 100, 100000);
    if (v > INT_MAX) v = INT_MAX;
    return (int) v;
  }
  case 4: {
    /* end *below* start (interesting: under-flow / empty-range case). */
    long v = (long) start - hegel_draw_int (tc, 1, 50);
    if (v < INT_MIN) v = INT_MIN;
    return (int) v;
  }
  case 5: {
    /* end below base. */
    long v = (long) s->base - hegel_draw_int (tc, 1, 50);
    if (v < INT_MIN) v = INT_MIN;
    return (int) v;
  }
  default: return hegel_draw_int (tc, INT_MIN, INT_MAX);
  }
}

/* Pick a "missing" id (not in shadow). Returns id or INT_MIN if all
** small candidates are taken (caller should treat that as "skip"). */
static int
draw_missing_id (hegel_testcase *tc, const st_t *s)
{
  long hi = (long) s->base + ID_HI_CAP + 100;
  if (hi > INT_MAX) hi = INT_MAX;
  for (int attempt = 0; attempt < 16; attempt++) {
    int candidate = hegel_draw_int (tc, 0, (int) hi);
    if (shadow_find_idx (s, candidate) < 0) return candidate;
  }
  return INT_MIN;
}

/* ---- rules ---- */

static void
rule_alloc (void *state, hegel_testcase *tc)
{
  st_t *s = state;
  ensure_init (s, tc);
  int start = draw_start (tc, s);
  int end = draw_end (tc, start, s);
  void *ptr = make_ptr (s);

  int got = idr_alloc (&s->idr, ptr, start, end, GFP_KERNEL);

  if (got >= 0) {
    /* Postcondition: ID must be in the requested range AND non-negative
    ** AND >= base (idr stores at id - base, so id < base would underflow). */
    HEGEL_ASSERT (got >= 0,
                  "idr_alloc returned negative id=%d (not an error code)", got);
    HEGEL_ASSERT (got >= start,
                  "idr_alloc(base=%u, start=%d, end=%d) returned id=%d < start",
                  s->base, start, end, got);
    if (end > 0) {
      HEGEL_ASSERT (got < end,
                    "idr_alloc(base=%u, start=%d, end=%d) returned id=%d >= end",
                    s->base, start, end, got);
    } else {
      /* end <= 0 means INT_MAX as upper inclusive. got <= INT_MAX
      ** trivially since got is int and >= 0. */
    }
    HEGEL_ASSERT ((unsigned int) got >= s->base,
                  "idr_alloc(base=%u, start=%d, end=%d) returned id=%d < base",
                  s->base, start, end, got);
    HEGEL_ASSERT (shadow_find_idx (s, got) < 0,
                  "idr_alloc returned duplicate id=%d", got);
    shadow_insert (s, got, ptr);
    s->c_alloc_ok++;
  } else {
    /* Errors should be -EINVAL (start<0), -ENOSPC, or -ENOMEM. */
    HEGEL_ASSERT (got == -EINVAL || got == -ENOSPC || got == -ENOMEM,
                  "idr_alloc returned unexpected error %d for start=%d end=%d",
                  got, start, end);
    /* Document the EINVAL case: kernel returns -EINVAL iff start < 0. */
    if (start < 0) {
      HEGEL_ASSERT (got == -EINVAL,
                    "idr_alloc(start=%d, end=%d) should return EINVAL, got %d",
                    start, end, got);
    }
    s->c_alloc_err++;
  }
}

static void
rule_alloc_cyclic (void *state, hegel_testcase *tc)
{
  st_t *s = state;
  ensure_init (s, tc);
  int start = draw_start (tc, s);
  int end = draw_end (tc, start, s);
  void *ptr = make_ptr (s);

  int got = idr_alloc_cyclic (&s->idr, ptr, start, end, GFP_KERNEL);

  if (got >= 0) {
    HEGEL_ASSERT (got >= 0,
                  "idr_alloc_cyclic returned negative id=%d", got);
    if (end > 0) {
      HEGEL_ASSERT (got < end,
                    "idr_alloc_cyclic(start=%d, end=%d) returned id=%d >= end",
                    start, end, got);
    }
    /* idr_alloc_cyclic does NOT guarantee id >= start (it wraps), but
    ** it should guarantee id >= base and id >= 0. */
    HEGEL_ASSERT ((unsigned int) got >= s->base,
                  "idr_alloc_cyclic id=%d < base=%u", got, s->base);
    HEGEL_ASSERT (shadow_find_idx (s, got) < 0,
                  "idr_alloc_cyclic returned duplicate id=%d", got);
    shadow_insert (s, got, ptr);
    s->c_alloc_cyc_ok++;
  } else {
    HEGEL_ASSERT (got == -EINVAL || got == -ENOSPC || got == -ENOMEM,
                  "idr_alloc_cyclic returned unexpected error %d", got);
    s->c_alloc_cyc_err++;
  }
}

static void
rule_alloc_u32 (void *state, hegel_testcase *tc)
{
  st_t *s = state;
  ensure_init (s, tc);
  /* Draw nextid across the unsigned int domain: 0, base region, large. */
  u32 nextid;
  int pick = hegel_draw_int (tc, 0, 4);
  switch (pick) {
  case 0: nextid = 0; break;
  case 1: nextid = (u32) hegel_draw_int (tc, 0, ID_HI_CAP); break;
  case 2: {
    /* near base */
    long v = (long) s->base + hegel_draw_int (tc, -3, ID_HI_CAP);
    if (v < 0) v = 0;
    if (v > (long) UINT_MAX) v = UINT_MAX;
    nextid = (u32) v;
    break;
  }
  case 3: nextid = (u32) hegel_draw_int (tc, 0, 1000000); break;
  default: nextid = (u32) (uint32_t) hegel_draw_int (tc, INT_MIN, INT_MAX); break;
  }
  /* Draw max: low values (may underflow), values around base, INT_MAX-ish,
  ** UINT_MAX. */
  unsigned long max;
  pick = hegel_draw_int (tc, 0, 5);
  switch (pick) {
  case 0: max = 0; break;
  case 1: max = (unsigned long) hegel_draw_int (tc, 1, ID_HI_CAP); break;
  case 2: {
    long v = (long) s->base + hegel_draw_int (tc, -3, ID_HI_CAP);
    if (v < 0) v = 0;
    max = (unsigned long) v;
    break;
  }
  case 3: max = (unsigned long) INT_MAX; break;
  case 4: max = UINT_MAX; break;
  default: max = (unsigned long) hegel_draw_int (tc, 1, 1000000); break;
  }

  void *ptr = make_ptr (s);
  u32 saved_nextid = nextid;
  int ret = idr_alloc_u32 (&s->idr, ptr, &nextid, max, GFP_KERNEL);

  if (ret == 0) {
    /* nextid was overwritten with the allocated id. */
    /* Postconditions: */
    HEGEL_ASSERT ((unsigned long) nextid <= max,
                  "idr_alloc_u32 stored id=%u > max=%lu (saved_nextid=%u, base=%u)",
                  nextid, max, saved_nextid, s->base);
    HEGEL_ASSERT (nextid >= s->base,
                  "idr_alloc_u32 stored id=%u < base=%u", nextid, s->base);
    /* The function should respect the input nextid as a lower bound (post
    ** the (id < base ? 0 : id - base) normalization). */
    if (saved_nextid >= s->base) {
      HEGEL_ASSERT (nextid >= saved_nextid,
                    "idr_alloc_u32 stored id=%u < requested nextid=%u (base=%u)",
                    nextid, saved_nextid, s->base);
    }
    /* Shadow uses int keys; if nextid > INT_MAX (only possible via
    ** idr_alloc_u32 with a large max), don't track. The other rules use
    ** idr_alloc which returns int. */
    if (nextid <= (u32) INT_MAX) {
      int got = (int) nextid;
      HEGEL_ASSERT (shadow_find_idx (s, got) < 0,
                    "idr_alloc_u32 returned duplicate id=%d", got);
      shadow_insert (s, got, ptr);
      s->c_alloc_u32_ok++;
    } else {
      /* Untracked: immediately remove via idr_remove with the ul id so
      ** we don't leak. */
      void *removed = idr_remove (&s->idr, (unsigned long) nextid);
      HEGEL_ASSERT (removed == ptr,
                    "idr_remove(id=%u > INT_MAX) returned %p, expected %p",
                    nextid, removed, ptr);
      s->c_alloc_u32_ok++;
    }
  } else {
    /* On error, nextid should be unchanged per the docstring. */
    HEGEL_ASSERT (nextid == saved_nextid,
                  "idr_alloc_u32 error path mutated nextid: was %u, now %u",
                  saved_nextid, nextid);
    HEGEL_ASSERT (ret == -ENOSPC || ret == -ENOMEM || ret == -EINVAL,
                  "idr_alloc_u32 returned unexpected error %d", ret);
    s->c_alloc_u32_err++;
  }
}

static void
rule_find_existing (void *state, hegel_testcase *tc)
{
  st_t *s = state;
  int pick = hegel_draw_int (tc, 0, s->n - 1);
  int id = s->entries[pick].id;
  void *expected = s->entries[pick].ptr;
  void *got = idr_find (&s->idr, (unsigned long) id);
  HEGEL_ASSERT (got == expected,
                "idr_find(id=%d) returned %p, expected %p", id, got, expected);
  s->c_find++;
}

static void
rule_find_missing (void *state, hegel_testcase *tc)
{
  st_t *s = state;
  ensure_init (s, tc);
  int id = draw_missing_id (tc, s);
  if (id == INT_MIN) return;
  void *got = idr_find (&s->idr, (unsigned long) id);
  HEGEL_ASSERT (got == NULL,
                "idr_find(missing id=%d) returned non-NULL %p", id, got);
  s->c_find_missing++;
}

static void
rule_remove (void *state, hegel_testcase *tc)
{
  st_t *s = state;
  int pick = hegel_draw_int (tc, 0, s->n - 1);
  int id = s->entries[pick].id;
  void *expected = s->entries[pick].ptr;
  void *got = idr_remove (&s->idr, (unsigned long) id);
  HEGEL_ASSERT (got == expected,
                "idr_remove(id=%d) returned %p, expected %p (the stored ptr)",
                id, got, expected);
  shadow_remove_at (s, pick);
  /* After removal, find must return NULL. */
  void *found = idr_find (&s->idr, (unsigned long) id);
  HEGEL_ASSERT (found == NULL,
                "idr_find after idr_remove(id=%d) returned %p, expected NULL",
                id, found);
  s->c_remove++;
}

static void
rule_remove_missing (void *state, hegel_testcase *tc)
{
  st_t *s = state;
  ensure_init (s, tc);
  int id = draw_missing_id (tc, s);
  if (id == INT_MIN) return;
  void *got = idr_remove (&s->idr, (unsigned long) id);
  HEGEL_ASSERT (got == NULL,
                "idr_remove(missing id=%d) returned non-NULL %p", id, got);
  s->c_remove_missing++;
}

static void
rule_replace (void *state, hegel_testcase *tc)
{
  st_t *s = state;
  int pick = hegel_draw_int (tc, 0, s->n - 1);
  int id = s->entries[pick].id;
  void *old_expected = s->entries[pick].ptr;
  void *new_ptr = make_ptr (s);
  /* Skip when the new pointer would equal the old (very rare, but avoid
  ** false positives in the post-find check). */
  if (new_ptr == old_expected) return;

  void *old = idr_replace (&s->idr, new_ptr, (unsigned long) id);
  HEGEL_ASSERT (!IS_ERR (old),
                "idr_replace(id=%d) returned error %ld", id, PTR_ERR (old));
  HEGEL_ASSERT (old == old_expected,
                "idr_replace(id=%d) returned old=%p, expected %p",
                id, old, old_expected);
  s->entries[pick].ptr = new_ptr;
  /* Round-trip: find must now return the new pointer. */
  void *found = idr_find (&s->idr, (unsigned long) id);
  HEGEL_ASSERT (found == new_ptr,
                "idr_find after idr_replace(id=%d) returned %p, expected %p",
                id, found, new_ptr);
  s->c_replace++;
}

/* ---- invariants ---- */

/* idr_for_each callback: collect (id, ptr) pairs into a context array. */
typedef struct {
  entry_t buf[MAX_TRACK + 8];   /* a few extra so overflow is detected */
  int n;
  int overflow;
} collect_ctx_t;

static int
collect_cb (int id, void *p, void *data)
{
  collect_ctx_t *c = data;
  if (c->n >= (int) (sizeof (c->buf) / sizeof (c->buf[0]))) {
    c->overflow = 1;
    return 1;                   /* stop early */
  }
  c->buf[c->n].id = id;
  c->buf[c->n].ptr = p;
  c->n++;
  return 0;
}

static void
inv_consistent (const void *cstate, hegel_testcase *tc)
{
  (void) tc;
  st_t *s = (st_t *) cstate;
  s->c_inv++;

  /* (1) is_empty iff shadow empty. */
  bool sut_empty = idr_is_empty (&s->idr);
  bool shadow_empty = (s->n == 0);
  HEGEL_ASSERT (sut_empty == shadow_empty,
                "idr_is_empty=%d but shadow_n=%d", sut_empty, s->n);

  /* (2) round-trip: every shadow id finds the right ptr. */
  for (int i = 0; i < s->n; i++) {
    int id = s->entries[i].id;
    void *expected = s->entries[i].ptr;
    void *got = idr_find (&s->idr, (unsigned long) id);
    HEGEL_ASSERT (got == expected,
                  "invariant: idr_find(id=%d) = %p, expected %p (base=%u)",
                  id, got, expected, s->base);
  }

  /* (3) every shadow id is in [base, INT_MAX]. */
  for (int i = 0; i < s->n; i++) {
    int id = s->entries[i].id;
    HEGEL_ASSERT (id >= 0,
                  "invariant: shadow id=%d is negative", id);
    HEGEL_ASSERT ((unsigned int) id >= s->base,
                  "invariant: shadow id=%d < base=%u", id, s->base);
  }

  /* (4) idr_for_each visits exactly the shadow set. */
  collect_ctx_t c = {0};
  int rc = idr_for_each (&s->idr, collect_cb, &c);
  HEGEL_ASSERT (rc == 0,
                "idr_for_each returned %d (overflow=%d, n=%d, shadow_n=%d)",
                rc, c.overflow, c.n, s->n);
  HEGEL_ASSERT (!c.overflow,
                "idr_for_each visited > shadow_n (overflow)");
  HEGEL_ASSERT (c.n == s->n,
                "idr_for_each visited %d entries, shadow has %d", c.n, s->n);
  /* The callback is invoked in id-ascending order; shadow is sorted too. */
  for (int i = 0; i < c.n; i++) {
    HEGEL_ASSERT (c.buf[i].id == s->entries[i].id &&
                  c.buf[i].ptr == s->entries[i].ptr,
                  "idr_for_each idx=%d got (%d,%p), shadow has (%d,%p)",
                  i, c.buf[i].id, c.buf[i].ptr,
                  s->entries[i].id, s->entries[i].ptr);
  }
}

/* ---- main ---- */

int
main (void)
{
  rcu_register_thread ();
  radix_tree_init ();

  printf ("idr stateful PBT (rediscovery)...\n");

  hegel_state_machine *sm = hegel_state_machine_new (idr_setup, idr_teardown);
  if (!sm) { fprintf (stderr, "state_machine_new failed\n"); return 1; }

#define ADD_RULE(name_, pre_, fn_) \
  if (hegel_state_machine_add_rule_with_precondition (sm, name_, pre_, fn_) != 0) \
    { fprintf (stderr, "add_rule %s failed\n", name_); return 1; }
#define ADD_RULE_NOPRE(name_, fn_) \
  if (hegel_state_machine_add_rule (sm, name_, fn_) != 0) \
    { fprintf (stderr, "add_rule %s failed\n", name_); return 1; }

  ADD_RULE ("alloc",          have_room, rule_alloc);
  ADD_RULE ("alloc_cyclic",   have_room, rule_alloc_cyclic);
  ADD_RULE ("alloc_u32",      have_room, rule_alloc_u32);
  ADD_RULE ("find_existing",  have_entries, rule_find_existing);
  ADD_RULE_NOPRE ("find_missing", rule_find_missing);
  ADD_RULE ("remove",         have_entries, rule_remove);
  ADD_RULE_NOPRE ("remove_missing", rule_remove_missing);
  ADD_RULE ("replace",        have_entries, rule_replace);

  if (hegel_state_machine_add_invariant (sm, "consistent", inv_consistent) != 0)
    return 1;

  hegel_state_machine_run_n (sm, 200);

  printf ("PASSED   alloc=%ld/%ld cyc=%ld/%ld u32=%ld/%ld "
          "find=%ld find_miss=%ld remove=%ld remove_miss=%ld "
          "replace=%ld inv=%ld\n",
          g_alloc_ok, g_alloc_err,
          g_alloc_cyc_ok, g_alloc_cyc_err,
          g_alloc_u32_ok, g_alloc_u32_err,
          g_find, g_find_missing,
          g_remove, g_remove_missing,
          g_replace, g_inv);

  hegel_state_machine_free (sm);
  return 0;
}
