/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: integrated shrinking on HEGEL_ARR_OF of struct pointers.
**
**   Item   { int val }
**   Bag    { Item **items; int n }
**
** Layer 1: predicate fails when n >= 2 AND items[0]->val == 5.
**          Notably, it depends on the FIRST element specifically;
**          the shrinker should NOT trim items[0] (the failing one)
**          when shrinking the array — it should shrink the second
**          element to a zero-filled struct and stop.
**
** Expected minimal: n=2, items[0]={val=5}, items[1]={val=0}.
**
** Probes that ARR_OF-of-pointers shrinking trims the right elements.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  int                 val;
} Item;

typedef struct {
  Item **             items;
  int                 n;
} Bag;

typedef struct {
  int                 n;
  int                 vals[16];
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
static hegel_schema_t bag_schema;

static
void
test_arr_of_structs_shrink (
hegel_testcase *    tc)
{
  Bag *               b;
  hegel_shape *       sh;
  int                 fail;
  int                 i;

  sh = hegel_schema_draw (tc, bag_schema, (void **) &b);

  fail = (b->n >= 2 && b->items[0] != NULL && b->items[0]->val == 5);
  if (fail) {
    Probe p = {0};
    p.n = b->n;
    for (i = 0; i < b->n && i < 16; i ++) {
      p.vals[i] = b->items[i] ? b->items[i]->val : -1;
    }
    probe_write (&p);
  }

  HEGEL_ASSERT (!fail, "n=%d items[0]->val=%d", b->n,
                (b->n >= 1 && b->items[0]) ? b->items[0]->val : -1);

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

  probe_init ("arrstr");

  hegel_schema_t item_schema = HEGEL_STRUCT (Item, HEGEL_INT (0, 100));

  bag_schema = HEGEL_STRUCT (Bag,
      HEGEL_LET    (n, HEGEL_INT (0, 8)),
      HEGEL_ARR_OF (HEGEL_USE (n), item_schema),
      HEGEL_USE    (n));

  printf ("Probing shrink quality of HEGEL_ARR_OF of struct pointers...\n");
  r = hegel_run_test_result_n (test_arr_of_structs_shrink, 500);

  if (r != 1) {
    fprintf (stderr, "FAIL: hegel did not detect a failing case (r=%d)\n", r);
    hegel_schema_free (bag_schema);
    return (1);
  }

  if (probe_read (&p) != (int) sizeof (p)) {
    fprintf (stderr, "FAIL: probe file empty\n");
    hegel_schema_free (bag_schema);
    return (1);
  }

  ok = 1;
  if (p.n != 2) {
    fprintf (stderr, "minimal n=%d, expected 2\n", p.n);
    ok = 0;
  }
  if (p.vals[0] != 5) {
    fprintf (stderr, "minimal items[0]->val=%d, expected 5\n", p.vals[0]);
    ok = 0;
  }
  if (p.n >= 2 && p.vals[1] != 0) {
    fprintf (stderr, "minimal items[1]->val=%d, expected 0\n", p.vals[1]);
    ok = 0;
  }

  unlink (probe_path);
  hegel_schema_free (bag_schema);

  if (!ok) {
    fprintf (stderr, "Shrink quality regression on HEGEL_ARR_OF of structs\n");
    return (1);
  }
  printf ("  PASSED (minimal: n=%d, items[0]=%d, items[1]=%d)\n",
          p.n, p.vals[0], p.vals[1]);
  return (0);
}
