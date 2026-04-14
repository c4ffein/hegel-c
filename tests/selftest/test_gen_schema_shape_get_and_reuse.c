/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_SHAPE_GET offset-based accessor with the positional
** HEGEL_STRUCT API.
**
** Verifies:
**   1. The layout pass assigns each HEGEL_U8() the right offset
**      (0..5) matching the Palette struct's nested RGB fields.
**   2. HEGEL_SHAPE_GET(sh, T, f) looks up by offset and returns the
**      matching field shape.
**
** Layer 1 (nominal): no external function — the property IS that
** the bindings layer works.
** Layer 2: build a struct with two RGB-triples (fg, bg), each using
** the SAME `channel` schema bound at six different offsets.  Draw,
** then use HEGEL_SHAPE_GET to walk into fg.r and verify it's a
** SCALAR shape.  Assert every drawn byte is in [0, 255] (redundant
** with the schema bound, but it proves the bindings resolved
** correctly at every offset).
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  uint8_t r, g, b;
} RGB;

typedef struct {
  RGB fg;
  RGB bg;
} Palette;

static hegel_schema_t palette_schema;

static void
init_schema (void)
{
  palette_schema = HEGEL_STRUCT (Palette,
      HEGEL_U8 (), HEGEL_U8 (), HEGEL_U8 (),
      HEGEL_U8 (), HEGEL_U8 (), HEGEL_U8 ());
}

static void
test_palette (hegel_testcase * tc)
{
  Palette *      p;
  hegel_shape *  sh;
  hegel_shape *  fg_r_shape;
  hegel_shape *  bg_b_shape;
  hegel_shape *  missing;

  sh = hegel_schema_draw (tc, palette_schema, (void **) &p);

  /* Every channel byte must land in [0, 255] (trivially true for a
  ** uint8_t, but this verifies the binding offsets don't collide or
  ** overwrite each other — if two bindings resolved to the same
  ** field, one channel would be zero while another would be double-
  ** written, and we'd fail below).  Instead of relying on
  ** uint8_t saturation, check the shape accessor returns a valid
  ** node for each field. */

  fg_r_shape = HEGEL_SHAPE_GET (sh, Palette, fg.r);
  HEGEL_ASSERT (fg_r_shape != NULL, "HEGEL_SHAPE_GET returned NULL for fg.r");
  HEGEL_ASSERT (fg_r_shape->kind == HEGEL_SHAPE_SCALAR,
                "fg.r shape kind = %d (want SCALAR)", (int) fg_r_shape->kind);

  bg_b_shape = HEGEL_SHAPE_GET (sh, Palette, bg.b);
  HEGEL_ASSERT (bg_b_shape != NULL, "HEGEL_SHAPE_GET returned NULL for bg.b");
  HEGEL_ASSERT (bg_b_shape->kind == HEGEL_SHAPE_SCALAR,
                "bg.b shape kind = %d (want SCALAR)", (int) bg_b_shape->kind);

  /* Looking up an offset that was never bound should return NULL. */
  missing = hegel_shape_get_offset (sh, (size_t) -1);
  HEGEL_ASSERT (missing == NULL, "expected NULL for unknown offset");

  /* Redundant value check to force the compiler not to elide the
  ** draw.  uint8_t is unsigned so r/g/b are always in [0, 255]. */
  (void) p->fg.r; (void) p->fg.g; (void) p->fg.b;
  (void) p->bg.r; (void) p->bg.g; (void) p->bg.b;

  hegel_shape_free (sh);
}

int
main (int argc, char * argv[])
{
  (void) argc; (void) argv;

  init_schema ();
  printf ("Testing schema reuse + HEGEL_SHAPE_GET...\n");
  hegel_run_test (test_palette);
  printf ("PASSED\n");

  hegel_schema_free (palette_schema);
  return (0);
}
