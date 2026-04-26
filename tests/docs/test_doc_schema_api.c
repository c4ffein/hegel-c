/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Backing tests for docs/schema-api.md.  Each section is a real,
** compilable, working example transcluded into the doc via
** /include markers.  Edit these here; docs-check enforces sync.
**
** Sections are bracketed with marker comments that start with the
** word "Section:" to help humans locate them; the /include line
** ranges point at the executable lines between those markers.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "hegel_c.h"
#include "hegel_gen.h"

static int my_function (int a, int b) { (void) a; (void) b; return 0; }

/* === Section: opener-primitive === */
static void opener_primitive (hegel_testcase * tc)
{
  int x = HEGEL_DRAW_INT (0, 100);
  int y = HEGEL_DRAW_INT (0, 100);
  HEGEL_ASSERT (my_function (x, y) >= 0, "x=%d y=%d", x, y);
}
/* === end === */

/* === Section: opener-composed === */
typedef struct { int x; int y; char * name; } Thing;
static hegel_schema_t thing_schema;

static void opener_composed (hegel_testcase * tc)
{
  Thing *             t;
  hegel_shape *       sh = HEGEL_DRAW (&t, thing_schema);
  HEGEL_ASSERT (t != NULL, "alloc");
  HEGEL_ASSERT (t->x >= 0 && t->y >= 0,
                "x=%d y=%d name=%s", t->x, t->y, t->name);
  hegel_shape_free (sh);
}
/* === end === */

/* === Section: struct-constructor === */
typedef struct { int a; int b; char * s; } MyStruct;
static hegel_schema_t mystruct_schema;

static void struct_demo (hegel_testcase * tc)
{
  MyStruct *          v;
  hegel_shape *       sh = HEGEL_DRAW (&v, mystruct_schema);
  HEGEL_ASSERT (v->a >= 0 && v->a <= 100, "a=%d", v->a);
  hegel_shape_free (sh);
}
/* === end === */

/* === Section: optional === */
typedef struct { int age; char * label; } Person;
static hegel_schema_t person_schema;

static void optional_demo (hegel_testcase * tc)
{
  Person *            p;
  hegel_shape *       sh = HEGEL_DRAW (&p, person_schema);
  HEGEL_ASSERT (p->age >= 0, "age=%d", p->age);
  /* p->label may be NULL — HEGEL_OPTIONAL flips a coin each draw. */
  hegel_shape_free (sh);
}
/* === end === */

/* === Section: inline === */
typedef struct { uint8_t r; uint8_t g; uint8_t b; } RGB;
typedef struct { RGB fg; RGB bg; } Palette;
static hegel_schema_t palette_schema;

static void inline_demo (hegel_testcase * tc)
{
  Palette *           p;
  hegel_shape *       sh = HEGEL_DRAW (&p, palette_schema);
  HEGEL_ASSERT (p != NULL, "alloc");
  hegel_shape_free (sh);
}
/* === end === */

/* === Section: self === */
typedef struct Node {
  int                 val;
  struct Node *       next;
} Node;
static hegel_schema_t node_schema;

static void self_demo (hegel_testcase * tc)
{
  Node *              n;
  hegel_shape *       sh = HEGEL_DRAW (&n, node_schema);
  HEGEL_ASSERT (n != NULL, "alloc");
  hegel_shape_free (sh);
}
/* === end === */

/* === Section: array === */
typedef struct { int * items; int n_items; } Bag;
static hegel_schema_t bag_schema;

static void array_demo (hegel_testcase * tc)
{
  Bag *               b;
  hegel_shape *       sh = HEGEL_DRAW (&b, bag_schema);
  HEGEL_ASSERT (b->n_items >= 0 && b->n_items <= 10, "n=%d", b->n_items);
  hegel_shape_free (sh);
}
/* === end === */

/* === Section: array-inline === */
typedef struct { uint8_t r; uint8_t g; uint8_t b; } Color;
typedef struct { Color * colors; int n; } PaletteArr;
static hegel_schema_t palette_arr_schema;

static void array_inline_demo (hegel_testcase * tc)
{
  PaletteArr *        p;
  hegel_shape *       sh = HEGEL_DRAW (&p, palette_arr_schema);
  HEGEL_ASSERT (p->n >= 1 && p->n <= 5, "n=%d", p->n);
  hegel_shape_free (sh);
}
/* === end === */

/* === Section: union === */
typedef struct {
  int                 tag;
  union {
    struct { double radius; }                      circle;
    struct { double w; double h; }                 rect;
  }                   u;
} Shape;
static hegel_schema_t shape_schema;

static void union_demo (hegel_testcase * tc)
{
  Shape *             s;
  hegel_shape *       sh = HEGEL_DRAW (&s, shape_schema);
  HEGEL_ASSERT (s->tag == 0 || s->tag == 1, "tag=%d", s->tag);
  hegel_shape_free (sh);
}
/* === end === */

/* === Section: variant === */
typedef struct { double radius; } Circle;
typedef struct { double w; double h; } Rect;
typedef struct { int tag; void * value; } ShapeVar;
static hegel_schema_t shapevar_schema;

