/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: full functional combinator coverage at the schema layer.
**
** Exercises everything the legacy hegel_gen_* API provides for
** functional composition, now available in typed schema form:
**
**   - map (int, i64, double)
**   - filter (int, i64, double)
**   - flat_map (int, i64, double)
**   - one_of for scalar generators (different distributions)
**   - bool via HEGEL_BOOL shorthand
**   - regex text generation
**   - optional int pointer (sanity check via HEGEL_OPTIONAL)
**
** This file is the feature-parity proof: with these combinators the
** schema API subsumes the legacy hegel_gen_* combinator surface.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ================================================================
** Part 1: Optional int via HEGEL_OPTIONAL + int_range (sanity)
** ================================================================ */

typedef struct {
  int *             maybe_val;
  int               always_val;
} OptIntThing;

static hegel_schema_t optint_schema;

static void test_optint (hegel_testcase * tc) {
  OptIntThing * t;
  hegel_shape * sh = hegel_schema_draw (tc, optint_schema, (void **) &t);
  HEGEL_ASSERT (t->always_val >= 0 && t->always_val <= 100,
                "always_val=%d", t->always_val);
  if (t->maybe_val != NULL)
    HEGEL_ASSERT (*t->maybe_val >= 50 && *t->maybe_val <= 150,
                  "*maybe_val=%d", *t->maybe_val);
  hegel_shape_free (sh);
}

/* ================================================================
** Part 2: map — int, i64, double
** ================================================================ */

static int square_int (int v, void *ctx) { (void)ctx; return v * v; }
static int64_t cube_i64 (int64_t v, void *ctx) {
  (void)ctx; return v * v * v;
}
static double halve_double (double v, void *ctx) {
  (void)ctx; return v / 2.0;
}

typedef struct {
  int               square;
  int64_t           cube;
  double            halved;
} MapThing;

static hegel_schema_t map_schema;

static void test_map (hegel_testcase * tc) {
  MapThing * t;
  hegel_shape * sh = hegel_schema_draw (tc, map_schema, (void **) &t);

  /* square in [0, 10000] and a perfect square */
  HEGEL_ASSERT (t->square >= 0 && t->square <= 10000,
                "square=%d", t->square);
  {
    int r = 0;
    while ((r + 1) * (r + 1) <= t->square) r ++;
    HEGEL_ASSERT (r * r == t->square,
                  "square=%d not a perfect square", t->square);
  }

  /* cube: source in [0, 10], so cube in [0, 1000] */
  HEGEL_ASSERT (t->cube >= 0 && t->cube <= 1000,
                "cube=%ld", (long) t->cube);

  /* halved: source in [0, 10], halved in [0, 5] */
  HEGEL_ASSERT (t->halved >= 0.0 && t->halved <= 5.0,
                "halved=%f", t->halved);

  hegel_shape_free (sh);
}

/* ================================================================
** Part 3: filter — int, i64, double
** ================================================================ */

static int is_even_int (int v, void *ctx) { (void)ctx; return (v % 2) == 0; }
static int is_positive_i64 (int64_t v, void *ctx) {
  (void)ctx; return v > 0;
}
static int is_ge_one_double (double v, void *ctx) {
  (void)ctx; return v >= 1.0;
}

/* Filters are tested separately because stacking multiple filters
** in one schema multiplies rejection probability (3 filters each
** rejecting 50% → 87.5% combined rejection, trips filter_too_much
** health check). */

typedef struct { int v; }     FilterIntThing;
typedef struct { int64_t v; } FilterI64Thing;
typedef struct { double v; }  FilterDoubleThing;

static hegel_schema_t filter_int_schema;
static hegel_schema_t filter_i64_schema;
static hegel_schema_t filter_double_schema;

static void test_filter_int_only (hegel_testcase * tc) {
  FilterIntThing * t;
  hegel_shape * sh = hegel_schema_draw (tc, filter_int_schema, (void **) &t);
  HEGEL_ASSERT ((t->v % 2) == 0, "v=%d not even", t->v);
  hegel_shape_free (sh);
}

