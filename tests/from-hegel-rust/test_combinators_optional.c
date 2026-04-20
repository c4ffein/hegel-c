/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_combinators.rs::test_optional_respects_inner_generator_bounds
** Equivalent of:
**   let value = tc.draw(gs::optional(gs::integers().min_value(10).max_value(20)));
**   if let Some(n) = value { assert!((10..=20).contains(&n)); }
**
** Schema-API translation of the original combinator-API port.
**
** Runs 2 cases in nofork mode so a file-scope counter can aggregate
** across test cases (fork mode would lose the counter in the child).
** Asserts both branches of optional() are exercised — at least one
** present and at least one absent — to catch a regression that
** collapses optional() to a single branch.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"
#include "hegel_gen.h"

#define N_CASES   2

typedef struct {
  int *               maybe_val;
} Opt;

static hegel_schema_t  opt_schema;
static int             n_total;
static int             n_present;

static
void
test_optional (
hegel_testcase *            tc)
{
  Opt *                t;
  hegel_shape *        sh;

  sh = hegel_schema_draw (tc, opt_schema, (void **) &t);

  n_total++;
  if (t->maybe_val != NULL) {
    n_present++;
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
  hegel_run_test_nofork_n (test_optional, N_CASES);
  hegel_schema_free (opt_schema);

  if (n_present == 0 || n_present == n_total) {
    fprintf (stderr,
             "optional collapsed to one branch: %d/%d present\n",
             n_present, n_total);
    return (1);
  }
  return (0);
}
