/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: integrated shrinking of USE-through-case paths — the three
** composite draw paths that resolve an outer LET from inside a
** sub-scope (union case, variant case, optional inner struct).
**
** Same probe technique as test_binding_shrink_bag: each failing run
** writes its drawn state to a temp file (last-write wins across
** forks); after hegel finishes shrinking, main reads the file and
** asserts the captured minimal matches the expected smallest
** counterexample.  Three independent probes run sequentially.
**
** Each probe fails when (chosen case/presence forces the sub-scope)
** AND n > 3, so the expected minimal is n=4 with the sub-scope's
** USE field equal to n — pinning that the shrinker keeps bindings
** coherent through the case paths while minimizing.
**
** Test status: TESTS_PASS — main exits 0 iff all captured minimals
** match expectations.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ---------------- shared probe machinery ---------------- */

typedef struct {
  int                 n;
  int                 tag;      /* union/variant tag, or optional presence */
  int                 use_val;  /* the USE field inside the sub-scope */
  int                 extra;    /* trailing drawn field, shrinks to 0 */
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

static
int
probe_check (
const char *        what,
int                 expected_tag)
{
  Probe               p;
  int                 ok;

  if (probe_read (&p) != (int) sizeof (p)) {
    fprintf (stderr, "FAIL (%s): probe file empty or unreadable\n", what);
    return (0);
  }
  unlink (probe_path);

  ok = 1;
  if (p.n != 4) {
    fprintf (stderr, "%s: minimal n=%d, expected 4\n", what, p.n);
    ok = 0;
  }
  if (p.tag != expected_tag) {
    fprintf (stderr, "%s: minimal tag=%d, expected %d\n",
             what, p.tag, expected_tag);
    ok = 0;
  }
  if (p.use_val != p.n) {
    fprintf (stderr, "%s: minimal use_val=%d != n=%d — USE lost "
             "coherence under shrinking\n", what, p.use_val, p.n);
    ok = 0;
  }
  if (p.extra != 0) {
    fprintf (stderr, "%s: minimal extra=%d, expected 0\n", what, p.extra);
    ok = 0;
  }
  if (ok)
    printf ("    PASSED (minimal: n=%d tag=%d use_val=%d extra=%d)\n",
            p.n, p.tag, p.use_val, p.extra);
  return (ok);
}

/* ---------------- probe 1: union case ---------------- */

typedef struct {
  int                 n;
  int                 tag;
  union {
    struct { int a; }         one;
    struct { int x; int y; }  two;
  } u;
} UBox;

HEGEL_BINDING (un);
static hegel_schema_t ubox_schema;

static
void
test_union_shrink (
hegel_testcase *    tc)
{
  UBox *              b;
  hegel_shape *       sh;
  int                 fail;

  sh = hegel_schema_draw (tc, ubox_schema, (void **) &b);

  fail = (b->tag == 1 && b->n > 3);
  if (fail) {
    Probe             p;
    memset (&p, 0, sizeof (p));
    p.n       = b->n;
    p.tag     = b->tag;
    p.use_val = b->u.two.x;
    p.extra   = b->u.two.y;
    probe_write (&p);
  }

  HEGEL_ASSERT (!fail, "tag=%d n=%d", b->tag, b->n);

  hegel_shape_free (sh);
}

/* ---------------- probe 2: variant case ---------------- */

typedef struct { int val; }            SCaseA;
typedef struct { int val; int extra; } SCaseB;

typedef struct {
  int                 tag;
  void *              value;
  int                 n;
} VBox;

HEGEL_BINDING (vn);
static hegel_schema_t vbox_schema;

static
void
test_variant_shrink (
hegel_testcase *    tc)
{
  VBox *              b;
  hegel_shape *       sh;
  int                 fail;

  sh = hegel_schema_draw (tc, vbox_schema, (void **) &b);

  fail = (b->tag == 1 && b->n > 3);
  if (fail) {
    SCaseB *          c = (SCaseB *) b->value;
    Probe             p;
    memset (&p, 0, sizeof (p));
    p.n       = b->n;
    p.tag     = b->tag;
    p.use_val = c->val;
    p.extra   = c->extra;
    probe_write (&p);
  }

  HEGEL_ASSERT (!fail, "tag=%d n=%d", b->tag, b->n);

  hegel_shape_free (sh);
}

/* ---------------- probe 3: optional inner struct ---------------- */

typedef struct { int val; } OInner;

typedef struct {
  OInner *            maybe;
  int                 n;
} OBox;

HEGEL_BINDING (on);
static hegel_schema_t obox_schema;

static
void
test_optional_shrink (
hegel_testcase *    tc)
{
  OBox *              b;
  hegel_shape *       sh;
  int                 fail;

  sh = hegel_schema_draw (tc, obox_schema, (void **) &b);

  fail = (b->maybe != NULL && b->n > 3);
  if (fail) {
    Probe             p;
    memset (&p, 0, sizeof (p));
    p.n       = b->n;
    p.tag     = 1;                 /* presence plays the tag's role */
    p.use_val = b->maybe->val;
    p.extra   = 0;
    probe_write (&p);
  }

  HEGEL_ASSERT (!fail, "present=%d n=%d", b->maybe != NULL, b->n);

  hegel_shape_free (sh);
}

/* ---------------- runner ---------------- */

static
int
run_probe (
const char *        what,
const char *        tag,
void             (* fn) (hegel_testcase *),
int                 expected_tag)
{
  int                 r;

  printf ("  %s...\n", what);
  probe_init (tag);
  /* 5000 cases: the predicates are common (~1/6 of draws), but the
  ** headroom keeps the run deterministic across engine-distribution
  ** changes (see test_binding_shrink_arr_of_structs). */
  r = hegel_run_test_result_n (fn, 5000);
  if (r != 1) {
    fprintf (stderr, "FAIL (%s): hegel did not detect a failing case "
             "(r=%d)\n", what, r);
    return (0);
  }
  return (probe_check (what, expected_tag));
}

int
main (
int                 argc,
char *              argv[])
{
  int                 ok = 1;

  (void) argc;
  (void) argv;

  ubox_schema = HEGEL_STRUCT (UBox,
      HEGEL_LET   (un, HEGEL_INT (0, 10)),
      HEGEL_USE   (un),
      HEGEL_UNION (
          HEGEL_CASE (HEGEL_USE (un)),
          HEGEL_CASE (HEGEL_USE (un), HEGEL_INT (0, 5))));

  {
    hegel_schema_t case_a = HEGEL_STRUCT (SCaseA,
        HEGEL_USE (vn));
    hegel_schema_t case_b = HEGEL_STRUCT (SCaseB,
        HEGEL_USE (vn),
        HEGEL_INT (0, 5));
    vbox_schema = HEGEL_STRUCT (VBox,
        HEGEL_LET     (vn, HEGEL_INT (0, 10)),
        HEGEL_VARIANT (case_a, case_b),
        HEGEL_USE     (vn));
  }

  {
    hegel_schema_t inner_s = HEGEL_STRUCT (OInner,
        HEGEL_USE (on));
    obox_schema = HEGEL_STRUCT (OBox,
        HEGEL_LET      (on, HEGEL_INT (0, 10)),
        HEGEL_OPTIONAL (inner_s),
        HEGEL_USE      (on));
  }

  ok &= run_probe ("shrink through union case",   "u_case", test_union_shrink,    1);
  ok &= run_probe ("shrink through variant case", "v_case", test_variant_shrink,  1);
  ok &= run_probe ("shrink through optional",     "o_case", test_optional_shrink, 1);

  hegel_schema_free (ubox_schema);
  hegel_schema_free (vbox_schema);
  hegel_schema_free (obox_schema);

  if (!ok) {
    fprintf (stderr, "Shrink quality regression on USE-through-case paths\n");
    return (1);
  }
  return (0);
}
