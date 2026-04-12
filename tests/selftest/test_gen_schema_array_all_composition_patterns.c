/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: all array composition patterns.
**
** 1. ARRAY of scalars (int)
** 2. ARRAY of pointer-to-struct (each element separately allocated)
** 3. ARRAY_INLINE of structs (contiguous)
** 4. ARRAY_INLINE of structs containing ARRAY (nested: inline outer,
**    pointer inner)
** 5. ARRAY of pointer-to-struct containing ARRAY_INLINE (pointer outer,
**    inline inner)
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ---- 1. ARRAY of scalars ---- */

typedef struct { int * vals; int n; } ScalarBag;

static hegel_schema_t scalar_bag_s;

static void test_scalar_array (hegel_testcase * tc) {
  ScalarBag * b;
  hegel_shape * sh = hegel_schema_draw (tc, scalar_bag_s, (void **) &b);
  HEGEL_ASSERT (b->n >= 0 && b->n <= 8, "n=%d", b->n);
  for (int i = 0; i < b->n; i ++)
    HEGEL_ASSERT (b->vals[i] >= -50 && b->vals[i] <= 50,
                  "vals[%d]=%d", i, b->vals[i]);
  hegel_shape_free (sh);
}

/* ---- 2. ARRAY of pointer-to-struct ---- */

typedef struct { int x; int y; } Pt;
typedef struct { Pt ** pts; int n; } PtrBag;

static hegel_schema_t ptr_bag_s;

static void test_ptr_array (hegel_testcase * tc) {
  PtrBag * b;
  hegel_shape * sh = hegel_schema_draw (tc, ptr_bag_s, (void **) &b);
  HEGEL_ASSERT (b->n >= 0 && b->n <= 6, "n=%d", b->n);
  for (int i = 0; i < b->n; i ++) {
    HEGEL_ASSERT (b->pts[i] != NULL, "pts[%d] NULL", i);
    HEGEL_ASSERT (b->pts[i]->x >= -10 && b->pts[i]->x <= 10,
                  "pts[%d]->x=%d", i, b->pts[i]->x);
    HEGEL_ASSERT (b->pts[i]->y >= -10 && b->pts[i]->y <= 10,
                  "pts[%d]->y=%d", i, b->pts[i]->y);
  }
  hegel_shape_free (sh);
}

/* ---- 3. ARRAY_INLINE of structs ---- */

typedef struct { uint8_t r; uint8_t g; uint8_t b; } Color;
typedef struct { Color * colors; int n; } Palette;

static hegel_schema_t palette_s;

static void test_inline_array (hegel_testcase * tc) {
  Palette * p;
  hegel_shape * sh = hegel_schema_draw (tc, palette_s, (void **) &p);
  HEGEL_ASSERT (p->n >= 1 && p->n <= 5, "n=%d", p->n);
  for (int i = 0; i < p->n; i ++) {
    HEGEL_ASSERT (p->colors[i].r <= 255 && p->colors[i].g <= 255
                  && p->colors[i].b <= 255,
                  "color[%d]=(%u,%u,%u)", i,
                  (unsigned) p->colors[i].r,
                  (unsigned) p->colors[i].g,
                  (unsigned) p->colors[i].b);
  }
  hegel_shape_free (sh);
}

/* ---- 4. ARRAY_INLINE of structs containing ARRAY ---- */

typedef struct { int * data; int n_data; } Chunk;
typedef struct { Chunk * chunks; int n_chunks; } ChunkList;

static hegel_schema_t chunklist_s;

static void test_inline_of_array (hegel_testcase * tc) {
  ChunkList * cl;
  hegel_shape * sh = hegel_schema_draw (tc, chunklist_s, (void **) &cl);
  HEGEL_ASSERT (cl->n_chunks >= 1 && cl->n_chunks <= 3,
                "n_chunks=%d", cl->n_chunks);
  for (int i = 0; i < cl->n_chunks; i ++) {
    HEGEL_ASSERT (cl->chunks[i].n_data >= 0 && cl->chunks[i].n_data <= 4,
                  "chunks[%d].n_data=%d", i, cl->chunks[i].n_data);
    for (int j = 0; j < cl->chunks[i].n_data; j ++)
      HEGEL_ASSERT (cl->chunks[i].data[j] >= 0
                    && cl->chunks[i].data[j] <= 9,
                    "chunks[%d].data[%d]=%d", i, j,
                    cl->chunks[i].data[j]);
  }
  hegel_shape_free (sh);
}

