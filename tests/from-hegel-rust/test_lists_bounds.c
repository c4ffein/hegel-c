/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_collections.rs::test_vec_with_min_and_max_size
** Rust test does:
**   draw min_size in [0, 10], max_size >= min_size
**   draw vec with both bounds, assert min <= len <= max
** Also covers test_vec_with_max_size and test_vec_with_min_size.
*/
#include <stdio.h>

#include "hegel_c.h"

#define BUF_CAP 64

/* Layer 2: property tests */

static void test_list_max (hegel_testcase * tc) {
  int max_len = hegel_draw_int (tc, 0, 20);
  hegel_gen * g = hegel_gen_int (-100, 100);
  int buf[BUF_CAP];
  int len = hegel_gen_draw_list_int (tc, g, 0, max_len, buf, BUF_CAP);
  hegel_gen_free (g);
  HEGEL_ASSERT (len <= max_len,
                "len=%d > max_len=%d", len, max_len);
}

static void test_list_min (hegel_testcase * tc) {
  int min_len = hegel_draw_int (tc, 0, 20);
  hegel_gen * g = hegel_gen_int (-100, 100);
  int buf[BUF_CAP];
  int len = hegel_gen_draw_list_int (tc, g, min_len, BUF_CAP, buf, BUF_CAP);
  hegel_gen_free (g);
  HEGEL_ASSERT (len >= min_len,
                "len=%d < min_len=%d", len, min_len);
}

static void test_list_min_max (hegel_testcase * tc) {
  int min_len = hegel_draw_int (tc, 0, 10);
  int max_len = hegel_draw_int (tc, min_len, min_len + 20);
  hegel_gen * g = hegel_gen_int (-100, 100);
  int buf[BUF_CAP];
  int len = hegel_gen_draw_list_int (tc, g, min_len, max_len, buf, BUF_CAP);
  hegel_gen_free (g);
  HEGEL_ASSERT (len >= min_len && len <= max_len,
                "len=%d not in [%d, %d]", len, min_len, max_len);
}

/* Layer 3: runner */

int main (void) {
  int errors = 0;

  if (hegel_run_test_result (test_list_max) != 0) {
    fprintf (stderr, "ERROR: list max size check failed\n");
    errors++;
  }
  if (hegel_run_test_result (test_list_min) != 0) {
    fprintf (stderr, "ERROR: list min size check failed\n");
    errors++;
  }
  if (hegel_run_test_result (test_list_min_max) != 0) {
    fprintf (stderr, "ERROR: list min+max size check failed\n");
    errors++;
  }

  if (errors > 0) {
    fprintf (stderr, "%d check(s) failed\n", errors);
    return (1);
  }
  return (0);
}
