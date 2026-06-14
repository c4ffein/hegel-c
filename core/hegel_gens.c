/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/*
** Legacy combinator generators (hegel_gen_*).  A small tagged-union
** tree evaluated at draw time against the primitive draws, with spans
** marking composite structure so the shrinker can treat each subtree
** as a unit.  Predates the schema API (hegel_gen.h), kept for source
** compatibility; prefer the schema API in new code.
*/

#include "hegel_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  GEN_INT, GEN_I64, GEN_U64, GEN_FLOAT, GEN_DOUBLE, GEN_BOOL,
  GEN_TEXT, GEN_REGEX,
  GEN_ONE_OF, GEN_SAMPLED, GEN_OPTIONAL,
  GEN_MAP_INT, GEN_MAP_I64, GEN_MAP_DOUBLE,
  GEN_FILTER_INT, GEN_FILTER_I64, GEN_FILTER_DOUBLE,
  GEN_FLAT_MAP_INT, GEN_FLAT_MAP_I64, GEN_FLAT_MAP_DOUBLE
} gen_kind;

struct HegelGen {
  gen_kind kind;
  union {
    struct { int64_t  min, max; }   irange;
    struct { uint64_t min, max; }   urange;
    struct { double   min, max; }   frange;
    struct { int      min, max; }   srange;     /* text sizes      */
    struct { char *   pattern; }    regex;
    struct { hegel_gen ** items; int count; } one_of;
    struct { int count; }           sampled;
    struct { hegel_gen * inner; }   optional;
    struct {
      hegel_gen * source;
      void *      fn;       /* typed per kind */
      void *      ctx;
    } xform;
  } u;
};

#define FILTER_MAX_ATTEMPTS 3

static hegel_gen * gen_alloc (gen_kind kind)
{
  hegel_gen * g = calloc (1, sizeof *g);
  if (!g) abort ();
  g->kind = kind;
  return g;
}

/* ---- Factories ---- */

hegel_gen * hegel_gen_int (int min_val, int max_val)
{
  hegel_gen * g = gen_alloc (GEN_INT);
  g->u.irange.min = min_val;
  g->u.irange.max = max_val;
  return g;
}

hegel_gen * hegel_gen_i64 (int64_t min_val, int64_t max_val)
{
  hegel_gen * g = gen_alloc (GEN_I64);
  g->u.irange.min = min_val;
  g->u.irange.max = max_val;
  return g;
}

hegel_gen * hegel_gen_u64 (uint64_t min_val, uint64_t max_val)
{
  hegel_gen * g = gen_alloc (GEN_U64);
  g->u.urange.min = min_val;
  g->u.urange.max = max_val;
  return g;
}

hegel_gen * hegel_gen_float (float min_val, float max_val)
{
  hegel_gen * g = gen_alloc (GEN_FLOAT);
  g->u.frange.min = min_val;
  g->u.frange.max = max_val;
  return g;
}

hegel_gen * hegel_gen_double (double min_val, double max_val)
{
  hegel_gen * g = gen_alloc (GEN_DOUBLE);
  g->u.frange.min = min_val;
  g->u.frange.max = max_val;
  return g;
}

hegel_gen * hegel_gen_bool (void)
{
  return gen_alloc (GEN_BOOL);
}

hegel_gen * hegel_gen_text (int min_size, int max_size)
{
  hegel_gen * g = gen_alloc (GEN_TEXT);
  g->u.srange.min = min_size;
  g->u.srange.max = max_size;
  return g;
}

hegel_gen * hegel_gen_regex (const char * pattern)
{
  hegel_gen * g = gen_alloc (GEN_REGEX);
  g->u.regex.pattern = strdup (pattern);
  if (!g->u.regex.pattern) abort ();
  return g;
}

hegel_gen * hegel_gen_one_of (hegel_gen ** gens, int count)
{
  hegel_gen * g = gen_alloc (GEN_ONE_OF);
  g->u.one_of.items = malloc ((size_t)count * sizeof (hegel_gen *));
  if (!g->u.one_of.items) abort ();
  memcpy (g->u.one_of.items, gens, (size_t)count * sizeof (hegel_gen *));
  g->u.one_of.count = count;
  return g;
}

