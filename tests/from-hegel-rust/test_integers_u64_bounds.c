/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_integers.rs::test_u64
** Equivalent of: assert_all_examples(gs::integers::<u64>(), |&n| n >= u64::MIN && n <= u64::MAX)
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"

static
void
test_u64_bounds (
hegel_testcase *            tc)
{
  uint64_t            n;

  n = hegel_draw_u64 (tc, 0, UINT64_MAX);
  HEGEL_ASSERT (n <= UINT64_MAX,
                "n=%lu out of [0, UINT64_MAX]", (unsigned long) n);
}

int
main (void)
{
  hegel_run_test (test_u64_bounds);
  return (0);
}
