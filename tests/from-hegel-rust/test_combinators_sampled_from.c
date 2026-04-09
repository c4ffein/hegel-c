/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_combinators.rs::test_sampled_from_returns_element_from_list
** Rust test does:
**   let list = tc.draw(vecs(integers::<i32>()).min_size(1));
**   let elem = tc.draw(sampled_from(&list));
**   assert!(list.contains(&elem));
**
** C port: draws a list of ints, then uses hegel_gen_sampled_from to
** pick an index, verifies the value at that index is in the list.
** Adapted: Rust's sampled_from(&list) returns an element directly;
** C's hegel_gen_sampled_from(count) returns an index.  Same property.
*/
#include <stdio.h>
#include <stdint.h>
#include <limits.h>

#include "hegel_c.h"

/* Layer 1: identity — verify sampled element is in list */

/* Layer 2: property test */

#define MAX_LIST 200

static void test_sampled_from (hegel_testcase * tc) {
  /* Draw a list of ints with length in [1, MAX_LIST].
  ** Rust uses vecs(integers::<i32>()).min_size(1) — unbounded max,
  ** but C needs a fixed buffer.  MAX_LIST is generous enough. */
  hegel_gen * elem_gen = hegel_gen_int (INT_MIN, INT_MAX);
  int buf[MAX_LIST];
  int len = hegel_gen_draw_list_int (tc, elem_gen, 1, MAX_LIST, buf, MAX_LIST);
  hegel_gen_free (elem_gen);

  /* Sample an index from [0, len) */
  hegel_gen * idx_gen = hegel_gen_sampled_from (len);
  int idx = hegel_gen_draw_int (tc, idx_gen);
  hegel_gen_free (idx_gen);

  int sampled = buf[idx];

  /* Verify the sampled value is in the list */
  int found = 0;
  for (int i = 0; i < len; i++) {
    if (buf[i] == sampled) {
      found = 1;
      break;
    }
  }
  HEGEL_ASSERT (found, "sampled %d not found in list of length %d", sampled, len);
}

/* Layer 3: runner */

int main (void) {
  if (hegel_run_test_result (test_sampled_from) != 0) {
    fprintf (stderr, "ERROR: sampled_from test should pass\n");
    return (1);
  }
  return (0);
}