static void variant_demo (hegel_testcase * tc)
{
  ShapeVar *          s;
  hegel_shape *       sh = HEGEL_DRAW (&s, shapevar_schema);
  HEGEL_ASSERT (s->value != NULL, "pointer");
  hegel_shape_free (sh);
}
/* === end === */

/* === Section: hegel-draw-scalar === */
static hegel_schema_t int_sch;

static void draw_scalar_demo (hegel_testcase * tc)
{
  int                 x;
  hegel_shape *       sh = HEGEL_DRAW (&x, int_sch);
  HEGEL_ASSERT (x >= 0, "got %d", x);
  hegel_shape_free (sh);
}
/* === end === */

/* === Section: hegel-draw-typed === */
static void draw_typed_demo (hegel_testcase * tc)
{
  int                 x = HEGEL_DRAW_INT    (0, 10);
  int                 y = HEGEL_DRAW_INT    ();           /* INT_MIN..INT_MAX */
  int64_t             a = HEGEL_DRAW_I64    (-100, 100);
  double              d = HEGEL_DRAW_DOUBLE (0.0, 1.0);
  bool                b = HEGEL_DRAW_BOOL   ();           /* no range — 0 or 1 */
  (void) x; (void) y; (void) a; (void) d; (void) b;
  HEGEL_ASSERT (x >= 0 && x <= 10, "x=%d", x);
  HEGEL_ASSERT (d >= 0.0 && d <= 1.0, "d=%f", d);
}
/* === end === */

int
main (void)
{
  thing_schema = HEGEL_STRUCT (Thing,
      HEGEL_INT  (0, 100),
      HEGEL_INT  (0, 100),
      HEGEL_TEXT (1, 20));

  mystruct_schema = HEGEL_STRUCT (MyStruct,
      HEGEL_INT  (0, 100),
      HEGEL_INT  (0, 100),
      HEGEL_TEXT (1, 20));

  person_schema = HEGEL_STRUCT (Person,
      HEGEL_INT      (0, 120),
      HEGEL_OPTIONAL (hegel_schema_text (1, 8)));

  palette_schema = HEGEL_STRUCT (Palette,
      HEGEL_INLINE (RGB, HEGEL_U8 (), HEGEL_U8 (), HEGEL_U8 ()),
      HEGEL_INLINE (RGB, HEGEL_U8 (), HEGEL_U8 (), HEGEL_U8 ()));

  node_schema = HEGEL_STRUCT (Node,
      HEGEL_INT  (0, 100),
      HEGEL_SELF ());

  {
    HEGEL_BINDING (n_items);
    bag_schema = HEGEL_STRUCT (Bag,
        HEGEL_LET    (n_items, HEGEL_INT (0, 10)),
        HEGEL_ARR_OF (HEGEL_USE (n_items), HEGEL_INT (0, 100)),
        HEGEL_USE    (n_items));
  }

  hegel_schema_t color_s = HEGEL_STRUCT (Color,
      HEGEL_U8 (), HEGEL_U8 (), HEGEL_U8 ());
  palette_arr_schema = HEGEL_STRUCT (PaletteArr,
      HEGEL_ARRAY_INLINE (color_s, sizeof (Color), 1, 5));

  shape_schema = HEGEL_STRUCT (Shape,
      HEGEL_UNION (
          HEGEL_CASE (HEGEL_DOUBLE (0.1, 100.0)),
          HEGEL_CASE (HEGEL_DOUBLE (0.1, 100.0),
                      HEGEL_DOUBLE (0.1, 100.0))));

  hegel_schema_t circle_s = HEGEL_STRUCT (Circle,
      HEGEL_DOUBLE (0.1, 100.0));
  hegel_schema_t rect_s = HEGEL_STRUCT (Rect,
      HEGEL_DOUBLE (0.1, 100.0),
      HEGEL_DOUBLE (0.1, 100.0));
  shapevar_schema = HEGEL_STRUCT (ShapeVar,
      HEGEL_VARIANT (circle_s, rect_s));

  int_sch = hegel_schema_int_range (0, 50);

  hegel_run_test (opener_primitive);
  hegel_run_test (opener_composed);
  hegel_run_test (struct_demo);
  hegel_run_test (optional_demo);
  hegel_run_test (inline_demo);
  hegel_run_test (self_demo);
  hegel_run_test (array_demo);
  hegel_run_test (array_inline_demo);
  hegel_run_test (union_demo);
  hegel_run_test (variant_demo);
  hegel_run_test (draw_scalar_demo);
  hegel_run_test (draw_typed_demo);

  hegel_schema_free (thing_schema);
  hegel_schema_free (mystruct_schema);
  hegel_schema_free (person_schema);
  hegel_schema_free (palette_schema);
  hegel_schema_free (node_schema);
  hegel_schema_free (bag_schema);
  hegel_schema_free (palette_arr_schema);
  hegel_schema_free (shape_schema);
  hegel_schema_free (shapevar_schema);
  hegel_schema_free (int_sch);

  return 0;
}