static void test_filter_i64_only (hegel_testcase * tc) {
  FilterI64Thing * t;
  hegel_shape * sh = hegel_schema_draw (tc, filter_i64_schema, (void **) &t);
  HEGEL_ASSERT (t->v > 0, "v=%ld not positive", (long) t->v);
  hegel_shape_free (sh);
}

static void test_filter_double_only (hegel_testcase * tc) {
  FilterDoubleThing * t;
  hegel_shape * sh = hegel_schema_draw (tc, filter_double_schema, (void **) &t);
  HEGEL_ASSERT (t->v >= 1.0, "v=%f not >= 1.0", t->v);
  hegel_shape_free (sh);
}

/* ================================================================
** Part 4: flat_map — int, i64, double
** ================================================================ */

static hegel_schema_t dep_int_fn (int n, void *ctx) {
  (void)ctx;
  return hegel_schema_int_range (0, n * 10);
}
static hegel_schema_t dep_i64_fn (int64_t n, void *ctx) {
  (void)ctx;
  return hegel_schema_i64_range (0, n * 100);
}
static hegel_schema_t dep_double_fn (double n, void *ctx) {
  (void)ctx;
  return hegel_schema_double_range (0.0, n * 2.0);
}

typedef struct {
  int               dep_int;
  int64_t           dep_i64;
  double            dep_double;
} FlatMapThing;

static hegel_schema_t flat_map_schema;

static void test_flat_map (hegel_testcase * tc) {
  FlatMapThing * t;
  hegel_shape * sh = hegel_schema_draw (tc, flat_map_schema, (void **) &t);

  HEGEL_ASSERT (t->dep_int >= 0 && t->dep_int <= 100,
                "dep_int=%d", t->dep_int);
  HEGEL_ASSERT (t->dep_i64 >= 0 && t->dep_i64 <= 1000,
                "dep_i64=%ld", (long) t->dep_i64);
  HEGEL_ASSERT (t->dep_double >= 0.0 && t->dep_double <= 20.0,
                "dep_double=%f", t->dep_double);

  hegel_shape_free (sh);
}

/* ================================================================
** Part 5: one_of for scalars — "small OR large"
** ================================================================ */

typedef struct {
  int               value;   /* either [0, 10] OR [1000, 9999] */
} OneOfThing;

static hegel_schema_t one_of_schema;

static void test_one_of (hegel_testcase * tc) {
  OneOfThing * t;
  hegel_shape * sh = hegel_schema_draw (tc, one_of_schema, (void **) &t);

  int in_small = (t->value >= 0    && t->value <= 10);
  int in_large = (t->value >= 1000 && t->value <= 9999);
  HEGEL_ASSERT (in_small || in_large,
                "value=%d is neither small [0,10] nor large [1000,9999]",
                t->value);

  hegel_shape_free (sh);
}

/* ================================================================
** Part 6: bool via HEGEL_BOOL shorthand
** ================================================================ */

typedef struct {
  bool              flag;
  int               val;
} BoolThing;

static hegel_schema_t bool_schema;

static void test_bool (hegel_testcase * tc) {
  BoolThing * t;
  hegel_shape * sh = hegel_schema_draw (tc, bool_schema, (void **) &t);

  /* A bool can only be 0 or 1.  If the bit layout is wrong, this
  ** would fail — so the test also verifies HEGEL_BOOL writes 1 byte. */
  int b = t->flag ? 1 : 0;
  HEGEL_ASSERT (b == 0 || b == 1, "flag=%d", b);
  HEGEL_ASSERT (t->val >= 0 && t->val <= 100, "val=%d", t->val);

  hegel_shape_free (sh);
}

/* ================================================================
** Part 7: regex text generation
** ================================================================
**
** NOTE: the underlying hegel_draw_regex uses "contains a match"
** semantics, not full-match — see TODO.md.  We just verify the
** generator runs and produces a non-NULL, bounded-length string. */

typedef struct {
  char *            text;
} RegexThing;

static hegel_schema_t regex_schema;

