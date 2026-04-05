/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_integers.rs::test_i64
** Equivalent of: assert_all_examples(gs::integers::<i64>(), |&n| n >= i64::MIN && n <= i64::MAX)
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"

static
void
test_i64_bounds (
hegel_testcase *            tc)
{
  int64_t             n;

  n = hegel_draw_i64 (tc, INT64_MIN, INT64_MAX);
  HEGEL_ASSERT (n >= INT64_MIN && n <= INT64_MAX,
                "n=%ld out of [INT64_MIN, INT64_MAX]", (long) n);
}

int
main (void)
{
  hegel_run_test (test_i64_bounds);
  return (0);
}
