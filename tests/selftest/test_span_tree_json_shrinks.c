/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: round-trip a recursive tree through JSON serialize+parse.
**       SHRINKS variant — the serializer has a deliberate bug: when a
**       node has BOTH children, it writes them in swapped order.
**       Hegel must find a minimal failing tree, which means a tree
**       where (a) some node has both children present and (b) those
**       children are not equal — anything simpler does not trigger
**       the bug.  See test_span_tree_json_passes.c for the correct
**       version that this file is derived from.
**
** This is the canonical property-based test pattern: generate a random
** structured value, run it through `parse(serialize(x))`, assert the
** result equals the original.  If the round-trip ever fails, hegel
** shrinks the input to a minimal counterexample.
**
** It also exists to exercise spans on a structurally interesting input.
** The tree has:
**   - an int value (always present)
**   - an optional string label
**   - an optional left subtree
**   - an optional right subtree
**
** Each node is generated inside a span; each optional field is also
** wrapped in a span (HEGEL_SPAN_OPTIONAL).  This tells the shrinker
** that the bytes belonging to a subtree are one logical unit — so it
** can try shrinks like "drop this subtree" or "shrink this subtree's
** value without touching its sibling" instead of mutating raw bytes
** that span unrelated draws.
**
** Layer 1: tree_to_json (with the swap bug) + json_to_tree.
** Layer 2: hegel test that round-trips a random tree.
** Layer 3: main runner.
**
** Expected: EXIT non-zero.  Hegel finds the bug and reports a shrunk
** failing input — a tree small enough to read at a glance, with two
** distinguishable children under one node.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "hegel_c.h"

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

/* ---- Layer 2: the hegel test ---- */

/* Generate a tree.  Each node and each optional field is wrapped in
** a span so the shrinker can act on whole subtrees.
**
** All start_span / stop_span calls are statically paired in this
** function — there is no failure path between them, so spans are
** always closed before any hegel_fail can fire. */
#define MAX_DEPTH 3

static
Tree *
gen_tree (
hegel_testcase *            tc,
int                         depth)
{
  Tree *              t;
  int                 n;

  hegel_start_span (tc, HEGEL_SPAN_USER); /* this whole node */
  t = (Tree *) calloc (1, sizeof (Tree));

  t->val = hegel_draw_int (tc, -1000, 1000);

  /* Optional label — drawn char-by-char from [a-z] so no JSON
  ** escaping is required.  Empty string is allowed (length 0) and is
  ** *not* the same thing as a missing label.
  **
  ** Char-by-char rather than hegel_draw_regex because the C regex
  ** wrapper uses Hypothesis's "contains a match" semantics, not
  ** full-match — for a permissive pattern like [a-z]{0,8} (which
  ** matches the empty string) that degrades to "any string." */
  hegel_start_span (tc, HEGEL_SPAN_OPTIONAL);
  if (hegel_draw_int (tc, 0, 1)) {
    int               i;
    n = hegel_draw_int (tc, 0, 8);
    t->label = (char *) malloc ((size_t) n + 1);
    for (i = 0; i < n; i ++) {
      t->label[i] = (char) hegel_draw_int (tc, 'a', 'z');
    }
    t->label[n] = 0;
  }
  hegel_stop_span (tc, 0);

  /* Optional left subtree */
  hegel_start_span (tc, HEGEL_SPAN_OPTIONAL);
  if (depth > 0 && hegel_draw_int (tc, 0, 1)) {
    t->left = gen_tree (tc, depth - 1);
  }
  hegel_stop_span (tc, 0);

  /* Optional right subtree */
  hegel_start_span (tc, HEGEL_SPAN_OPTIONAL);
  if (depth > 0 && hegel_draw_int (tc, 0, 1)) {
    t->right = gen_tree (tc, depth - 1);
  }
  hegel_stop_span (tc, 0);

  hegel_stop_span (tc, 0); /* close the node span */
  return (t);
}

static
void
test_roundtrip (
hegel_testcase *            tc)
{
  Tree *              original;
  Tree *              parsed;
  char *              json;
  int                 eq;
  char                msg[1024];

  original = gen_tree (tc, MAX_DEPTH);
  json     = tree_to_json (original);
  parsed   = json_to_tree (json);
  eq       = trees_equal (original, parsed);

  if (! eq) {
    snprintf (msg, sizeof (msg),
              "round-trip mismatch, json=%s", json);
    /* In fork mode, hegel_fail _exit()s the child — leaks here are
    ** harmless because the process is about to die. */
    hegel_fail (msg);
  }

  tree_free (original);
  tree_free (parsed);
  free (json);
}

/* ---- Layer 3: runner ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing JSON round-trip on random trees with optional branches...\n");
  hegel_run_test (test_roundtrip);
  printf ("PASSED\n");

  return (0);
}
