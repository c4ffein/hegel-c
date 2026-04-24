/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_BINDING / HEGEL_LET / HEGEL_USE — Stage 1 prototype.
**
** Same-struct scope only.  A Pair { int n; int copy_of_n; } where
** copy_of_n is read from the same binding n drew into.  After every
** draw: n == copy_of_n (always), and 2 <= n <= 5 (HEGEL_INT range).
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  int                 n;
  int                 copy_of_n;
} Pair;

HEGEL_BINDING (n);

static hegel_schema_t pair_schema;

static
void
init_schema (void)
{
  pair_schema = HEGEL_STRUCT (Pair,
      HEGEL_LET (n, HEGEL_INT (2, 5)),
      HEGEL_USE (n));
}

static
void
test_pair_coherence (
hegel_testcase *    tc)
{
  Pair *              p;
  hegel_shape *       sh;

  sh = hegel_schema_draw (tc, pair_schema, (void **) &p);

  HEGEL_ASSERT (p->n >= 2 && p->n <= 5,
                "n=%d out of [2,5]", p->n);
  HEGEL_ASSERT (p->copy_of_n == p->n,
                "copy_of_n=%d != n=%d", p->copy_of_n, p->n);

  hegel_shape_free (sh);
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  init_schema ();
  printf ("Testing HEGEL_BINDING / HEGEL_LET / HEGEL_USE (same-struct)...\n");
  hegel_run_test (test_pair_coherence);
  printf ("  PASSED\n");

  hegel_schema_free (pair_schema);
  return (0);
}
