/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: same Tree JSON round-trip as test_span_tree_json_passes.c,
** but using the hegel_gen.h schema helpers instead of hand-written
** gen_tree().  This is the validation that the three-layer architecture
** (schema → shape → value) actually works and produces correct,
** shrinkable, automatically-freed structured data.
**
** The schema definition is ~10 lines.  Compare to the ~50-line
** hand-rolled gen_tree() in test_span_tree_json_passes.c.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ---- Layer 1: Tree struct + JSON codec ----
** (same codec as test_span_tree_json_passes.c) */

typedef struct Tree {
  int                 val;
  char *              label;
  struct Tree *       left;
  struct Tree *       right;
} Tree;

/* -- free (manual, for the parsed copy only) -- */

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

/* -- compare -- */

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

static void buf_grow (char ** out, size_t * cap, size_t need) {
  while (need + 1 > *cap) {
    *cap = (*cap == 0) ? 64 : (*cap * 2);
    *out = (char *) realloc (*out, *cap);
  }
}

static void buf_emit (char ** out, size_t * cap, size_t * len,
                      const char * s) {
  size_t n = strlen (s);
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

  if (t == NULL) { buf_emit (out, cap, len, "null"); return; }
  snprintf (tmp, sizeof (tmp), "{\"v\":%d,\"l\":", t->val);
  buf_emit (out, cap, len, tmp);
  if (t->label != NULL) {
    buf_emit (out, cap, len, "\"");
    buf_emit (out, cap, len, t->label);
    buf_emit (out, cap, len, "\"");
  } else {
    buf_emit (out, cap, len, "null");
  }
  buf_emit (out, cap, len, ",\"L\":");
  serialize_node (t->left, out, cap, len);
  buf_emit (out, cap, len, ",\"R\":");
  serialize_node (t->right, out, cap, len);
  buf_emit (out, cap, len, "}");
}

static char * tree_to_json (Tree * t) {
  char * out = NULL; size_t cap = 0, len = 0;
  serialize_node (t, &out, &cap, &len);
  return (out);
}

/* -- parse -- */

typedef struct { const char * p; } parser;

static int expect_char (parser * ps, char c) {
  if (*ps->p == c) { ps->p ++; return (1); }
  return (0);
}

static int expect_lit (parser * ps, const char * lit) {
  size_t n = strlen (lit);
  if (strncmp (ps->p, lit, n) != 0) return (0);
  ps->p += n;
  return (1);
}

static Tree * parse_node (parser * ps) {
  Tree * t; const char * start; char * end; size_t n;
  if (expect_lit (ps, "null")) return (NULL);
  if (! expect_char (ps, '{')) return (NULL);
  t = (Tree *) calloc (1, sizeof (Tree));
  expect_lit (ps, "\"v\":");
  t->val = (int) strtol (ps->p, &end, 10);
  ps->p = end;
  expect_char (ps, ',');
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
  expect_lit (ps, "\"L\":");
  t->left = parse_node (ps);
  expect_char (ps, ',');
  expect_lit (ps, "\"R\":");
  t->right = parse_node (ps);
  expect_char (ps, '}');
  return (t);
}

static Tree * json_to_tree (const char * json) {
  parser ps; ps.p = json;
  return (parse_node (&ps));
}

/* ---- Layer 2: the schema + hegel test ---- */

/* This is the whole point: define the schema ONCE, draw from it.
** Compare this to the ~50 lines of hand-rolled gen_tree(). */
static hegel_schema_t tree_schema;

static
void
init_schema (void)
{
  tree_schema = HEGEL_STRUCT (Tree,
      HEGEL_INT  (-1000, 1000),
      HEGEL_OPTIONAL (hegel_schema_text (0, 8)),
      HEGEL_SELF (),
      HEGEL_SELF ());
}