hegel_gen * hegel_gen_sampled_from (int count)
{
  hegel_gen * g = gen_alloc (GEN_SAMPLED);
  g->u.sampled.count = count;
  return g;
}

hegel_gen * hegel_gen_optional (hegel_gen * inner)
{
  hegel_gen * g = gen_alloc (GEN_OPTIONAL);
  g->u.optional.inner = inner;
  return g;
}

static hegel_gen * gen_xform (gen_kind kind, hegel_gen * source, void * fn, void * ctx)
{
  hegel_gen * g = gen_alloc (kind);
  g->u.xform.source = source;
  g->u.xform.fn = fn;
  g->u.xform.ctx = ctx;
  return g;
}

hegel_gen * hegel_gen_map_int (hegel_gen * source,
                               int (*map_fn)(int, void *), void * ctx)
{ return gen_xform (GEN_MAP_INT, source, (void *)map_fn, ctx); }

hegel_gen * hegel_gen_map_i64 (hegel_gen * source,
                               int64_t (*map_fn)(int64_t, void *), void * ctx)
{ return gen_xform (GEN_MAP_I64, source, (void *)map_fn, ctx); }

hegel_gen * hegel_gen_map_double (hegel_gen * source,
                                  double (*map_fn)(double, void *), void * ctx)
{ return gen_xform (GEN_MAP_DOUBLE, source, (void *)map_fn, ctx); }

hegel_gen * hegel_gen_filter_int (hegel_gen * source,
                                  int (*pred_fn)(int, void *), void * ctx)
{ return gen_xform (GEN_FILTER_INT, source, (void *)pred_fn, ctx); }

hegel_gen * hegel_gen_filter_i64 (hegel_gen * source,
                                  int (*pred_fn)(int64_t, void *), void * ctx)
{ return gen_xform (GEN_FILTER_I64, source, (void *)pred_fn, ctx); }

hegel_gen * hegel_gen_filter_double (hegel_gen * source,
                                     int (*pred_fn)(double, void *), void * ctx)
{ return gen_xform (GEN_FILTER_DOUBLE, source, (void *)pred_fn, ctx); }

hegel_gen * hegel_gen_flat_map_int (hegel_gen * source,
                                    hegel_gen * (*fn)(int, void *), void * ctx)
{ return gen_xform (GEN_FLAT_MAP_INT, source, (void *)fn, ctx); }

hegel_gen * hegel_gen_flat_map_i64 (hegel_gen * source,
                                    hegel_gen * (*fn)(int64_t, void *), void * ctx)
{ return gen_xform (GEN_FLAT_MAP_I64, source, (void *)fn, ctx); }

hegel_gen * hegel_gen_flat_map_double (hegel_gen * source,
                                       hegel_gen * (*fn)(double, void *), void * ctx)
{ return gen_xform (GEN_FLAT_MAP_DOUBLE, source, (void *)fn, ctx); }

/* ---- Free ---- */

void hegel_gen_free (hegel_gen * gen)
{
  if (!gen) return;
  switch (gen->kind) {
    case GEN_REGEX:
      free (gen->u.regex.pattern);
      break;
    case GEN_ONE_OF:
      for (int i = 0; i < gen->u.one_of.count; i++)
        hegel_gen_free (gen->u.one_of.items[i]);
      free (gen->u.one_of.items);
      break;
    case GEN_OPTIONAL:
      hegel_gen_free (gen->u.optional.inner);
      break;
    case GEN_MAP_INT: case GEN_MAP_I64: case GEN_MAP_DOUBLE:
    case GEN_FILTER_INT: case GEN_FILTER_I64: case GEN_FILTER_DOUBLE:
    case GEN_FLAT_MAP_INT: case GEN_FLAT_MAP_I64: case GEN_FLAT_MAP_DOUBLE:
      hegel_gen_free (gen->u.xform.source);
      break;
    default:
      break;
  }
  free (gen);
}

