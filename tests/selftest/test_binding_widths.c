/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_LET / HEGEL_USE_* with non-int widths.
**
** Layer 1: each bound value is read back through the matching
** HEGEL_USE_* and stored in a struct field of the right C type.
** A USE width mismatch with the binding's stored kind aborts loudly,
** so a clean read-back proves the kind was preserved.
**
** Covers i64, u64, double, float, and reuses int (HEGEL_USE) for
** sanity.  Bounds are tight so a successful run also proves the
** value survived the round-trip without truncation.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  int                 reg_n;
  int64_t             big_i;
  uint64_t            big_u;
  float               wf;
  double              wd;
} Widths;

HEGEL_BINDING (n);
HEGEL_BINDING (bi);
HEGEL_BINDING (bu);
HEGEL_BINDING (wf);
HEGEL_BINDING (wd);

static hegel_schema_t widths_schema;

static
void
test_widths (
hegel_testcase *    tc)
{
  Widths *            w;
  hegel_shape *       sh;

  sh = hegel_schema_draw (tc, widths_schema, (void **) &w);

  HEGEL_ASSERT (w->reg_n >= 1 && w->reg_n <= 9,
                "reg_n=%d out of [1,9]", w->reg_n);

  /* i64 range chosen above 32-bit max so any silent truncation to
  ** int would clip to -1 / something small.  A clean read-back is
  ** strong evidence the binding kind survived. */
  HEGEL_ASSERT (w->big_i >= 5000000000LL && w->big_i <= 6000000000LL,
                "big_i=%lld out of i64 range", (long long) w->big_i);

  /* u64 range above 32-bit max for the same reason. */
  HEGEL_ASSERT (w->big_u >= 5000000000ULL && w->big_u <= 6000000000ULL,
                "big_u=%llu out of u64 range",
                (unsigned long long) w->big_u);

  HEGEL_ASSERT (w->wf >= -1.0f && w->wf <= 1.0f,
                "wf=%g out of [-1,1]", (double) w->wf);
  HEGEL_ASSERT (w->wd >= 100.0 && w->wd <= 200.0,
                "wd=%g out of [100,200]", w->wd);

  hegel_shape_free (sh);
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  widths_schema = HEGEL_STRUCT (Widths,
      HEGEL_LET (n,  HEGEL_INT    (1, 9)),
      HEGEL_LET (bi, HEGEL_I64    (5000000000LL, 6000000000LL)),
      HEGEL_LET (bu, HEGEL_U64    (5000000000ULL, 6000000000ULL)),
      HEGEL_LET (wf, HEGEL_FLOAT  (-1.0f, 1.0f)),
      HEGEL_LET (wd, HEGEL_DOUBLE (100.0, 200.0)),
      HEGEL_USE        (n),
      HEGEL_USE_I64    (bi),
      HEGEL_USE_U64    (bu),
      HEGEL_USE_FLOAT  (wf),
      HEGEL_USE_DOUBLE (wd));

  printf ("Testing HEGEL_LET / HEGEL_USE_* widths...\n");
  hegel_run_test (test_widths);
  printf ("  PASSED\n");

  hegel_schema_free (widths_schema);
  return (0);
}
