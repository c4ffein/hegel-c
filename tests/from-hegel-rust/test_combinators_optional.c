/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_combinators.rs::test_optional_respects_inner_generator_bounds
** Equivalent of:
**   let value = tc.draw(gs::optional(gs::integers().min_value(10).max_value(20)));
**   if let Some(n) = value { assert!((10..=20).contains(&n)); }
**
** Schema-API translation of the original combinator-API port.
*/
#include <stdio.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  int *               maybe_val;
} Opt;

static hegel_schema_t  opt_schema;

static
void
test_optional (
hegel_testcase *            tc)
{
  Opt *                t;
  hegel_shape *        sh;

  sh = hegel_schema_draw (tc, opt_schema, (void **) &t);

  if (t->maybe_val != NULL) {
    HEGEL_ASSERT (*t->maybe_val >= 10 && *t->maybe_val <= 20,
                  "optional(int(10,20)) produced %d", *t->maybe_val);
  }

  hegel_shape_free (sh);
}

int
main (void)
{
  opt_schema = HEGEL_STRUCT (Opt,
      HEGEL_OPTIONAL (HEGEL_INT (10, 20)));
  hegel_run_test (test_optional);
  hegel_schema_free (opt_schema);
  return (0);
}
