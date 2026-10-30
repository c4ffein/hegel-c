/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: the swapped-children JSON bug of test_span_tree_json_shrinks.c,
**       with the children drawn by HEGEL_DEPENDS callbacks.
**
** See test_schema_tree_json_shrinks.c for the trio.  This version
** keeps gen_tree()'s distribution (depth cap 3, 50/50 children) but
** declares it: each child slot asks hegel_view_depth how deep its node
** is.  Spans, allocation and freeing come from the schema layer.
**
** Expected: EXIT non-zero, shrunk like the other two.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ---- Layer 1: the data structure + JSON codec ---- */

typedef struct Tree {
  int                 val;
  char *              label; /* NULL if absent */
  struct Tree *       left;  /* NULL if absent */
  struct Tree *       right; /* NULL if absent */
} Tree;

/* -- free -- */

static
void
tree_free (
Tree *                      t)
{
  if (t == NULL) return;
  free (t->label);
  tree_free (t->left);
  tree_free (t->right);
  free (t);
}

/* -- compare (structural equality) -- */

static
int
trees_equal (
Tree *                      a,
Tree *                      b)
{
  if (a == NULL && b == NULL) return (1);
  if (a == NULL || b == NULL) return (0);
  if (a->val != b->val) return (0);
  if ((a->label == NULL) != (b->label == NULL)) return (0);
  if (a->label != NULL && strcmp (a->label, b->label) != 0) return (0);
  return (trees_equal (a->left,  b->left) &&
          trees_equal (a->right, b->right));
}

/* -- serialize -- */
/* Format: {"v":INT,"l":STRING|null,"L":OBJ|null,"R":OBJ|null}
** Labels are constrained to [a-z]{0,8} by the generator, so no
** escaping is needed inside the string body. */

static
void
buf_grow (
char * *                    out,
size_t *                    cap,
size_t                      need)
{
  while (need + 1 > *cap) {
    *cap = (*cap == 0) ? 64 : (*cap * 2);
    *out = realloc (*out, *cap);
  }
}

static
void
buf_emit (
char * *                    out,
size_t *                    cap,
size_t *                    len,
const char *                s)
{
  size_t              n;

  n = strlen (s);
  buf_grow (out, cap, *len + n);
  memcpy (*out + *len, s, n);
  *len += n;
  (*out)[*len] = 0;
}

static
void
serialize_node (
Tree *                      t,
char * *                    out,
size_t *                    cap,
size_t *                    len)
{
  char                tmp[64];

  if (t == NULL) {
    buf_emit (out, cap, len, "null");
    return;
  }

  snprintf (tmp, sizeof (tmp), "{\"v\":%d,\"l\":", t->val);
  buf_emit (out, cap, len, tmp);

  if (t->label != NULL) {
    buf_emit (out, cap, len, "\"");
    buf_emit (out, cap, len, t->label);
    buf_emit (out, cap, len, "\"");
  } else {
    buf_emit (out, cap, len, "null");
  }

  /* DELIBERATE BUG (the shrinks variant):
  ** When BOTH children are present, write them in swapped order.
  ** Round-trip then fails for any tree with two non-equal children.
  ** Hegel's job: shrink to a minimal failing tree — one that has both
  ** children present (so the bug fires) but where the children differ
  ** in the smallest possible way. */
  if (t->left != NULL && t->right != NULL) {
    buf_emit (out, cap, len, ",\"L\":");
    serialize_node (t->right, out, cap, len);  /* swapped */
    buf_emit (out, cap, len, ",\"R\":");
    serialize_node (t->left, out, cap, len);   /* swapped */
  } else {
    buf_emit (out, cap, len, ",\"L\":");
    serialize_node (t->left, out, cap, len);
    buf_emit (out, cap, len, ",\"R\":");
    serialize_node (t->right, out, cap, len);
  }
  buf_emit (out, cap, len, "}");
}

static
char *
tree_to_json (
Tree *                      t)
{
  char *              out;
  size_t              cap;
  size_t              len;

  out = NULL;
  cap = 0;
  len = 0;
  serialize_node (t, &out, &cap, &len);
  return (out);
}