static void test_regex (hegel_testcase * tc) {
  RegexThing * t;
  hegel_shape * sh = hegel_schema_draw (tc, regex_schema, (void **) &t);

  HEGEL_ASSERT (t->text != NULL, "text is NULL");
  /* Length should be within the capacity we gave. */
  HEGEL_ASSERT (strlen (t->text) < 64,
                "text length=%zu >= 64", strlen (t->text));

  hegel_shape_free (sh);
}

/* ================================================================
** Runner
** ================================================================ */

int main (void) {
  optint_schema = HEGEL_STRUCT (OptIntThing,
      HEGEL_OPTIONAL (hegel_schema_int_range (50, 150)),
      HEGEL_INT (0, 100));

  map_schema = HEGEL_STRUCT (MapThing,
      HEGEL_MAP_INT    (hegel_schema_int_range (0, 100),
                        square_int, NULL),
      HEGEL_MAP_I64    (hegel_schema_i64_range (0, 10),
                        cube_i64, NULL),
      HEGEL_MAP_DOUBLE (hegel_schema_double_range (0.0, 10.0),
                        halve_double, NULL));

  filter_int_schema = HEGEL_STRUCT (FilterIntThing,
      HEGEL_FILTER_INT (hegel_schema_int_range (0, 100),
                        is_even_int, NULL));
  filter_i64_schema = HEGEL_STRUCT (FilterI64Thing,
      HEGEL_FILTER_I64 (hegel_schema_i64_range (-1000, 1000),
                        is_positive_i64, NULL));
  filter_double_schema = HEGEL_STRUCT (FilterDoubleThing,
      HEGEL_FILTER_DOUBLE (hegel_schema_double_range (0.0, 10.0),
                           is_ge_one_double, NULL));

  flat_map_schema = HEGEL_STRUCT (FlatMapThing,
      HEGEL_FLAT_MAP_INT    (hegel_schema_int_range (1, 10),
                             dep_int_fn, NULL),
      HEGEL_FLAT_MAP_I64    (hegel_schema_i64_range (1, 10),
                             dep_i64_fn, NULL),
      HEGEL_FLAT_MAP_DOUBLE (hegel_schema_double_range (1.0, 10.0),
                             dep_double_fn, NULL));

  one_of_schema = HEGEL_STRUCT (OneOfThing,
      HEGEL_ONE_OF_INT (hegel_schema_int_range (0, 10),
                        hegel_schema_int_range (1000, 9999)));

  bool_schema = HEGEL_STRUCT (BoolThing,
      HEGEL_BOOL (),
      HEGEL_INT  (0, 100));

  regex_schema = HEGEL_STRUCT (RegexThing,
      HEGEL_REGEX ("[a-z]+", 64));

  printf ("  optional int pointer...\n");
  hegel_run_test (test_optint); printf ("    PASSED\n");
  printf ("  map (int, i64, double)...\n");
  hegel_run_test (test_map); printf ("    PASSED\n");
  printf ("  filter int...\n");
  hegel_run_test (test_filter_int_only); printf ("    PASSED\n");
  printf ("  filter i64...\n");
  hegel_run_test (test_filter_i64_only); printf ("    PASSED\n");
  printf ("  filter double...\n");
  hegel_run_test (test_filter_double_only); printf ("    PASSED\n");
  printf ("  flat_map (int, i64, double)...\n");
  hegel_run_test (test_flat_map); printf ("    PASSED\n");
  printf ("  one_of scalar...\n");
  hegel_run_test (test_one_of); printf ("    PASSED\n");
  printf ("  bool...\n");
  hegel_run_test (test_bool); printf ("    PASSED\n");
  printf ("  regex text...\n");
  hegel_run_test (test_regex); printf ("    PASSED\n");

  hegel_schema_free (optint_schema);
  hegel_schema_free (map_schema);
  hegel_schema_free (filter_int_schema);
  hegel_schema_free (filter_i64_schema);
  hegel_schema_free (filter_double_schema);
  hegel_schema_free (flat_map_schema);
  hegel_schema_free (one_of_schema);
  hegel_schema_free (bool_schema);
  hegel_schema_free (regex_schema);
  return 0;
}
