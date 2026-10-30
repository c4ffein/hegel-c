/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_INLINE — inline-by-value sub-struct as a positional
** field.
**
** Three sub-tests:
**   1. Fresh-build form.  A Palette with two separate HEGEL_INLINE
**      calls (each builds its own RGB sub-schema).  Proves the
**      nested sizeof assertion chain at schema-build time and
**      per-element draw at draw time.
**   2. Mixed inline + pointer-to-struct.  A Sprite with one inline
**      RGB and one HEGEL_OPTIONAL(Point) field, proving the two
**      field kinds coexist cleanly in one parent struct.
**   3. Triple nesting.  A Doll containing a Box containing a Gem,
**      all by value, proving the recursive sub-schema build works
**      and HEGEL_SHAPE_GET resolves through two levels of nesting.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ---- Sub-test 1: fresh HEGEL_INLINE ---- */

typedef struct { uint8_t r, g, b; }  RGB;
typedef struct { RGB fg; RGB bg; }   Palette;

static hegel_schema_t palette_schema;

static void
test_palette_inline (hegel_testcase * tc)
{
  Palette *     p;
  hegel_shape * sh;
  hegel_shape * fg_r;
  hegel_shape * bg_b;

  sh = hegel_schema_draw (tc, palette_schema, (void **) &p);

  fg_r = HEGEL_SHAPE_GET (sh, Palette, fg.r);
  HEGEL_ASSERT (fg_r != NULL && fg_r->kind == HEGEL_SHAPE_SCALAR,
                "fg.r lookup failed");

  bg_b = HEGEL_SHAPE_GET (sh, Palette, bg.b);
  HEGEL_ASSERT (bg_b != NULL && bg_b->kind == HEGEL_SHAPE_SCALAR,
                "bg.b lookup failed");

  /* uint8_t — always in [0, 255], prevents dead-code elimination. */
  (void) p->fg.r; (void) p->fg.g; (void) p->fg.b;
  (void) p->bg.r; (void) p->bg.g; (void) p->bg.b;

  hegel_shape_free (sh);
}

/* ---- Sub-test 2: inline + optional pointer in one parent ---- */

typedef struct { int x, y; }          Point;
typedef struct {
  RGB      color;     /* inline by value */
  Point *  origin;    /* separate allocation, possibly NULL */
} Sprite;

static hegel_schema_t point_schema;
static hegel_schema_t sprite_schema;

static void
test_sprite_mixed (hegel_testcase * tc)
{
  Sprite *      s;
  hegel_shape * sh;
  hegel_shape * color_r;

  sh = hegel_schema_draw (tc, sprite_schema, (void **) &s);

  /* Inline color field — resolved through the nested RGB shape. */
  color_r = HEGEL_SHAPE_GET (sh, Sprite, color.r);
  HEGEL_ASSERT (color_r != NULL && color_r->kind == HEGEL_SHAPE_SCALAR,
                "Sprite.color.r lookup failed");

  /* origin is optional — when present, it must be a valid pointer
  ** containing coordinates in the [-1000, 1000] schema range.  When
  ** absent, it must be NULL (no freed-memory access on cleanup). */
  if (s->origin != NULL) {
    HEGEL_ASSERT (s->origin->x >= -1000 && s->origin->x <= 1000,
                  "origin->x=%d out of range", s->origin->x);
    HEGEL_ASSERT (s->origin->y >= -1000 && s->origin->y <= 1000,
                  "origin->y=%d out of range", s->origin->y);
  }

  hegel_shape_free (sh);
}

/* ---- Sub-test 3: three levels of nesting ---- */

typedef struct { uint32_t karat; }     Gem;
typedef struct { Gem g; uint8_t lid; } Box;
typedef struct { Box b; uint8_t age; } Doll;

static hegel_schema_t doll_schema;

static void
test_doll_triple_nested (hegel_testcase * tc)
{
  Doll *        d;
  hegel_shape * sh;
  hegel_shape * karat;
  hegel_shape * lid;
  hegel_shape * age;

  sh = hegel_schema_draw (tc, doll_schema, (void **) &d);

  /* Deepest leaf: Doll.b.g.karat.  Two pass-2 recursions +
  ** one pass-1 exact match on the innermost binding. */
  karat = HEGEL_SHAPE_GET (sh, Doll, b.g.karat);
  HEGEL_ASSERT (karat != NULL && karat->kind == HEGEL_SHAPE_SCALAR,
                "Doll.b.g.karat lookup failed");

  lid = HEGEL_SHAPE_GET (sh, Doll, b.lid);
  HEGEL_ASSERT (lid != NULL && lid->kind == HEGEL_SHAPE_SCALAR,
                "Doll.b.lid lookup failed");

  age = HEGEL_SHAPE_GET (sh, Doll, age);
  HEGEL_ASSERT (age != NULL && age->kind == HEGEL_SHAPE_SCALAR,
                "Doll.age lookup failed");

  HEGEL_ASSERT (d->b.g.karat <= 24, "karat=%u out of range", d->b.g.karat);
  (void) d->b.lid; (void) d->age;

  hegel_shape_free (sh);
}

int
main (int argc, char * argv[])
{
  (void) argc; (void) argv;

  /* Sub-test 1: fresh HEGEL_INLINE, two independent sub-schemas. */
  palette_schema = HEGEL_STRUCT (Palette,
      HEGEL_INLINE (RGB, HEGEL_U8 (), HEGEL_U8 (), HEGEL_U8 ()),
      HEGEL_INLINE (RGB, HEGEL_U8 (), HEGEL_U8 (), HEGEL_U8 ()));
  printf ("Testing HEGEL_INLINE fresh-build (Palette)...\n");
  hegel_run_test (test_palette_inline);

  /* Sub-test 2: mixed inline + optional pointer. */
  point_schema  = HEGEL_STRUCT (Point,
      HEGEL_INT (-1000, 1000), HEGEL_INT (-1000, 1000));
  sprite_schema = HEGEL_STRUCT (Sprite,
      HEGEL_INLINE   (RGB, HEGEL_U8 (), HEGEL_U8 (), HEGEL_U8 ()),
      HEGEL_OPTIONAL (point_schema));
  printf ("Testing HEGEL_INLINE + HEGEL_OPTIONAL (Sprite)...\n");
  hegel_run_test (test_sprite_mixed);

  /* Sub-test 3: three levels of nested inline structs. */
  doll_schema = HEGEL_STRUCT (Doll,
      HEGEL_INLINE (Box,
          HEGEL_INLINE (Gem, HEGEL_U32 (0, 24)),
          HEGEL_U8 ()),
      HEGEL_U8 ());
  printf ("Testing HEGEL_INLINE triple nesting (Doll)...\n");
  hegel_run_test (test_doll_triple_nested);

  printf ("PASSED\n");

  hegel_schema_free (palette_schema);
  hegel_schema_free (sprite_schema);
  hegel_schema_free (doll_schema);
  return (0);
}
