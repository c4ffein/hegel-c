/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Property: ida_find_first_range (ida, min, max) returns the lowest
** allocated ID in [min, max], or -ENOENT when there is none.
**
** Layer 1: lib/idr.c of the pin (real kernel code, compiled by the
**          radix-tree harness) — ida_alloc_range, ida_find_first_range.
** Layer 2: allocate a drawn set of IDs spread over four bitmap blocks
**          (IDA_BITMAP_BITS = 1024 IDs each), draw a range, and compare
**          the answer with a shadow of the set.  test_ida_stateful checks
**          the same thing with IDs below 200, i.e. inside block 0 only.
** Layer 3: tests/irl/linux/pins/ida_find_first_range.conf runs it (v7.1:
**          red, no fix exists yet).
*/
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "hegel_c.h"

#include <linux/idr.h>

extern void rcu_register_thread (void);
extern void radix_tree_init (void);

#define ID_SPACE  (4 * 1024)      /* four IDA bitmap blocks */
#define MAX_IDS   16

static void test_find_first_range (hegel_testcase *tc)
{
  DEFINE_IDA (ida);
  bool * held = hegel_alloc (tc, ID_SPACE * sizeof *held);
  char   ids[MAX_IDS * 8 + 1] = "";
  int    n = hegel_draw_int (tc, 0, MAX_IDS);

  for (int i = 0; i < n; i++) {
    int id = hegel_draw_int (tc, 0, ID_SPACE - 1);
    if (held[id])
      continue;
    int got = ida_alloc_range (&ida, id, id, GFP_KERNEL);
    HEGEL_ASSERT (got == id, "ida_alloc_range (%d, %d) = %d", id, id, got);
    held[id] = true;
    snprintf (ids + strlen (ids), sizeof ids - strlen (ids), " %d", id);
  }

  int min = hegel_draw_int (tc, 0, ID_SPACE - 1);
  int max = hegel_draw_int (tc, min, ID_SPACE - 1);

  int want = -ENOENT;
  for (int id = min; id <= max; id++)
    if (held[id]) {
      want = id;
      break;
    }

  int got = ida_find_first_range (&ida, min, max);
  HEGEL_ASSERT (got == want, "IDs {%s }: ida_find_first_range (%d, %d) = %d, want %d",
                ids, min, max, got, want);
  ida_destroy (&ida);
}

int main (void)
{
  rcu_register_thread ();
  radix_tree_init ();
  hegel_run_test_n (test_find_first_range, 1000);
  return 0;
}