/* ---- Evaluation ----
**
** Numeric values flow through a small variant so one evaluator serves
** all the typed public draw functions. */

typedef struct {
  enum { V_I64, V_U64, V_DOUBLE } tag;
  union { int64_t i; uint64_t u; double d; } v;
} num_value;

static num_value eval_num (hegel_testcase * tc, hegel_gen * g);

static int64_t num_as_i64 (num_value n)
{
  switch (n.tag) {
    case V_I64:    return n.v.i;
    case V_U64:    return (int64_t)n.v.u;
    default:       return (int64_t)n.v.d;
  }
}

static uint64_t num_as_u64 (num_value n)
{
  switch (n.tag) {
    case V_I64:    return (uint64_t)n.v.i;
    case V_U64:    return n.v.u;
    default:       return (uint64_t)n.v.d;
  }
}

static double num_as_double (num_value n)
{
  switch (n.tag) {
    case V_I64:    return (double)n.v.i;
    case V_U64:    return (double)n.v.u;
    default:       return n.v.d;
  }
}

static num_value eval_num (hegel_testcase * tc, hegel_gen * g)
{
  num_value n;
  switch (g->kind) {
    case GEN_INT: case GEN_I64:
      n.tag = V_I64;
      n.v.i = hegel_draw_i64 (tc, g->u.irange.min, g->u.irange.max);
      return n;
    case GEN_U64:
      n.tag = V_U64;
      n.v.u = hegel_draw_u64 (tc, g->u.urange.min, g->u.urange.max);
      return n;
    case GEN_FLOAT:
      n.tag = V_DOUBLE;
      n.v.d = (double)hegel_draw_float (tc, (float)g->u.frange.min,
                                        (float)g->u.frange.max);
      return n;
    case GEN_DOUBLE:
      n.tag = V_DOUBLE;
      n.v.d = hegel_draw_double (tc, g->u.frange.min, g->u.frange.max);
      return n;
    case GEN_BOOL:
      n.tag = V_I64;
      n.v.i = hegel__draw_bool (tc);
      return n;
    case GEN_SAMPLED:
      n.tag = V_I64;
      hegel_start_span (tc, HEGEL_SPAN_SAMPLED_FROM);
      n.v.i = hegel_draw_i64 (tc, 0, g->u.sampled.count - 1);
      hegel_stop_span (tc, 0);
      return n;
    case GEN_ONE_OF: {
      hegel_start_span (tc, HEGEL_SPAN_ONE_OF);
      int idx = (int)hegel_draw_i64 (tc, 0, g->u.one_of.count - 1);
      n = eval_num (tc, g->u.one_of.items[idx]);
      hegel_stop_span (tc, 0);
      return n;
    }
    case GEN_OPTIONAL:
      /* Plain numeric draw on an Optional ignores absence — use the
      ** hegel_gen_draw_optional_* family to observe it. */
      return eval_num (tc, g->u.optional.inner);

    case GEN_MAP_INT: {
      hegel_start_span (tc, HEGEL_SPAN_MAPPED);
      num_value s = eval_num (tc, g->u.xform.source);
      hegel_stop_span (tc, 0);
      int (*fn)(int, void *) = (int (*)(int, void *))g->u.xform.fn;
      n.tag = V_I64;
      n.v.i = fn ((int)num_as_i64 (s), g->u.xform.ctx);
      return n;
    }
    case GEN_MAP_I64: {
      hegel_start_span (tc, HEGEL_SPAN_MAPPED);
      num_value s = eval_num (tc, g->u.xform.source);
      hegel_stop_span (tc, 0);
      int64_t (*fn)(int64_t, void *) = (int64_t (*)(int64_t, void *))g->u.xform.fn;
      n.tag = V_I64;
      n.v.i = fn (num_as_i64 (s), g->u.xform.ctx);
      return n;
    }
    case GEN_MAP_DOUBLE: {
      hegel_start_span (tc, HEGEL_SPAN_MAPPED);
      num_value s = eval_num (tc, g->u.xform.source);
      hegel_stop_span (tc, 0);
      double (*fn)(double, void *) = (double (*)(double, void *))g->u.xform.fn;
      n.tag = V_DOUBLE;
      n.v.d = fn (num_as_double (s), g->u.xform.ctx);
      return n;
    }

    case GEN_FILTER_INT: case GEN_FILTER_I64: case GEN_FILTER_DOUBLE: {
      for (int attempt = 0; attempt < FILTER_MAX_ATTEMPTS; attempt++) {
        hegel_start_span (tc, HEGEL_SPAN_FILTER);
        num_value s = eval_num (tc, g->u.xform.source);
        int keep;
        if (g->kind == GEN_FILTER_INT) {
          int (*fn)(int, void *) = (int (*)(int, void *))g->u.xform.fn;
          keep = fn ((int)num_as_i64 (s), g->u.xform.ctx);
        } else if (g->kind == GEN_FILTER_I64) {
          int (*fn)(int64_t, void *) = (int (*)(int64_t, void *))g->u.xform.fn;
          keep = fn (num_as_i64 (s), g->u.xform.ctx);
        } else {
          int (*fn)(double, void *) = (int (*)(double, void *))g->u.xform.fn;
          keep = fn (num_as_double (s), g->u.xform.ctx);
        }
        hegel_stop_span (tc, !keep);
        if (keep) return s;
      }
      /* All attempts rejected — discard the test case. */
      hegel_assume (tc, 0);
      /* unreachable */
      n.tag = V_I64; n.v.i = 0;
      return n;
    }

    case GEN_FLAT_MAP_INT: case GEN_FLAT_MAP_I64: case GEN_FLAT_MAP_DOUBLE: {
      hegel_start_span (tc, HEGEL_SPAN_FLAT_MAP);
      num_value s = eval_num (tc, g->u.xform.source);
      hegel_gen * g2;
      if (g->kind == GEN_FLAT_MAP_INT) {
        hegel_gen * (*fn)(int, void *) = (hegel_gen * (*)(int, void *))g->u.xform.fn;
        g2 = fn ((int)num_as_i64 (s), g->u.xform.ctx);
      } else if (g->kind == GEN_FLAT_MAP_I64) {
        hegel_gen * (*fn)(int64_t, void *) = (hegel_gen * (*)(int64_t, void *))g->u.xform.fn;
        g2 = fn (num_as_i64 (s), g->u.xform.ctx);
      } else {
        hegel_gen * (*fn)(double, void *) = (hegel_gen * (*)(double, void *))g->u.xform.fn;
        g2 = fn (num_as_double (s), g->u.xform.ctx);
      }
      num_value r = eval_num (tc, g2);
      hegel_gen_free (g2);
      hegel_stop_span (tc, 0);
      return r;
    }

    case GEN_TEXT: case GEN_REGEX:
      fprintf (stderr, "hegel-c: numeric draw on a text/regex generator\n");
      abort ();
  }
  fprintf (stderr, "hegel-c: unknown generator kind %d\n", g->kind);
  abort ();
}

