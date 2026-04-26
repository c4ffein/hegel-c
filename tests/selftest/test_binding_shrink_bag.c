/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: integrated shrinking on the canonical binding pattern
**       Bag{int n; int *items} where n is a LET and the array length.
**
** Layer 1: predicate is `n > 3 && items[2] == 7`.
** Layer 2: hegel detects the failure and shrinks.  The IPC trick:
**          each failing run writes its drawn state to a temp file
**          (last-write wins across forks).  After hegel finishes
**          shrinking, main reads the file and asserts the captured
**          minimal matches the expected smallest counterexample.
**
** Expected minimal: n=4, items=[0,0,7,0].
**   - n must be > 3, smallest is 4
**   - items[0..1] shrink toward 0
**   - items[2] must be 7 (the predicate)
**   - items[3] shrinks to 0 (it's part of the array but unconstrained)
**
** Test status: TESTS_PASS — main exits 0 iff the captured minimal
** matches expectations.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  int *               items;
  int                 n;
} Bag;

typedef struct {
  int                 n;
  int                 items[16];
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
  int                 fd;
  ssize_t             w;

  fd = open (probe_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) return;
  w = write (fd, p, sizeof (*p));
  (void) w;
  close (fd);
}

static
int
probe_read (
Probe *             p)
{
  int                 fd;
  ssize_t             r;

  fd = open (probe_path, O_RDONLY);
  if (fd < 0) return (-1);
  r = read (fd, p, sizeof (*p));
  close (fd);
  return ((int) r);
}

HEGEL_BINDING (n);
static hegel_schema_t bag_schema;

static
void
test_bag_shrink (
hegel_testcase *    tc)
{
  Bag *               b;
  hegel_shape *       sh;
  int                 i;
  int                 fail;

  sh = hegel_schema_draw (tc, bag_schema, (void **) &b);

  fail = (b->n > 3 && b->n >= 3 && b->items[2] == 7);
  if (fail) {
    Probe             p;
    memset (&p, 0, sizeof (p));
    p.n = b->n;
    for (i = 0; i < b->n && i < 16; i ++) p.items[i] = b->items[i];
    probe_write (&p);
  }

  HEGEL_ASSERT (!fail, "n=%d items[2]=%d", b->n,
                (b->n >= 3) ? b->items[2] : -1);

  hegel_shape_free (sh);
}

int
main (
int                 argc,
char *              argv[])
{
  Probe               p;
  int                 r;
  int                 i;
  int                 ok;

  (void) argc;
  (void) argv;

  probe_init ("bag");

  bag_schema = HEGEL_STRUCT (Bag,
      HEGEL_LET    (n, HEGEL_INT (0, 16)),
      HEGEL_ARR_OF (HEGEL_USE (n), HEGEL_INT (0, 100)),
      HEGEL_USE    (n));

  printf ("Probing shrink quality of Bag{n, items}...\n");
  r = hegel_run_test_result_n (test_bag_shrink, 1000);

  if (r != 1) {
    fprintf (stderr, "FAIL: hegel did not detect a failing case (r=%d)\n", r);
    hegel_schema_free (bag_schema);
    return (1);
  }

  if (probe_read (&p) != (int) sizeof (p)) {
    fprintf (stderr, "FAIL: probe file empty or unreadable\n");
    hegel_schema_free (bag_schema);
    return (1);
  }

  ok = 1;
  if (p.n != 4) { fprintf (stderr, "minimal n=%d, expected 4\n", p.n); ok = 0; }
  for (i = 0; i < 4 && i < 16; i ++) {
    int expected = (i == 2) ? 7 : 0;
    if (p.items[i] != expected) {
      fprintf (stderr, "minimal items[%d]=%d, expected %d\n",
               i, p.items[i], expected);
      ok = 0;
    }
  }

  unlink (probe_path);
  hegel_schema_free (bag_schema);

  if (!ok) {
    fprintf (stderr, "Shrink quality regression on Bag{n, items}\n");
    return (1);
  }
  printf ("  PASSED (minimal: n=%d, items=[%d,%d,%d,%d])\n",
          p.n, p.items[0], p.items[1], p.items[2], p.items[3]);
  return (0);
}
