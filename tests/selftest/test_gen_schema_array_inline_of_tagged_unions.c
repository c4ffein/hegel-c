/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: contiguous inline array where each element is a tagged union.
**
** Every array slot is `sizeof(Shape)` bytes — the union pads to the
** size of the biggest variant, so slot stride is uniform even though
** each slot can independently hold a circle (one double) or a rect
** (two doubles).  No pointer chasing, one allocation for the whole
** array, per-element polymorphism.
**
** This is the "inline polymorphic array" pattern — common in real C
** code (think command lists, event queues, AST nodes with fixed
** upper-bound variants).  Without the HEGEL_SCH_UNION elem support
** in HEGEL_ARRAY_INLINE, users would have to fall back to pointer
** arrays with separate allocations, losing cache locality.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ---- Types ---- */

typedef struct {
  int               tag;   /* 0 = circle, 1 = rect */
  union {
    struct { double radius; }                circle;
    struct { double width; double height; }  rect;
  } u;
} Shape;

typedef struct {
  Shape *           shapes;
  int               n_shapes;
} Gallery;

/* ---- Schema ---- */

static hegel_schema_t gallery_schema;

static
void
init_schema (void)
{
  /* A standalone tagged-union schema.  Used as the element type for
  ** ARRAY_INLINE below — each array slot is sizeof(Shape) bytes, and
  ** the union draw writes the tag + the chosen variant's fields into
  ** each slot independently. */
  hegel_schema_t shape_union = HEGEL_UNION (Shape, tag,
      HEGEL_CASE (HEGEL_DOUBLE (Shape, u.circle.radius, 0.1, 100.0)),
      HEGEL_CASE (HEGEL_DOUBLE (Shape, u.rect.width,  0.1, 100.0),
                  HEGEL_DOUBLE (Shape, u.rect.height, 0.1, 100.0)));

  gallery_schema = hegel_schema_struct (sizeof (Gallery),
      HEGEL_ARRAY_INLINE (Gallery, shapes, n_shapes,
                          shape_union, sizeof (Shape), 1, 6));
}

/* ---- Test ---- */

static
void
test_gallery (
hegel_testcase *            tc)
{
  Gallery *         g;
  hegel_shape *     sh;
  int               i;
  int               n_circles = 0;
  int               n_rects   = 0;

  sh = hegel_schema_draw (tc, gallery_schema, (void **) &g);

  HEGEL_ASSERT (g->n_shapes >= 1 && g->n_shapes <= 6,
                "n_shapes=%d", g->n_shapes);

  for (i = 0; i < g->n_shapes; i ++) {
    Shape * s = &g->shapes[i];

    HEGEL_ASSERT (s->tag == 0 || s->tag == 1,
                  "shapes[%d].tag=%d", i, s->tag);

    if (s->tag == 0) {
      n_circles ++;
      HEGEL_ASSERT (s->u.circle.radius >= 0.1
                    && s->u.circle.radius <= 100.0,
                    "shapes[%d].circle.radius=%f",
                    i, s->u.circle.radius);
    } else {
      n_rects ++;
      HEGEL_ASSERT (s->u.rect.width >= 0.1
                    && s->u.rect.width <= 100.0,
                    "shapes[%d].rect.width=%f",
                    i, s->u.rect.width);
      HEGEL_ASSERT (s->u.rect.height >= 0.1
                    && s->u.rect.height <= 100.0,
                    "shapes[%d].rect.height=%f",
                    i, s->u.rect.height);
    }
  }

  HEGEL_ASSERT (n_circles + n_rects == g->n_shapes,
                "circles=%d rects=%d total=%d",
                n_circles, n_rects, g->n_shapes);

  /* Shape-tree length accessor must match struct count. */
  {
    int len = hegel_shape_array_len (hegel_shape_field (sh, 0));
    HEGEL_ASSERT (len == g->n_shapes,
                  "shape_len=%d struct_n=%d", len, g->n_shapes);
  }

  hegel_shape_free (sh);
}

/* ---- Runner ---- */

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  init_schema ();
  printf ("Testing ARRAY_INLINE of tagged unions...\n");
  hegel_run_test (test_gallery);
  printf ("  PASSED\n");

  hegel_schema_free (gallery_schema);
  return (0);
}