/* ---- 5. ARRAY of ptr-to-struct containing ARRAY_INLINE ---- */

typedef struct { Color * pixels; int n_pixels; } Sprite;
typedef struct { Sprite ** sprites; int n_sprites; } Scene;

static hegel_schema_t scene_s;

static void test_ptr_of_inline (hegel_testcase * tc) {
  Scene * sc;
  hegel_shape * sh = hegel_schema_draw (tc, scene_s, (void **) &sc);
  HEGEL_ASSERT (sc->n_sprites >= 1 && sc->n_sprites <= 3,
                "n_sprites=%d", sc->n_sprites);
  for (int i = 0; i < sc->n_sprites; i ++) {
    HEGEL_ASSERT (sc->sprites[i] != NULL, "sprites[%d] NULL", i);
    HEGEL_ASSERT (sc->sprites[i]->n_pixels >= 1
                  && sc->sprites[i]->n_pixels <= 4,
                  "sprites[%d]->n_pixels=%d", i,
                  sc->sprites[i]->n_pixels);
    for (int j = 0; j < sc->sprites[i]->n_pixels; j ++) {
      Color c = sc->sprites[i]->pixels[j];
      HEGEL_ASSERT (c.r <= 255 && c.g <= 255 && c.b <= 255,
                    "sprites[%d].pixels[%d]=(%u,%u,%u)", i, j,
                    (unsigned) c.r, (unsigned) c.g, (unsigned) c.b);
    }
  }
  hegel_shape_free (sh);
}

/* ---- Runner ---- */

int main (void) {
  hegel_schema_t pt_s = hegel_schema_struct (sizeof (Pt),
      HEGEL_INT (Pt, x, -10, 10), HEGEL_INT (Pt, y, -10, 10));
  hegel_schema_t color_s = hegel_schema_struct (sizeof (Color),
      HEGEL_U8 (Color, r), HEGEL_U8 (Color, g), HEGEL_U8 (Color, b));
  /* color_s is used in two parents (palette_s + sprite_s).  Add one
  ** extra reference so ownership transfer works for both. */
  hegel_schema_ref (color_s);

  scalar_bag_s = hegel_schema_struct (sizeof (ScalarBag),
      HEGEL_ARRAY (ScalarBag, vals, n, hegel_schema_int_range (-50, 50), 0, 8));

  ptr_bag_s = hegel_schema_struct (sizeof (PtrBag),
      HEGEL_ARRAY (PtrBag, pts, n, pt_s, 0, 6));

  palette_s = hegel_schema_struct (sizeof (Palette),
      HEGEL_ARRAY_INLINE (Palette, colors, n, color_s, sizeof (Color), 1, 5));

  hegel_schema_t chunk_s = hegel_schema_struct (sizeof (Chunk),
      HEGEL_ARRAY (Chunk, data, n_data, hegel_schema_int_range (0, 9), 0, 4));
  chunklist_s = hegel_schema_struct (sizeof (ChunkList),
      HEGEL_ARRAY_INLINE (ChunkList, chunks, n_chunks,
                          chunk_s, sizeof (Chunk), 1, 3));

  hegel_schema_t sprite_s = hegel_schema_struct (sizeof (Sprite),
      HEGEL_ARRAY_INLINE (Sprite, pixels, n_pixels,
                          color_s, sizeof (Color), 1, 4));
  scene_s = hegel_schema_struct (sizeof (Scene),
      HEGEL_ARRAY (Scene, sprites, n_sprites, sprite_s, 1, 3));

  printf ("  scalar array...\n");
  hegel_run_test (test_scalar_array);
  printf ("    PASSED\n");

  printf ("  ptr-to-struct array...\n");
  hegel_run_test (test_ptr_array);
  printf ("    PASSED\n");

  printf ("  inline struct array...\n");
  hegel_run_test (test_inline_array);
  printf ("    PASSED\n");

  printf ("  inline-of-array (nested)...\n");
  hegel_run_test (test_inline_of_array);
  printf ("    PASSED\n");

  printf ("  ptr-of-inline (nested)...\n");
  hegel_run_test (test_ptr_of_inline);
  printf ("    PASSED\n");

  hegel_schema_free (scalar_bag_s);
  hegel_schema_free (ptr_bag_s);
  hegel_schema_free (palette_s);
  hegel_schema_free (chunklist_s);
  hegel_schema_free (scene_s);
  return (0);
}