static
void
test_roundtrip (
hegel_testcase *            tc)
{
  Tree *              original;
  Tree *              parsed;
  char *              json;
  hegel_shape *       shape;
  int                 eq;
  char                msg[1024];

  shape = hegel_schema_draw (tc, tree_schema, (void **) &original);

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
  hegel_shape_free (shape); /* frees original + all its children */
}

/* ---- Array test: Bag with int array ---- */

typedef struct Bag {
  int *               items;
  int                 n_items;
  int                 tag;
} Bag;

static hegel_schema_t bag_schema;

static
void
init_bag_schema (void)
{
  bag_schema = HEGEL_STRUCT (Bag,
      HEGEL_ARRAY (hegel_schema_int_range (0, 100), 0, 10),
      HEGEL_INT (0, 3));
}

static
void
test_bag (
hegel_testcase *            tc)
{
  Bag *               b;
  hegel_shape *       shape;
  int                 i;

  shape = hegel_schema_draw (tc, bag_schema, (void **) &b);

  /* Verify all items are in range and count is consistent. */
  HEGEL_ASSERT (b->n_items >= 0 && b->n_items <= 10,
                "n_items=%d out of [0,10]", b->n_items);
  HEGEL_ASSERT (b->tag >= 0 && b->tag <= 3,
                "tag=%d out of [0,3]", b->tag);
  for (i = 0; i < b->n_items; i ++) {
    HEGEL_ASSERT (b->items[i] >= 0 && b->items[i] <= 100,
                  "items[%d]=%d out of [0,100]", i, b->items[i]);
  }

  hegel_shape_free (shape);
}

/* ---- Multi-type test: exercise typed integer macros ---- */

typedef struct Sensor {
  uint8_t             id;
  int16_t             temp;       /* celsius × 10 */
  uint32_t            serial;
  int64_t             timestamp;
  float               voltage;
  double              latitude;
} Sensor;

static hegel_schema_t sensor_schema;

static
void
init_sensor_schema (void)
{
  sensor_schema = HEGEL_STRUCT (Sensor,
      HEGEL_U8  (),
      HEGEL_I16 (-400, 850),
      HEGEL_U32 (),
      HEGEL_I64 (),
      HEGEL_FLOAT  (0.0, 5.0),
      HEGEL_DOUBLE (-90.0, 90.0));
}

static
void
test_sensor (
hegel_testcase *            tc)
{
  Sensor *            s;
  hegel_shape *       shape;

  shape = hegel_schema_draw (tc, sensor_schema, (void **) &s);

  HEGEL_ASSERT (s->id <= UINT8_MAX,
                "id=%u out of u8 range", (unsigned) s->id);
  HEGEL_ASSERT (s->temp >= -400 && s->temp <= 850,
                "temp=%d out of [-400,850]", (int) s->temp);
  HEGEL_ASSERT (s->serial <= UINT32_MAX,
                "serial=%u out of u32 range", (unsigned) s->serial);
  HEGEL_ASSERT (s->voltage >= 0.0f && s->voltage <= 5.0f,
                "voltage=%f out of [0,5]", (double) s->voltage);
  HEGEL_ASSERT (s->latitude >= -90.0 && s->latitude <= 90.0,
                "latitude=%f out of [-90,90]", s->latitude);

  hegel_shape_free (shape);
}

/* ---- Layer 3: runner ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  init_schema ();
  printf ("Testing JSON round-trip with hegel_gen.h schema helpers...\n");
  hegel_run_test (test_roundtrip);
  printf ("  tree round-trip PASSED\n");

  init_bag_schema ();
  printf ("Testing Bag with int array...\n");
  hegel_run_test (test_bag);
  printf ("  bag array PASSED\n");

  init_sensor_schema ();
  printf ("Testing Sensor with typed integer/float fields...\n");
  hegel_run_test (test_sensor);
  printf ("  sensor types PASSED\n");

  hegel_schema_free (tree_schema);
  hegel_schema_free (bag_schema);
  hegel_schema_free (sensor_schema);
  return (0);
}
