/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: variant/union generation via hegel_gen.h schema helpers.
**
** Exercises:
**   1. HEGEL_UNION — tagged union with inline data
**   2. HEGEL_UNION_UNTAGGED — tag lives in shape tree only
**   3. HEGEL_VARIANT — tag + pointer to separate struct
**   4. HEGEL_ARRAY_INLINE — contiguous array of inline structs
**   5. Shape accessors — hegel_shape_tag, hegel_shape_is_some, etc.
**
** This test uses the wrapper API (hegel_schema_t) — no raw pointers
** in user code, no trailing NULL in variadic macros.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ================================================================
** Test 1: HEGEL_UNION — tagged union, inline
** ================================================================ */

typedef struct {
  int               tag;  /* 0 = circle, 1 = rect */
  union {
    struct { double radius; }               circle;
    struct { double width; double height; }  rect;
  } u;
} Shape;

static hegel_schema_t shape_schema;

static void init_shape_schema (void) {
  shape_schema = HEGEL_STRUCT (Shape,
      HEGEL_UNION (
          HEGEL_CASE (HEGEL_DOUBLE (0.1, 100.0)),
          HEGEL_CASE (HEGEL_DOUBLE (0.1, 100.0),
                      HEGEL_DOUBLE (0.1, 100.0))));
}

static void test_union_tagged (hegel_testcase * tc) {
  Shape *       s;
  hegel_shape * sh = hegel_schema_draw (tc, shape_schema, (void **) &s);

  HEGEL_ASSERT (s->tag == 0 || s->tag == 1,
                "tag=%d not in [0,1]", s->tag);
  if (s->tag == 0) {
    HEGEL_ASSERT (s->u.circle.radius >= 0.1 && s->u.circle.radius <= 100.0,
                  "radius=%f", s->u.circle.radius);
  } else {
    HEGEL_ASSERT (s->u.rect.width >= 0.1 && s->u.rect.width <= 100.0,
                  "width=%f", s->u.rect.width);
    HEGEL_ASSERT (s->u.rect.height >= 0.1 && s->u.rect.height <= 100.0,
                  "height=%f", s->u.rect.height);
  }

  int shape_tag = hegel_shape_tag (hegel_shape_field (sh, 0));
  HEGEL_ASSERT (shape_tag == s->tag,
                "shape_tag=%d struct_tag=%d", shape_tag, s->tag);

  hegel_shape_free (sh);
}

/* ================================================================
** Test 2: HEGEL_UNION_UNTAGGED — tag in shape tree only
** ================================================================ */

typedef union {
  struct { double radius; }               circle;
  struct { double width; double height; }  rect;
} RawShape;

static hegel_schema_t raw_shape_schema;

static void init_raw_shape_schema (void) {
  raw_shape_schema = HEGEL_STRUCT (RawShape,
      HEGEL_UNION_UNTAGGED (
          HEGEL_CASE (HEGEL_DOUBLE (0.1, 100.0)),
          HEGEL_CASE (HEGEL_DOUBLE (0.1, 100.0),
                      HEGEL_DOUBLE (0.1, 100.0))));
}

static void test_union_untagged (hegel_testcase * tc) {
  RawShape *    s;
  hegel_shape * sh = hegel_schema_draw (tc, raw_shape_schema, (void **) &s);

  int tag = hegel_shape_tag (hegel_shape_field (sh, 0));
  HEGEL_ASSERT (tag == 0 || tag == 1, "tag=%d", tag);

  if (tag == 0) {
    HEGEL_ASSERT (s->circle.radius >= 0.1 && s->circle.radius <= 100.0,
                  "radius=%f", s->circle.radius);
  } else {
    HEGEL_ASSERT (s->rect.width >= 0.1 && s->rect.width <= 100.0,
                  "width=%f", s->rect.width);
  }

  hegel_shape_free (sh);
}

/* ================================================================
** Test 3: HEGEL_VARIANT — tag + pointer to separate struct
** ================================================================ */

typedef struct { double radius; } Circle;
typedef struct { double width; double height; } Rect;

