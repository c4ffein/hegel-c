/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Property: xa_store_range (xa, first, last, entry) returns, and then
** loads at first and at last both give entry.
**
** Layer 1: lib/xarray.c of the pin (real kernel code, compiled by the
**          radix-tree harness) — xa_store_range, xa_load.
** Layer 2: first and last drawn over the whole unsigned long range, so the
**          ends of the index space (0, ULONG_MAX) come up.  A store that
**          does not return is a bug too: each case arms a 250 ms timer
**          whose SIGALRM ends the fork child, which hegel-c reports as a
**          crash and shrinks like any other failure.
** Layer 3: tests/irl/linux/pins/xa_store_range.conf runs it (v7.1: red;
**          with pins/xa_store_range.patch: green).
*/
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>

#include "hegel_c.h"

#include <linux/xarray.h>

extern void rcu_register_thread (void);
extern void radix_tree_init (void);

static void arm_timer (long usec)
{
  struct itimerval t = { .it_value = { .tv_sec = usec / 1000000, .tv_usec = usec % 1000000 } };
  setitimer (ITIMER_REAL, &t, NULL);
}

static void test_store_range (hegel_testcase *tc)
{
  DEFINE_XARRAY (xa);
  unsigned long first = hegel_draw_u64 (tc, 0, UINT64_MAX);
  unsigned long last  = hegel_draw_u64 (tc, first, UINT64_MAX);
  void *        entry = xa_mk_value (hegel_draw_int (tc, 1, 1000));
  char          what[96];

  snprintf (what, sizeof what, "xa_store_range (%lu, %lu)", first, last);
  hegel_note (tc, what);               /* printed even if the store never returns */

  arm_timer (250000);
  void * err = xa_store_range (&xa, first, last, entry, GFP_KERNEL);
  arm_timer (0);

  HEGEL_ASSERT (!xa_is_err (err), "%s failed: %d", what, xa_err (err));
  HEGEL_ASSERT (xa_load (&xa, first) == entry, "%s: xa_load (%lu) is not the entry",
                what, first);
  HEGEL_ASSERT (xa_load (&xa, last) == entry, "%s: xa_load (%lu) is not the entry",
                what, last);
  xa_destroy (&xa);
}

int main (void)
{
  rcu_register_thread ();
  radix_tree_init ();
  hegel_run_test_n (test_store_range, 1000);
  return 0;
}
