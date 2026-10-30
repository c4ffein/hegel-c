/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: integrated shrinking with binding shadowing.
**
**   ShadowOuter { int outer_n_before; ShadowInner inner; int outer_n_after }
**   ShadowInner { int inner_n }
**
**   Outer LET(sn)=40..50, inner LET(sn)=1..5.  Inner USE(sn) reads
**   inner's shadow; outer's two USE(sn) entries read outer's value.
**
** Layer 1: predicate fails when outer_n_before > 45 AND inner_n > 3.
**
** Expected minimal:
**   outer_n_before = outer_n_after = 46  (smallest in [40,50] above 45)
**   inner_n = 4                          (smallest in [1,5] above 3)
**
** Probes that shrinking doesn't confuse outer's binding with the
** shadowed inner one — outer_n_after MUST equal outer_n_before
** in the minimal (both read the same outer LET).
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  int                 inner_n;
} ShadowInner;

typedef struct {
  int                 outer_n_before;
  ShadowInner         inner;
  int                 outer_n_after;
} ShadowOuter;

typedef struct {
  int                 outer_n_before;
  int                 inner_n;
  int                 outer_n_after;
} Probe;

static char           probe_path[256];

static
void
probe_init (
const char *        tag)
{
  snprintf (probe_path, sizeof (probe_path),
            "/tmp/hegel_shrink_probe_%s_%d", tag, (int) getpid ());
  unlink (probe_path);
}

static
void
probe_write (
const Probe *       p)
{
  int fd = open (probe_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) return;
  ssize_t w = write (fd, p, sizeof (*p));
  (void) w;
  close (fd);
}

static
int
probe_read (
Probe *             p)
{
  int fd = open (probe_path, O_RDONLY);
  if (fd < 0) return (-1);
  ssize_t r = read (fd, p, sizeof (*p));
  close (fd);
  return ((int) r);
}

HEGEL_BINDING (sn);
static hegel_schema_t shadow_schema;

static
void
test_shadow_shrink (
hegel_testcase *    tc)
{
  ShadowOuter *       o;
  hegel_shape *       sh;
  int                 fail;

  sh = hegel_schema_draw (tc, shadow_schema, (void **) &o);

  fail = (o->outer_n_before > 45 && o->inner.inner_n > 3);
  if (fail) {
    Probe p = {0};
    p.outer_n_before = o->outer_n_before;
    p.inner_n        = o->inner.inner_n;
    p.outer_n_after  = o->outer_n_after;
    probe_write (&p);
  }

  HEGEL_ASSERT (!fail, "outer_before=%d inner=%d outer_after=%d",
                o->outer_n_before, o->inner.inner_n, o->outer_n_after);

  hegel_shape_free (sh);
}

int
main (
int                 argc,
char *              argv[])
{
  Probe               p;
  int                 r;
  int                 ok;

  (void) argc;
  (void) argv;

  probe_init ("shadow");

  shadow_schema = HEGEL_STRUCT (ShadowOuter,
      HEGEL_LET    (sn, HEGEL_INT (40, 50)),
      HEGEL_USE    (sn),
      HEGEL_INLINE (ShadowInner,
          HEGEL_LET (sn, HEGEL_INT (1, 5)),
          HEGEL_USE (sn)),
      HEGEL_USE    (sn));

  printf ("Probing shrink quality of shadowed bindings...\n");
  r = hegel_run_test_result_n (test_shadow_shrink, 500);

  if (r != 1) {
    fprintf (stderr, "FAIL: hegel did not detect a failing case (r=%d)\n", r);
    hegel_schema_free (shadow_schema);
    return (1);
  }

  if (probe_read (&p) != (int) sizeof (p)) {
    fprintf (stderr, "FAIL: probe file empty\n");
    hegel_schema_free (shadow_schema);
    return (1);
  }

  ok = 1;
  if (p.outer_n_before != 46) {
    fprintf (stderr, "minimal outer_n_before=%d, expected 46\n", p.outer_n_before);
    ok = 0;
  }
  if (p.inner_n != 4) {
    fprintf (stderr, "minimal inner_n=%d, expected 4\n", p.inner_n);
    ok = 0;
  }
  if (p.outer_n_after != p.outer_n_before) {
    fprintf (stderr,
             "minimal outer_n_after=%d != outer_n_before=%d - shadow leaked into shrink\n",
             p.outer_n_after, p.outer_n_before);
    ok = 0;
  }

  unlink (probe_path);
  hegel_schema_free (shadow_schema);

  if (!ok) {
    fprintf (stderr, "Shrink quality regression on shadowed bindings\n");
    return (1);
  }
  printf ("  PASSED (minimal: outer=%d, inner=%d, outer_after=%d)\n",
          p.outer_n_before, p.inner_n, p.outer_n_after);
  return (0);
}
