/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: integrated shrinking on the jagged 2D pattern.
**
**   Group  { int m; int *data }    - inner ARR_OF length m
**   Bag2D  { Group **groups; int n }
**
** Layer 1: predicate fails when the first group exists, has at
** least one element, and that element equals 5.
**
** Expected minimal: outer n=1, groups[0]={m=1, data=[5]}.
**   - n must be >= 1 (so groups[0] exists)
**   - groups[0]->m must be >= 1 (so data[0] exists)
**   - data[0] must be 5 (the predicate)
**
** Probes per-instance binding scoping under shrink: the inner LET(m)
** is independent per element, so shrinking groups[0]->m must NOT
** cascade into other groups (which is fine since outer n=1 here),
** AND must reach 1 cleanly.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  int *               data;
  int                 m;
} Group;

typedef struct {
  Group **            groups;
  int                 n;
} Bag2D;

typedef struct {
  int                 n;
  int                 g0_m;
  int                 g0_data0;
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

HEGEL_BINDING (n);
HEGEL_BINDING (m);
static hegel_schema_t bag2d_schema;

static
void
test_jagged_shrink (
hegel_testcase *    tc)
{
  Bag2D *             b;
  hegel_shape *       sh;
  int                 fail;

  sh = hegel_schema_draw (tc, bag2d_schema, (void **) &b);

  fail = (b->n >= 1
          && b->groups[0] != NULL
          && b->groups[0]->m >= 1
          && b->groups[0]->data[0] == 5);

  if (fail) {
    Probe p = {0};
    p.n        = b->n;
    p.g0_m     = b->groups[0]->m;
    p.g0_data0 = b->groups[0]->data[0];
    probe_write (&p);
  }

  HEGEL_ASSERT (!fail, "n=%d g0_m=%d g0_data0=%d", b->n,
                b->groups[0]->m, b->groups[0]->data[0]);

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

  probe_init ("jagged");

  hegel_schema_t group = HEGEL_STRUCT (Group,
      HEGEL_LET    (m, HEGEL_INT (0, 4)),
      HEGEL_ARR_OF (HEGEL_USE (m), HEGEL_INT (0, 10)),
      HEGEL_USE    (m));

  bag2d_schema = HEGEL_STRUCT (Bag2D,
      HEGEL_LET    (n, HEGEL_INT (0, 4)),
      HEGEL_ARR_OF (HEGEL_USE (n), group),
      HEGEL_USE    (n));

  printf ("Probing shrink quality of jagged Bag2D...\n");
  r = hegel_run_test_result_n (test_jagged_shrink, 1000);

  if (r != 1) {
    fprintf (stderr, "FAIL: hegel did not detect a failing case (r=%d)\n", r);
    hegel_schema_free (bag2d_schema);
    return (1);
  }

  if (probe_read (&p) != (int) sizeof (p)) {
    fprintf (stderr, "FAIL: probe file empty\n");
    hegel_schema_free (bag2d_schema);
    return (1);
  }

  ok = 1;
  if (p.n        != 1) { fprintf (stderr, "minimal n=%d, expected 1\n", p.n); ok = 0; }
  if (p.g0_m     != 1) { fprintf (stderr, "minimal g0_m=%d, expected 1\n", p.g0_m); ok = 0; }
  if (p.g0_data0 != 5) { fprintf (stderr, "minimal g0_data0=%d, expected 5\n", p.g0_data0); ok = 0; }

  unlink (probe_path);
  hegel_schema_free (bag2d_schema);

  if (!ok) {
    fprintf (stderr, "Shrink quality regression on jagged Bag2D\n");
    return (1);
  }
  printf ("  PASSED (minimal: n=%d, groups[0]={m=%d, data=[%d]})\n",
          p.n, p.g0_m, p.g0_data0);
  return (0);
}