/* ---- Public scalar draws ---- */

int hegel_gen_draw_int (hegel_testcase * tc, hegel_gen * gen)
{
  return (int)num_as_i64 (eval_num (tc, gen));
}

int64_t hegel_gen_draw_i64 (hegel_testcase * tc, hegel_gen * gen)
{
  return num_as_i64 (eval_num (tc, gen));
}

uint64_t hegel_gen_draw_u64 (hegel_testcase * tc, hegel_gen * gen)
{
  return num_as_u64 (eval_num (tc, gen));
}

float hegel_gen_draw_float (hegel_testcase * tc, hegel_gen * gen)
{
  return (float)num_as_double (eval_num (tc, gen));
}

double hegel_gen_draw_double (hegel_testcase * tc, hegel_gen * gen)
{
  return num_as_double (eval_num (tc, gen));
}

int hegel_gen_draw_bool (hegel_testcase * tc, hegel_gen * gen)
{
  return (int)num_as_i64 (eval_num (tc, gen));
}

int hegel_gen_draw_text (hegel_testcase * tc, hegel_gen * gen,
                         char * buf, int capacity)
{
  switch (gen->kind) {
    case GEN_TEXT:
      return hegel_draw_text (tc, gen->u.srange.min, gen->u.srange.max,
                              buf, capacity);
    case GEN_REGEX:
      return hegel_draw_regex (tc, gen->u.regex.pattern, buf, capacity);
    case GEN_ONE_OF: {
      hegel_start_span (tc, HEGEL_SPAN_ONE_OF);
      int idx = (int)hegel_draw_i64 (tc, 0, gen->u.one_of.count - 1);
      int len = hegel_gen_draw_text (tc, gen->u.one_of.items[idx], buf, capacity);
      hegel_stop_span (tc, 0);
      return len;
    }
    default:
      fprintf (stderr, "hegel-c: text draw on a non-text generator\n");
      abort ();
  }
}

