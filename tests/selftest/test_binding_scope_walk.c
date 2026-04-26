/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_USE inside a HEGEL_INLINE sub-struct resolves a
** HEGEL_LET bound in the enclosing struct — i.e. bindings walk
** the lexical scope chain outward, not just within their own
** struct.
**
** Topology:
**     Outer {
**       int n;        // bound here via HEGEL_LET (count)
**       Inner { int echo; }  // HEGEL_USE(count) at inner field
**     }
**
** Property: after draw, o->inner.echo == o->n, and n is in range.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  int                 echo;
} Inner;

typedef struct {
  int                 n;
  Inner               inner;
} Outer;

HEGEL_BINDING (count);

static hegel_schema_t outer_schema;

static
void
init_schema (void)
{
  outer_schema = HEGEL_STRUCT (Outer,
      HEGEL_LET (count, HEGEL_INT (10, 20)),    /* non-positional */
      HEGEL_USE (count),                         /* field 0: int n */
      HEGEL_INLINE (Inner,
          HEGEL_USE (count)));                   /* field 1: Inner.echo */
}

static
void
test_scope_walk (
hegel_testcase *    tc)
{
  Outer *             o;
  hegel_shape *       sh;

  sh = hegel_schema_draw (tc, outer_schema, (void **) &o);

  HEGEL_ASSERT (o->n >= 10 && o->n <= 20,
                "n=%d out of [10,20]", o->n);
  HEGEL_ASSERT (o->inner.echo == o->n,
                "inner.echo=%d != n=%d (parent-scope walk broken)",
                o->inner.echo, o->n);

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
  printf ("Testing HEGEL_USE reaches HEGEL_LET in enclosing struct...\n");
  hegel_run_test (test_scope_walk);
  printf ("  PASSED\n");

  hegel_schema_free (outer_schema);
  return (0);
}