typedef struct {
  int             tag;
  void *          value;
} ShapeVar;

static hegel_schema_t shapevar_schema;

static void init_shapevar_schema (void) {
  hegel_schema_t circle_s = HEGEL_STRUCT (Circle,
      HEGEL_DOUBLE (0.1, 100.0));
  hegel_schema_t rect_s = HEGEL_STRUCT (Rect,
      HEGEL_DOUBLE (0.1, 100.0),
      HEGEL_DOUBLE (0.1, 100.0));
  shapevar_schema = HEGEL_STRUCT (ShapeVar,
      HEGEL_VARIANT (circle_s, rect_s));
}

static void test_variant (hegel_testcase * tc) {
  ShapeVar *    sv;
  hegel_shape * sh = hegel_schema_draw (tc, shapevar_schema, (void **) &sv);

  HEGEL_ASSERT (sv->tag == 0 || sv->tag == 1,
                "tag=%d", sv->tag);
  HEGEL_ASSERT (sv->value != NULL, "value is NULL");

  if (sv->tag == 0) {
    Circle * c = (Circle *) sv->value;
    HEGEL_ASSERT (c->radius >= 0.1 && c->radius <= 100.0,
                  "radius=%f", c->radius);
  } else {
    Rect * r = (Rect *) sv->value;
    HEGEL_ASSERT (r->width >= 0.1 && r->width <= 100.0,
                  "width=%f", r->width);
    HEGEL_ASSERT (r->height >= 0.1 && r->height <= 100.0,
                  "height=%f", r->height);
  }

  hegel_shape_free (sh);
}

/* ================================================================
** Test 4: HEGEL_ARRAY_INLINE — contiguous structs
** ================================================================ */

typedef struct { int x; int y; } Point;

typedef struct {
  Point *         points;
  int             n_points;
} Path;

static hegel_schema_t path_schema;

static void init_path_schema (void) {
  hegel_schema_t point_s = HEGEL_STRUCT (Point,
      HEGEL_INT (-100, 100),
      HEGEL_INT (-100, 100));
  path_schema = HEGEL_STRUCT (Path,
      HEGEL_ARRAY_INLINE (point_s, sizeof (Point), 1, 8));
}

static void test_array_inline (hegel_testcase * tc) {
  Path *        p;
  hegel_shape * sh = hegel_schema_draw (tc, path_schema, (void **) &p);

  HEGEL_ASSERT (p->n_points >= 1 && p->n_points <= 8,
                "n_points=%d", p->n_points);
  for (int i = 0; i < p->n_points; i ++) {
    HEGEL_ASSERT (p->points[i].x >= -100 && p->points[i].x <= 100,
                  "points[%d].x=%d", i, p->points[i].x);
    HEGEL_ASSERT (p->points[i].y >= -100 && p->points[i].y <= 100,
                  "points[%d].y=%d", i, p->points[i].y);
  }

  int len = hegel_shape_array_len (hegel_shape_field (sh, 0));
  HEGEL_ASSERT (len == p->n_points,
                "shape_len=%d struct_n=%d", len, p->n_points);

  hegel_shape_free (sh);
}

/* ================================================================
** Runner
** ================================================================ */

int main (void) {
  init_shape_schema ();
  printf ("  union tagged...\n");
  hegel_run_test (test_union_tagged);
  printf ("    PASSED\n");

  init_raw_shape_schema ();
  printf ("  union untagged...\n");
  hegel_run_test (test_union_untagged);
  printf ("    PASSED\n");

  init_shapevar_schema ();
  printf ("  variant (ptr)...\n");
  hegel_run_test (test_variant);
  printf ("    PASSED\n");

  init_path_schema ();
  printf ("  array inline...\n");
  hegel_run_test (test_array_inline);
  printf ("    PASSED\n");

  hegel_schema_free (shape_schema);
  hegel_schema_free (raw_shape_schema);
  hegel_schema_free (shapevar_schema);
  hegel_schema_free (path_schema);
  return (0);
}