/* ---- Optional draws ---- */

static int draw_optional_present (hegel_testcase * tc, hegel_gen * gen,
                                  hegel_gen ** out_inner)
{
  if (gen->kind != GEN_OPTIONAL) {
    *out_inner = gen;
    return 1;
  }
  hegel_start_span (tc, HEGEL_SPAN_OPTIONAL);
  int present = hegel__draw_bool (tc);
  *out_inner = gen->u.optional.inner;
  if (!present) hegel_stop_span (tc, 0);
  /* On the present path the span is closed by the caller after the
  ** inner draw (see the wrappers below). */
  return present;
}

#define DEFINE_DRAW_OPTIONAL(SUFFIX, CTYPE, DRAW_FN)                          \
  int hegel_gen_draw_optional_##SUFFIX (hegel_testcase * tc, hegel_gen * gen, \
                                        CTYPE * out)                          \
  {                                                                           \
    hegel_gen * inner;                                                        \
    int present = draw_optional_present (tc, gen, &inner);                    \
    if (!present) return 0;                                                   \
    *out = DRAW_FN (tc, inner);                                               \
    if (gen->kind == GEN_OPTIONAL) hegel_stop_span (tc, 0);                   \
    return 1;                                                                 \
  }

DEFINE_DRAW_OPTIONAL (int,    int,      hegel_gen_draw_int)
DEFINE_DRAW_OPTIONAL (i64,    int64_t,  hegel_gen_draw_i64)
DEFINE_DRAW_OPTIONAL (u64,    uint64_t, hegel_gen_draw_u64)
DEFINE_DRAW_OPTIONAL (float,  float,    hegel_gen_draw_float)
DEFINE_DRAW_OPTIONAL (double, double,   hegel_gen_draw_double)

/* ---- List draws ---- */

#define DEFINE_DRAW_LIST(SUFFIX, CTYPE, DRAW_FN)                              \
  int hegel_gen_draw_list_##SUFFIX (hegel_testcase * tc, hegel_gen * elem_gen,\
                                    int min_len, int max_len,                 \
                                    CTYPE * buf, int capacity)                \
  {                                                                           \
    if (max_len > capacity) max_len = capacity;                               \
    if (min_len > max_len)  min_len = max_len;                                \
    hegel_start_span (tc, HEGEL_SPAN_LIST);                                   \
    int n = hegel_draw_int (tc, min_len, max_len);                            \
    for (int i = 0; i < n; i++) {                                             \
      hegel_start_span (tc, HEGEL_SPAN_LIST_ELEMENT);                         \
      buf[i] = DRAW_FN (tc, elem_gen);                                        \
      hegel_stop_span (tc, 0);                                                \
    }                                                                         \
    hegel_stop_span (tc, 0);                                                  \
    return n;                                                                 \
  }

DEFINE_DRAW_LIST (int,    int,      hegel_gen_draw_int)
DEFINE_DRAW_LIST (i64,    int64_t,  hegel_gen_draw_i64)
DEFINE_DRAW_LIST (u64,    uint64_t, hegel_gen_draw_u64)
DEFINE_DRAW_LIST (float,  float,    hegel_gen_draw_float)
DEFINE_DRAW_LIST (double, double,   hegel_gen_draw_double)
