/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: raw pointer array with heterogeneous struct targets.
**
** Unlike test_schema_array_of_variant_struct_pointers.c (which
** uses an `Elem { which; void*ptr; }` wrapper per slot), this test
** uses a bare `void **items` — no wrapper, no per-slot tag field.
** Each pointer lands directly in the array, and the user discriminates
** by reading a tag field inside the pointed-to struct itself.
**
** The new schema kind `HEGEL_ONE_OF_STRUCT` produces a pointer-valued
** generator: when drawn, it picks one of several STRUCT schemas,
** allocates the chosen one, and returns the pointer.  It writes
** nothing to any parent — it's usable anywhere a pointer-producing
** generator makes sense (ARRAY element, OPTIONAL inner).
**
** The variant structs each carry their own tag field, drawn as a
** constant (min==max), so `*(int*)items[i]` tells you which type it
** really is.  This is the "tag inside the pointed-to struct" idiom,
** common in real C (think Lua TValue, Erlang terms, any Scheme
** interpreter's cell tagging).
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ---- Types ---- */

typedef struct {
  int               tag;     /* always 3 */
  int               value;
} TypeA;

typedef struct {
  int               tag;     /* always 5 */
  char *            name;
  uint8_t           byte;
} TypeB;

typedef struct {
  int               tag;     /* always 7 */
  double            x;
  double            y;
  double            z;
} TypeC;

typedef struct {
  void **           items;     /* each entry: TypeA*, TypeB*, or TypeC* */
  int               n_items;
} RawCollection;

/* ---- Schema ---- */

HEGEL_BINDING (n_items);

static hegel_schema_t coll_schema;

static
void
init_schema (void)
{
  hegel_schema_t type_a = HEGEL_STRUCT (TypeA,
      HEGEL_INT (3, 3),                      /* constant: always 3 */
      HEGEL_INT (-1000, 1000));

  hegel_schema_t type_b = HEGEL_STRUCT (TypeB,
      HEGEL_INT (5, 5),                      /* constant: always 5 */
      HEGEL_TEXT (1, 10),
      HEGEL_U8 ());

  hegel_schema_t type_c = HEGEL_STRUCT (TypeC,
      HEGEL_INT (7, 7),                      /* constant: always 7 */
      HEGEL_DOUBLE (-10.0, 10.0),
      HEGEL_DOUBLE (-10.0, 10.0),
      HEGEL_DOUBLE (-10.0, 10.0));

  /* HEGEL_ONE_OF_STRUCT picks one of the three struct schemas and
  ** returns a pointer to a freshly allocated instance.  Used as the
  ** element of HEGEL_ARR_OF, each slot holds a raw `void *` pointing
  ** to whichever variant was chosen — TypeA, TypeB, or TypeC. */
  hegel_schema_t one_of = HEGEL_ONE_OF_STRUCT (type_a, type_b, type_c);

  coll_schema = HEGEL_STRUCT (RawCollection,
      HEGEL_LET    (n_items, HEGEL_INT (1, 6)),
      HEGEL_ARR_OF (HEGEL_USE (n_items), one_of),
      HEGEL_USE    (n_items));
}

/* ---- Test ---- */

static
void
test_raw_collection (
hegel_testcase *            tc)
{
  RawCollection *   c;
  hegel_shape *     sh;
  int               i;
  int               n_a = 0, n_b = 0, n_c = 0;

  sh = hegel_schema_draw (tc, coll_schema, (void **) &c);

  HEGEL_ASSERT (c->n_items >= 1 && c->n_items <= 6,
                "n_items=%d", c->n_items);

  for (i = 0; i < c->n_items; i ++) {
    HEGEL_ASSERT (c->items[i] != NULL, "items[%d] NULL", i);

    /* Read the tag from the first int of the pointed-to struct.
    ** All three types have `int tag` as their first field, so reading
    ** `*(int*)items[i]` is legal regardless of which variant it is. */
    int tag = *(int *) c->items[i];

    if (tag == 3) {
      TypeA * a = (TypeA *) c->items[i];
      HEGEL_ASSERT (a->value >= -1000 && a->value <= 1000,
                    "items[%d] TypeA.value=%d", i, a->value);
      n_a ++;
    } else if (tag == 5) {
      TypeB * b = (TypeB *) c->items[i];
      HEGEL_ASSERT (b->name != NULL && strlen (b->name) >= 1
                    && strlen (b->name) <= 10,
                    "items[%d] TypeB.name", i);
      n_b ++;
    } else if (tag == 7) {
      TypeC * cc = (TypeC *) c->items[i];
      HEGEL_ASSERT (cc->x >= -10.0 && cc->x <= 10.0,
                    "items[%d] TypeC.x=%f", i, cc->x);
      HEGEL_ASSERT (cc->y >= -10.0 && cc->y <= 10.0,
                    "items[%d] TypeC.y=%f", i, cc->y);
      HEGEL_ASSERT (cc->z >= -10.0 && cc->z <= 10.0,
                    "items[%d] TypeC.z=%f", i, cc->z);
      n_c ++;
    } else {
      hegel_fail ("items[i] has unknown tag");
    }
  }

  HEGEL_ASSERT (n_a + n_b + n_c == c->n_items,
                "a=%d b=%d c=%d total=%d",
                n_a, n_b, n_c, c->n_items);

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
  printf ("Testing raw pointer array with 3-way one-of-struct...\n");
  hegel_run_test (test_raw_collection);
  printf ("  PASSED\n");

  hegel_schema_free (coll_schema);
  return (0);
}