/* -- parse --
** Recursive-descent parser that only accepts the exact format above.
** No whitespace handling, no escapes, no error recovery — this
** parser is the inverse of serialize_node by construction. */

typedef struct {
  const char *        p;
} parser;

static
int
expect_char (
parser *                    ps,
char                        c)
{
  if (*ps->p == c) { ps->p ++; return (1); }
  return (0);
}

static
int
expect_lit (
parser *                    ps,
const char *                lit)
{
  size_t              n;

  n = strlen (lit);
  if (strncmp (ps->p, lit, n) != 0) return (0);
  ps->p += n;
  return (1);
}

static
Tree *
parse_node (
parser *                    ps)
{
  Tree *              t;
  const char *        start;
  char *              end;
  size_t              n;

  if (expect_lit (ps, "null")) return (NULL);
  if (! expect_char (ps, '{')) return (NULL); /* malformed */

  t = (Tree *) calloc (1, sizeof (Tree));

  /* "v":INT */
  expect_lit (ps, "\"v\":");
  t->val = (int) strtol (ps->p, &end, 10);
  ps->p = end;
  expect_char (ps, ',');

  /* "l":STRING|null */
  expect_lit (ps, "\"l\":");
  if (expect_lit (ps, "null")) {
    t->label = NULL;
  } else {
    expect_char (ps, '"');
    start = ps->p;
    while (*ps->p != 0 && *ps->p != '"') ps->p ++;
    n = (size_t) (ps->p - start);
    t->label = (char *) malloc (n + 1);
    memcpy (t->label, start, n);
    t->label[n] = 0;
    expect_char (ps, '"');
  }
  expect_char (ps, ',');

  /* "L":OBJ|null */
  expect_lit (ps, "\"L\":");
  t->left = parse_node (ps);
  expect_char (ps, ',');

  /* "R":OBJ|null */
  expect_lit (ps, "\"R\":");
  t->right = parse_node (ps);

  expect_char (ps, '}');
  return (t);
}

static
Tree *
json_to_tree (
const char *                json)
{
  parser              ps;

  ps.p = json;
  return (parse_node (&ps));
}

/* ---- Layer 2: the schema + hegel test ---- */

/* The prim gen_tree caps the recursion with a depth parameter and
** draws each child as a 50/50 option.  Here a callback does the same:
** it asks the view how deep this node is and returns either an
** optional pointer to another Tree or, at the cap, a NULL.
**
** No schema means "always NULL", so the cap writes a 64-bit zero
** into the pointer slot — a gap of the PoC, see TODO. */
#define MAX_DEPTH 3

static hegel_schema_t tree_schema;

static
hegel_schema_t
child_of (
const hegel_view *          v,
void *                      ctx)
{
  (void) ctx;
  if (hegel_view_depth (v) >= MAX_DEPTH)
    return (hegel_schema_i64_range (0, 0));
  return (hegel_schema_optional_ptr (hegel_schema_ref (tree_schema)));
}

static hegel_schema_t tree_schema;

static
void
test_roundtrip (
hegel_testcase *            tc)
{
  Tree *              original;
  Tree *              parsed;
  hegel_shape *       sh;
  char *              json;
  int                 eq;
  char                msg[1024];

  sh     = hegel_schema_draw (tc, tree_schema, (void **) &original);
  json   = tree_to_json (original);
  parsed = json_to_tree (json);
  eq     = trees_equal (original, parsed);

  if (! eq) {
    snprintf (msg, sizeof (msg),
              "round-trip mismatch, json=%s", json);
    hegel_fail (msg);
  }

  tree_free (parsed);
  free (json);
  hegel_shape_free (sh);               /* frees original */
}

/* ---- Layer 3: runner ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  tree_schema = HEGEL_STRUCT (Tree,
      HEGEL_INT      (-1000, 1000),
      HEGEL_OPTIONAL (hegel_schema_text (0, 8)),
      HEGEL_DEPENDS  (Tree *, child_of, NULL),
      HEGEL_DEPENDS  (Tree *, child_of, NULL));

  printf ("Testing JSON round-trip (buggy serializer) on view-drawn trees...\n");
  hegel_run_test (test_roundtrip);
  printf ("PASSED\n");

  hegel_schema_free (tree_schema);
  return (0);
}
