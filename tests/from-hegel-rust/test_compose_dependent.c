/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_compose.rs::test_compose_dependent_generation
** Rust test does:
**   let (x, y) = tc.draw(compose!({
**     let x = draw(integers::<i32>().min_value(0).max_value(100));
**     let y = draw(integers::<i32>().min_value(x).max_value(100));
**     (x, y)
**   }));
**   assert!(y >= x);
**   assert!(x >= 0 && x <= 100);
**   assert!(y >= 0 && y <= 100);
**
** C port: uses two sequential hegel_draw_int calls where the second
** depends on the first.  This is how compose/dependent generation
** works in C — just multiple draws in sequence.
*/
#include <stdio.h>

#include "hegel_c.h"

/* Layer 1: identity — draw dependent pair */

/* Layer 2: property test */

static void test_dependent (hegel_testcase * tc) {
  int x = hegel_draw_int (tc, 0, 50);
  int y = hegel_draw_int (tc, x, 100);
  HEGEL_ASSERT (y >= x, "y=%d < x=%d", y, x);
  HEGEL_ASSERT (x >= 0 && x <= 50, "x=%d out of [0, 50]", x);
  HEGEL_ASSERT (y >= 0 && y <= 100, "y=%d out of [0, 100]", y);
}

/* Also test: draw a list, then draw a valid index into it.
** Rust: test_compose_list_with_index */
static void test_list_with_index (hegel_testcase * tc) {
  hegel_gen * elem_gen = hegel_gen_int (0, 999);
  int buf[50];
  int len = hegel_gen_draw_list_int (tc, elem_gen, 1, 50, buf, 50);
  hegel_gen_free (elem_gen);

  int idx = hegel_draw_int (tc, 0, len - 1);
  HEGEL_ASSERT (idx >= 0 && idx < len,
                "idx=%d out of [0, %d)", idx, len);
  HEGEL_ASSERT (buf[idx] >= 0 && buf[idx] <= 999,
                "buf[%d]=%d out of [0, 999]", idx, buf[idx]);
}

/* Layer 3: runner */

int main (void) {
  int errors = 0;

  if (hegel_run_test_result (test_dependent) != 0) {
    fprintf (stderr, "ERROR: dependent generation test should pass\n");
    errors++;
  }
  if (hegel_run_test_result (test_list_with_index) != 0) {
    fprintf (stderr, "ERROR: list_with_index test should pass\n");
    errors++;
  }

  if (errors > 0) {
    fprintf (stderr, "%d test(s) failed\n", errors);
    return (1);
  }
  return (0);
}
