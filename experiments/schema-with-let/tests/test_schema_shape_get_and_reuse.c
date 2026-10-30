/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_SHAPE_GET offset-based accessor with the positional
** HEGEL_STRUCT API, exercising HEGEL_INLINE_REF (inline-by-value
** sub-struct with a shared schema).
**
** Verifies:
**   1. HEGEL_INLINE_REF lays out a nested RGB at offset 0 and another
**      at offset 3 inside Palette; nested sizeof assert fires if the
**      sub-schema doesn't match sizeof(RGB).
**   2. HEGEL_SHAPE_GET(sh, T, f) resolves through nested struct
**      shapes.  offsetof(Palette, fg.r) = 0 must find the scalar
**      for r, not the wrapper struct shape for fg.
**   3. hegel_schema_ref + HEGEL_INLINE_REF cleanly shares the same
**      RGB sub-schema across two parent fields.
**
** Layer 1 (nominal): no external function — the property IS that
** the bindings layer works end-to-end through nested shapes.
** Layer 2: build the Palette schema with one shared rgb sub-schema
** plugged in twice via HEGEL_INLINE_REF, draw, and walk into every
** leaf via HEGEL_SHAPE_GET.
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

static hegel_schema_t rgb_schema;
static hegel_schema_t palette_schema;

static void
init_schema (void)
{
  rgb_schema = HEGEL_STRUCT (RGB,
      HEGEL_U8 (), HEGEL_U8 (), HEGEL_U8 ());
  /* Share rgb_schema across two Palette fields: one ref per extra
  ** use, matching the pattern for any other shared schema. */
  hegel_schema_ref (rgb_schema);
  palette_schema = HEGEL_STRUCT (Palette,
      HEGEL_INLINE_REF (RGB, rgb_schema),
      HEGEL_INLINE_REF (RGB, rgb_schema));
}

static void
test_palette (hegel_testcase * tc)
{
  Palette *      p;
  hegel_shape *  sh;
  hegel_shape *  fg_r_shape;
  hegel_shape *  fg_g_shape;
  hegel_shape *  bg_g_shape;
  hegel_shape *  bg_b_shape;
  hegel_shape *  missing;

  sh = hegel_schema_draw (tc, palette_schema, (void **) &p);

  /* fg.r at offset 0: exact-match pass 1 on the outer Palette
  ** binding, then descend with sub-offset 0 into the RGB struct
  ** shape to return the scalar for r (not the wrapper struct). */
  fg_r_shape = HEGEL_SHAPE_GET (sh, Palette, fg.r);
  HEGEL_ASSERT (fg_r_shape != NULL, "HEGEL_SHAPE_GET returned NULL for fg.r");
  HEGEL_ASSERT (fg_r_shape->kind == HEGEL_SHAPE_SCALAR,
                "fg.r shape kind = %d (want SCALAR)", (int) fg_r_shape->kind);

  /* fg.g at offset 1: no exact match at the top level, Pass 2
  ** recurses into the inline RGB at offset 0 with sub-offset 1. */
  fg_g_shape = HEGEL_SHAPE_GET (sh, Palette, fg.g);
  HEGEL_ASSERT (fg_g_shape != NULL, "HEGEL_SHAPE_GET returned NULL for fg.g");
  HEGEL_ASSERT (fg_g_shape->kind == HEGEL_SHAPE_SCALAR,
                "fg.g shape kind = %d (want SCALAR)", (int) fg_g_shape->kind);

  /* bg.g at offset 4: Pass 2 must recurse into the SECOND inline
  ** RGB (at offset 3) with sub-offset 1, not the first. */
  bg_g_shape = HEGEL_SHAPE_GET (sh, Palette, bg.g);
  HEGEL_ASSERT (bg_g_shape != NULL, "HEGEL_SHAPE_GET returned NULL for bg.g");
  HEGEL_ASSERT (bg_g_shape->kind == HEGEL_SHAPE_SCALAR,
                "bg.g shape kind = %d (want SCALAR)", (int) bg_g_shape->kind);

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
