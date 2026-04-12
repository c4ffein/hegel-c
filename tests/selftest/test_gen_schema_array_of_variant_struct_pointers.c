/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: complex composition — array of pointers to variant structs.
**
** Generates a Collection containing an array of Elem, where each Elem
** holds a pointer to either a TypeA (tag=3, int value) or a TypeB
** (tag=5, string + byte).  Exercises: ARRAY_INLINE + VARIANT + typed
** integers + text + shape accessors.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ---- The user's types (what the tested function would take) ---- */

typedef struct {
  int               tag;       /* always 3 */
  int               value;
} TypeA;

typedef struct {
  int               tag;       /* always 5 */
  char *            name;
  uint8_t           byte;
} TypeB;

/* Wrapper for one element (framework-level discriminant + pointer): */
typedef struct {
  int               which;     /* 0 = TypeA, 1 = TypeB */
  void *            ptr;       /* TypeA * or TypeB * */
} Elem;

typedef struct {
  Elem *            items;     /* inline array of Elem */
  int               n_items;
} Collection;

/* ---- Schema ---- */

static hegel_schema_t coll_schema;

static void init_schema (void) {
  hegel_schema_t type_a = hegel_schema_struct (sizeof (TypeA),
      HEGEL_INT (TypeA, tag, 3, 3),          /* constant: always 3 */
      HEGEL_INT (TypeA, value, -1000, 1000));

  hegel_schema_t type_b = hegel_schema_struct (sizeof (TypeB),
      HEGEL_INT (TypeB, tag, 5, 5),          /* constant: always 5 */
      HEGEL_TEXT (TypeB, name, 1, 10),
      HEGEL_U8  (TypeB, byte));

  hegel_schema_t elem_schema = hegel_schema_struct (sizeof (Elem),
      HEGEL_VARIANT (Elem, which, ptr, type_a, type_b));

  coll_schema = hegel_schema_struct (sizeof (Collection),
      HEGEL_ARRAY_INLINE (Collection, items, n_items,
                          elem_schema, sizeof (Elem), 1, 5));
}

/* ---- Test ---- */

static void test_complex (hegel_testcase * tc) {
  Collection *  c;
  hegel_shape * sh = hegel_schema_draw (tc, coll_schema, (void **) &c);
  int           i;

  HEGEL_ASSERT (c->n_items >= 1 && c->n_items <= 5,
                "n_items=%d", c->n_items);

  for (i = 0; i < c->n_items; i ++) {
    HEGEL_ASSERT (c->items[i].which == 0 || c->items[i].which == 1,
                  "items[%d].which=%d", i, c->items[i].which);
    HEGEL_ASSERT (c->items[i].ptr != NULL,
                  "items[%d].ptr is NULL", i);

    if (c->items[i].which == 0) {
      TypeA * a = (TypeA *) c->items[i].ptr;
      HEGEL_ASSERT (a->tag == 3,
                    "TypeA.tag=%d expected 3", a->tag);
      HEGEL_ASSERT (a->value >= -1000 && a->value <= 1000,
                    "TypeA.value=%d", a->value);
    } else {
      TypeB * b = (TypeB *) c->items[i].ptr;
      HEGEL_ASSERT (b->tag == 5,
                    "TypeB.tag=%d expected 5", b->tag);
      HEGEL_ASSERT (b->name != NULL && strlen (b->name) >= 1
                    && strlen (b->name) <= 10,
                    "TypeB.name len=%d",
                    b->name ? (int) strlen (b->name) : -1);
      /* byte is u8, always in [0, 255] — no assert needed */
    }
  }

  hegel_shape_free (sh);
}

/* ---- Runner ---- */

int main (void) {
  init_schema ();
  printf ("Testing complex: array of variant struct pointers...\n");
  hegel_run_test (test_complex);
  printf ("  PASSED\n");
  hegel_schema_free (coll_schema);
  return (0);
}
