/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_shrink_quality/integers.rs::test_minimize_bounded_integers_to_positive
** EXPECTED_SHRINK: 1
** Rust: minimal(
**           integers::<i64>().min_value(-10).max_value(10)
**               .filter(|&x| x != 0),
**           |_| true)
**       == 1
**
** Draw i64 in [-10, 10] filtered to nonzero, always fail.  Hegel's
** simplicity ordering is 0 < 1 < -1 < 2 < -2 < ...; with 0 filtered
** out, the minimal counterexample is 1.
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"

/* filter(x != 0): draw, and reject the case when the predicate fails. */
static
void
test_bounded_positive (
hegel_testcase *    tc)
{
  int64_t             val;

  val = hegel_draw_i64 (tc, -10, 10);
  hegel_assume (tc, val != 0);
  HEGEL_ASSERT (0, "x=%ld", (long) val);
}

int
main (void)
{
  hegel_run_test (test_bounded_positive);
  return (0);
}
