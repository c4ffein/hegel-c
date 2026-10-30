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
#include "hegel_gen.h"

static
int
is_nonzero (
int64_t             val,
void *              ctx)
{
  (void) ctx;
  return (val != 0);
}

static hegel_schema_t  filter_schema;

static
void
test_bounded_positive (
hegel_testcase *    tc)
{
  int64_t             val = 0;
  hegel_shape *       sh;

  sh = HEGEL_DRAW (&val, filter_schema);
  HEGEL_ASSERT (0, "x=%ld", (long) val);
  hegel_shape_free (sh);
}

int
main (void)
{
  filter_schema = HEGEL_FILTER_I64 (
      HEGEL_I64 (-10, 10), is_nonzero, NULL);
  hegel_run_test (test_bounded_positive);
  hegel_schema_free (filter_schema);
  return (0);
}
