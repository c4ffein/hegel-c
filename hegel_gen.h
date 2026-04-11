/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
#ifndef HEGEL_GEN_H
#define HEGEL_GEN_H

// TODO: this is a PoC, we should do better obviously

/*
** Structured generation helpers for hegel-c.
**
** Three-layer architecture:
**   1. Schema (generator tree) — describes what to generate.
**      Built once by the user via hegel_schema_*() helpers.
**   2. Shape tree — records what was generated on each draw.
**      Built automatically by hegel_schema_draw(). Owns the
**      value memory. Freed by hegel_shape_free().
**   3. Value memory — the actual C struct the user defined.
**      Allocated and filled in by hegel_schema_draw(). The user
**      gets a typed pointer back. Freed when shape is freed.
**
** Spans are emitted automatically — the user never calls
** hegel_start_span / hegel_stop_span directly.
**
** Integer types are unified: i8, u8, i16, u16, i32, u32, i64, u64,
** int, long — each with a no-arg full-range version and a _range
** version for constraints.  Macros support 2-arg (full range) or
** 4-arg (constrained) forms:
**
**   HEGEL_U8(Packet, flags)             // [0, 255]
**   HEGEL_U8(Packet, flags, 0, 15)      // [0, 15]
**   HEGEL_INT(Tree, val)                // [INT_MIN, INT_MAX]
**   HEGEL_INT(Tree, val, -1000, 1000)   // constrained
**
** Example (binary tree with optional label + children):
**
**   typedef struct Tree {
**     int val;
**     char *label;
**     struct Tree *left, *right;
**   } Tree;
**
**   hegel_schema *s = hegel_schema_struct(sizeof(Tree),
**       HEGEL_INT(Tree, val, -1000, 1000),
**       HEGEL_OPTIONAL(Tree, label, hegel_schema_text(0, 8)),
**       HEGEL_SELF(Tree, left),
**       HEGEL_SELF(Tree, right),
**       NULL);
**
** Example (struct with int array):
**
**   typedef struct Bag {
**     int *items;  int n_items;  int tag;
**   } Bag;
**
**   hegel_schema *s = hegel_schema_struct(sizeof(Bag),
**       HEGEL_ARRAY(Bag, items, n_items,
**                   hegel_schema_int_range(0, 100), 0, 10),
**       HEGEL_INT(Bag, tag, 0, 3),
**       NULL);
*/

#include "hegel_c.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <limits.h>
#include <float.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
** Schema (generator tree)
** ================================================================ */

typedef enum {
  HEGEL_SCH_INTEGER,        /* any integer type: i8..u64, int, long */
  HEGEL_SCH_FLOAT,          /* float or double                     */
  HEGEL_SCH_TEXT,            /* malloc'd string ptr at offset       */
  HEGEL_SCH_STRUCT,          /* allocate struct, draw fields        */
  HEGEL_SCH_OPTIONAL_PTR,    /* draw bool; if true draw inner       */
  HEGEL_SCH_ARRAY,           /* draw len; malloc array; draw elems  */
  HEGEL_SCH_SELF             /* recursive ref (resolved by ctor)    */
} hegel_schema_kind;

typedef struct hegel_schema {
  hegel_schema_kind       kind;
  size_t                  offset;
  union {
    struct {
      int               width;       /* 1, 2, 4, 8 bytes            */
      int               is_signed;
      int64_t           min_s;       /* signed bounds                */
      int64_t           max_s;
      uint64_t          min_u;       /* unsigned bounds              */
      uint64_t          max_u;
    }                                                  integer;
    struct {
      int               width;       /* 4 (float) or 8 (double)     */
      double            min;
      double            max;
    }                                                  fp;
    struct { int min_len; int max_len; }               text_range;
    struct {
      size_t            size;
      int               n_fields;
      struct hegel_schema ** fields;
    }                                                  struct_def;
    struct {
      struct hegel_schema * inner;
    }                                                  optional_ptr;
    struct {
      size_t            len_offset;
      struct hegel_schema * elem;
      int               min_len;
      int               max_len;
    }                                                  array_def;
    struct {
      struct hegel_schema * target;
    }                                                  self_ref;
  };
} hegel_schema;

/* ================================================================
** Shape (drawn-value tree)
** ================================================================ */

typedef enum {
  HEGEL_SHAPE_SCALAR,
  HEGEL_SHAPE_TEXT,
  HEGEL_SHAPE_STRUCT,
  HEGEL_SHAPE_OPTIONAL,
  HEGEL_SHAPE_ARRAY
} hegel_shape_kind;

typedef struct hegel_shape {
  hegel_shape_kind        kind;
  void *                  owned;
  union {
    struct {
      int               n_fields;
      struct hegel_shape ** fields;
    }                                                  struct_shape;
    struct {
      int               is_some;
      struct hegel_shape * inner;
    }                                                  optional_shape;
    struct {
      int               len;
      struct hegel_shape ** elems;
    }                                                  array_shape;
  };
} hegel_shape;

/* ================================================================
** Schema constructors — integers
** ================================================================
**
** Internal helper: build an integer schema node with explicit params.
** All public constructors go through this. */

static hegel_schema *
hegel__integer (size_t offset, int width, int is_signed,
                int64_t min_s, int64_t max_s,
                uint64_t min_u, uint64_t max_u)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_INTEGER;
  s->offset = offset;
  s->integer.width = width;
  s->integer.is_signed = is_signed;
  s->integer.min_s = min_s;
  s->integer.max_s = max_s;
  s->integer.min_u = min_u;
  s->integer.max_u = max_u;
  return (s);
}

/* ---- signed constructors ---- */

/* offset-free (for OPTIONAL / ARRAY inner) */
static hegel_schema * hegel_schema_i8 (void) {
  return hegel__integer (0, 1, 1, INT8_MIN, INT8_MAX, 0, 0);
}
static hegel_schema * hegel_schema_i8_range (int64_t lo, int64_t hi) {
  return hegel__integer (0, 1, 1, lo, hi, 0, 0);
}
static hegel_schema * hegel_schema_i16 (void) {
  return hegel__integer (0, 2, 1, INT16_MIN, INT16_MAX, 0, 0);
}
static hegel_schema * hegel_schema_i16_range (int64_t lo, int64_t hi) {
  return hegel__integer (0, 2, 1, lo, hi, 0, 0);
}
static hegel_schema * hegel_schema_i32 (void) {
  return hegel__integer (0, 4, 1, INT32_MIN, INT32_MAX, 0, 0);
}
static hegel_schema * hegel_schema_i32_range (int64_t lo, int64_t hi) {
  return hegel__integer (0, 4, 1, lo, hi, 0, 0);
}
static hegel_schema * hegel_schema_i64 (void) {
  return hegel__integer (0, 8, 1, INT64_MIN, INT64_MAX, 0, 0);
}
static hegel_schema * hegel_schema_i64_range (int64_t lo, int64_t hi) {
  return hegel__integer (0, 8, 1, lo, hi, 0, 0);
}

/* platform-width signed */
static hegel_schema * hegel_schema_int (void) {
  return hegel__integer (0, (int) sizeof (int), 1, INT_MIN, INT_MAX, 0, 0);
}
static hegel_schema * hegel_schema_int_range (int64_t lo, int64_t hi) {
  return hegel__integer (0, (int) sizeof (int), 1, lo, hi, 0, 0);
}
static hegel_schema * hegel_schema_long (void) {
  return hegel__integer (0, (int) sizeof (long), 1, LONG_MIN, LONG_MAX, 0, 0);
}
static hegel_schema * hegel_schema_long_range (int64_t lo, int64_t hi) {
  return hegel__integer (0, (int) sizeof (long), 1, lo, hi, 0, 0);
}

/* ---- unsigned constructors ---- */

static hegel_schema * hegel_schema_u8 (void) {
  return hegel__integer (0, 1, 0, 0, 0, 0, UINT8_MAX);
}
static hegel_schema * hegel_schema_u8_range (uint64_t lo, uint64_t hi) {
  return hegel__integer (0, 1, 0, 0, 0, lo, hi);
}
static hegel_schema * hegel_schema_u16 (void) {
  return hegel__integer (0, 2, 0, 0, 0, 0, UINT16_MAX);
}
static hegel_schema * hegel_schema_u16_range (uint64_t lo, uint64_t hi) {
  return hegel__integer (0, 2, 0, 0, 0, lo, hi);
}
static hegel_schema * hegel_schema_u32 (void) {
  return hegel__integer (0, 4, 0, 0, 0, 0, UINT32_MAX);
}
static hegel_schema * hegel_schema_u32_range (uint64_t lo, uint64_t hi) {
  return hegel__integer (0, 4, 0, 0, 0, lo, hi);
}
static hegel_schema * hegel_schema_u64 (void) {
  return hegel__integer (0, 8, 0, 0, 0, 0, UINT64_MAX);
}
static hegel_schema * hegel_schema_u64_range (uint64_t lo, uint64_t hi) {
  return hegel__integer (0, 8, 0, 0, 0, lo, hi);
}

/* ---- _at variants: set offset for direct struct fields ---- */

/* Internal: copy a schema and set its offset. */
static hegel_schema *
hegel__at (hegel_schema * s, size_t offset)
{
  s->offset = offset;
  return (s);
}

#define hegel_schema_i8_at(off)               hegel__at (hegel_schema_i8 (), (off))
#define hegel_schema_i8_range_at(off, lo, hi) hegel__at (hegel_schema_i8_range ((lo), (hi)), (off))
#define hegel_schema_i16_at(off)              hegel__at (hegel_schema_i16 (), (off))
#define hegel_schema_i16_range_at(off, lo, hi) hegel__at (hegel_schema_i16_range ((lo), (hi)), (off))
#define hegel_schema_i32_at(off)              hegel__at (hegel_schema_i32 (), (off))
#define hegel_schema_i32_range_at(off, lo, hi) hegel__at (hegel_schema_i32_range ((lo), (hi)), (off))
#define hegel_schema_i64_at(off)              hegel__at (hegel_schema_i64 (), (off))
#define hegel_schema_i64_range_at(off, lo, hi) hegel__at (hegel_schema_i64_range ((lo), (hi)), (off))
#define hegel_schema_int_at(off)              hegel__at (hegel_schema_int (), (off))
#define hegel_schema_int_range_at(off, lo, hi) hegel__at (hegel_schema_int_range ((lo), (hi)), (off))
#define hegel_schema_long_at(off)             hegel__at (hegel_schema_long (), (off))
#define hegel_schema_long_range_at(off, lo, hi) hegel__at (hegel_schema_long_range ((lo), (hi)), (off))
#define hegel_schema_u8_at(off)               hegel__at (hegel_schema_u8 (), (off))
#define hegel_schema_u8_range_at(off, lo, hi) hegel__at (hegel_schema_u8_range ((lo), (hi)), (off))
#define hegel_schema_u16_at(off)              hegel__at (hegel_schema_u16 (), (off))
#define hegel_schema_u16_range_at(off, lo, hi) hegel__at (hegel_schema_u16_range ((lo), (hi)), (off))
#define hegel_schema_u32_at(off)              hegel__at (hegel_schema_u32 (), (off))
#define hegel_schema_u32_range_at(off, lo, hi) hegel__at (hegel_schema_u32_range ((lo), (hi)), (off))
#define hegel_schema_u64_at(off)              hegel__at (hegel_schema_u64 (), (off))
#define hegel_schema_u64_range_at(off, lo, hi) hegel__at (hegel_schema_u64_range ((lo), (hi)), (off))

/* ================================================================
** Schema constructors — floats
** ================================================================ */

static hegel_schema *
hegel__fp (size_t offset, int width, double min_val, double max_val)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_FLOAT;
  s->offset = offset;
  s->fp.width = width;
  s->fp.min = min_val;
  s->fp.max = max_val;
  return (s);
}

static hegel_schema * hegel_schema_float (void) {
  return hegel__fp (0, 4, -FLT_MAX, FLT_MAX);
}
static hegel_schema * hegel_schema_float_range (double lo, double hi) {
  return hegel__fp (0, 4, lo, hi);
}
static hegel_schema * hegel_schema_double (void) {
  return hegel__fp (0, 8, -DBL_MAX, DBL_MAX);
}
static hegel_schema * hegel_schema_double_range (double lo, double hi) {
  return hegel__fp (0, 8, lo, hi);
}

#define hegel_schema_float_at(off)               hegel__at (hegel_schema_float (), (off))
#define hegel_schema_float_range_at(off, lo, hi) hegel__at (hegel_schema_float_range ((lo), (hi)), (off))
#define hegel_schema_double_at(off)              hegel__at (hegel_schema_double (), (off))
#define hegel_schema_double_range_at(off, lo, hi) hegel__at (hegel_schema_double_range ((lo), (hi)), (off))

/* ================================================================
** Schema constructors — text, optional, array, struct, self
** ================================================================ */

static hegel_schema *
hegel_schema_text_at (size_t offset, int min_len, int max_len)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_TEXT;
  s->offset = offset;
  s->text_range.min_len = min_len;
  s->text_range.max_len = max_len;
  return (s);
}

static hegel_schema *
hegel_schema_text (int min_len, int max_len)
{
  return (hegel_schema_text_at (0, min_len, max_len));
}

static hegel_schema *
hegel_schema_optional_ptr_at (size_t offset, hegel_schema * inner)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_OPTIONAL_PTR;
  s->offset = offset;
  s->optional_ptr.inner = inner;
  return (s);
}

static hegel_schema *
hegel_schema_array_at (size_t ptr_offset, size_t len_offset,
                       hegel_schema * elem, int min_len, int max_len)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_ARRAY;
  s->offset = ptr_offset;
  s->array_def.len_offset = len_offset;
  s->array_def.elem = elem;
  s->array_def.min_len = min_len;
  s->array_def.max_len = max_len;
  return (s);
}

static hegel_schema *
hegel_schema_self (void)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_SELF;
  s->self_ref.target = NULL;
  return (s);
}

/* ---- SELF resolution ---- */

static void
hegel__resolve_self (hegel_schema * node, hegel_schema * target)
{
  if (node == NULL) return;
  switch (node->kind) {
    case HEGEL_SCH_SELF:
      node->self_ref.target = target;
      break;
    case HEGEL_SCH_OPTIONAL_PTR:
      hegel__resolve_self (node->optional_ptr.inner, target);
      break;
    case HEGEL_SCH_ARRAY:
      hegel__resolve_self (node->array_def.elem, target);
      break;
    case HEGEL_SCH_STRUCT:
      for (int i = 0; i < node->struct_def.n_fields; i ++)
        hegel__resolve_self (node->struct_def.fields[i], target);
      break;
    default:
      break;
  }
}

/* ---- Struct constructor (variadic) ---- */

static hegel_schema *
hegel_schema_struct_v (size_t size, hegel_schema ** field_list)
{
  int                 n;
  hegel_schema *      s;

  for (n = 0; field_list[n] != NULL; n ++) {}

  s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_STRUCT;
  s->offset = 0;
  s->struct_def.size = size;
  s->struct_def.n_fields = n;
  s->struct_def.fields = (hegel_schema **) malloc (
      (size_t) n * sizeof (hegel_schema *));
  for (int i = 0; i < n; i ++)
    s->struct_def.fields[i] = field_list[i];

  hegel__resolve_self (s, s);
  return (s);
}

#define hegel_schema_struct(size, ...)                          \
  hegel_schema_struct_v ((size),                                \
      (hegel_schema *[]){ __VA_ARGS__ })

/* ================================================================
** Convenience macros — arg-count overloading
** ================================================================
**
** HEGEL_INT(T, f)           → full int range
** HEGEL_INT(T, f, lo, hi)   → constrained
**
** Uses the standard C99 VA_ARGS counting trick. */

#define HEGEL__NARG_(_4, _3, _2, _1, N, ...) N
#define HEGEL__NARG(...)  HEGEL__NARG_(__VA_ARGS__, 4, 3, 2, 1)

/* Signed integer macros */
#define HEGEL__I8_2(T, f)          hegel_schema_i8_at (offsetof (T, f))
#define HEGEL__I8_4(T, f, lo, hi)  hegel_schema_i8_range_at (offsetof (T, f), (lo), (hi))
#define HEGEL__I8_SEL(_1, _2, _3, _4, NAME, ...) NAME
#define HEGEL_I8(...) HEGEL__I8_SEL(__VA_ARGS__, HEGEL__I8_4, _skip, HEGEL__I8_2, _skip)(__VA_ARGS__)

#define HEGEL__I16_2(T, f)         hegel_schema_i16_at (offsetof (T, f))
#define HEGEL__I16_4(T, f, lo, hi) hegel_schema_i16_range_at (offsetof (T, f), (lo), (hi))
#define HEGEL__I16_SEL(_1, _2, _3, _4, NAME, ...) NAME
#define HEGEL_I16(...) HEGEL__I16_SEL(__VA_ARGS__, HEGEL__I16_4, _skip, HEGEL__I16_2, _skip)(__VA_ARGS__)

#define HEGEL__I32_2(T, f)         hegel_schema_i32_at (offsetof (T, f))
#define HEGEL__I32_4(T, f, lo, hi) hegel_schema_i32_range_at (offsetof (T, f), (lo), (hi))
#define HEGEL__I32_SEL(_1, _2, _3, _4, NAME, ...) NAME
#define HEGEL_I32(...) HEGEL__I32_SEL(__VA_ARGS__, HEGEL__I32_4, _skip, HEGEL__I32_2, _skip)(__VA_ARGS__)

#define HEGEL__I64_2(T, f)         hegel_schema_i64_at (offsetof (T, f))
#define HEGEL__I64_4(T, f, lo, hi) hegel_schema_i64_range_at (offsetof (T, f), (lo), (hi))
#define HEGEL__I64_SEL(_1, _2, _3, _4, NAME, ...) NAME
#define HEGEL_I64(...) HEGEL__I64_SEL(__VA_ARGS__, HEGEL__I64_4, _skip, HEGEL__I64_2, _skip)(__VA_ARGS__)

#define HEGEL__INT_2(T, f)         hegel_schema_int_at (offsetof (T, f))
#define HEGEL__INT_4(T, f, lo, hi) hegel_schema_int_range_at (offsetof (T, f), (lo), (hi))
#define HEGEL__INT_SEL(_1, _2, _3, _4, NAME, ...) NAME
#define HEGEL_INT(...) HEGEL__INT_SEL(__VA_ARGS__, HEGEL__INT_4, _skip, HEGEL__INT_2, _skip)(__VA_ARGS__)

#define HEGEL__LONG_2(T, f)        hegel_schema_long_at (offsetof (T, f))
#define HEGEL__LONG_4(T, f, lo, hi) hegel_schema_long_range_at (offsetof (T, f), (lo), (hi))
#define HEGEL__LONG_SEL(_1, _2, _3, _4, NAME, ...) NAME
#define HEGEL_LONG(...) HEGEL__LONG_SEL(__VA_ARGS__, HEGEL__LONG_4, _skip, HEGEL__LONG_2, _skip)(__VA_ARGS__)

/* Unsigned integer macros */
#define HEGEL__U8_2(T, f)          hegel_schema_u8_at (offsetof (T, f))
#define HEGEL__U8_4(T, f, lo, hi)  hegel_schema_u8_range_at (offsetof (T, f), (lo), (hi))
#define HEGEL__U8_SEL(_1, _2, _3, _4, NAME, ...) NAME
#define HEGEL_U8(...) HEGEL__U8_SEL(__VA_ARGS__, HEGEL__U8_4, _skip, HEGEL__U8_2, _skip)(__VA_ARGS__)

#define HEGEL__U16_2(T, f)         hegel_schema_u16_at (offsetof (T, f))
#define HEGEL__U16_4(T, f, lo, hi) hegel_schema_u16_range_at (offsetof (T, f), (lo), (hi))
#define HEGEL__U16_SEL(_1, _2, _3, _4, NAME, ...) NAME
#define HEGEL_U16(...) HEGEL__U16_SEL(__VA_ARGS__, HEGEL__U16_4, _skip, HEGEL__U16_2, _skip)(__VA_ARGS__)

#define HEGEL__U32_2(T, f)         hegel_schema_u32_at (offsetof (T, f))
#define HEGEL__U32_4(T, f, lo, hi) hegel_schema_u32_range_at (offsetof (T, f), (lo), (hi))
#define HEGEL__U32_SEL(_1, _2, _3, _4, NAME, ...) NAME
#define HEGEL_U32(...) HEGEL__U32_SEL(__VA_ARGS__, HEGEL__U32_4, _skip, HEGEL__U32_2, _skip)(__VA_ARGS__)

#define HEGEL__U64_2(T, f)         hegel_schema_u64_at (offsetof (T, f))
#define HEGEL__U64_4(T, f, lo, hi) hegel_schema_u64_range_at (offsetof (T, f), (lo), (hi))
#define HEGEL__U64_SEL(_1, _2, _3, _4, NAME, ...) NAME
#define HEGEL_U64(...) HEGEL__U64_SEL(__VA_ARGS__, HEGEL__U64_4, _skip, HEGEL__U64_2, _skip)(__VA_ARGS__)

/* Float macros */
#define HEGEL__FLOAT_2(T, f)         hegel_schema_float_at (offsetof (T, f))
#define HEGEL__FLOAT_4(T, f, lo, hi) hegel_schema_float_range_at (offsetof (T, f), (lo), (hi))
#define HEGEL__FLOAT_SEL(_1, _2, _3, _4, NAME, ...) NAME
#define HEGEL_FLOAT(...) HEGEL__FLOAT_SEL(__VA_ARGS__, HEGEL__FLOAT_4, _skip, HEGEL__FLOAT_2, _skip)(__VA_ARGS__)

#define HEGEL__DOUBLE_2(T, f)         hegel_schema_double_at (offsetof (T, f))
#define HEGEL__DOUBLE_4(T, f, lo, hi) hegel_schema_double_range_at (offsetof (T, f), (lo), (hi))
#define HEGEL__DOUBLE_SEL(_1, _2, _3, _4, NAME, ...) NAME
#define HEGEL_DOUBLE(...) HEGEL__DOUBLE_SEL(__VA_ARGS__, HEGEL__DOUBLE_4, _skip, HEGEL__DOUBLE_2, _skip)(__VA_ARGS__)

/* Text + structural macros (no overloading needed) */
#define HEGEL_TEXT(type, field, lo, hi) \
  hegel_schema_text_at (offsetof (type, field), (lo), (hi))

#define HEGEL_OPTIONAL(type, field, inner) \
  hegel_schema_optional_ptr_at (offsetof (type, field), (inner))

#define HEGEL_SELF(type, field) \
  hegel_schema_optional_ptr_at (offsetof (type, field), \
      hegel_schema_self ())

#define HEGEL_ARRAY(type, ptr_field, len_field, elem, lo, hi) \
  hegel_schema_array_at (offsetof (type, ptr_field),           \
      offsetof (type, len_field), (elem), (lo), (hi))

/* ================================================================
** Draw
** ================================================================ */

static hegel_shape *
hegel__draw_struct (hegel_testcase * tc, hegel_schema * gen, void ** out,
                    int depth);
static hegel_shape *
hegel__draw_field (hegel_testcase * tc, hegel_schema * gen, void * parent,
                   int depth);

/* ---- Draw an integer into memory at `dst` ---- */

static void
hegel__draw_integer_into (hegel_testcase * tc, hegel_schema * gen, void * dst)
{
  if (gen->integer.is_signed) {
    int64_t v = hegel_draw_i64 (tc, gen->integer.min_s, gen->integer.max_s);
    switch (gen->integer.width) {
      case 1: *(int8_t *)  dst = (int8_t)  v; break;
      case 2: *(int16_t *) dst = (int16_t) v; break;
      case 4: *(int32_t *) dst = (int32_t) v; break;
      case 8: *(int64_t *) dst = v;            break;
    }
  } else {
    uint64_t v = hegel_draw_u64 (tc, gen->integer.min_u, gen->integer.max_u);
    switch (gen->integer.width) {
      case 1: *(uint8_t *)  dst = (uint8_t)  v; break;
      case 2: *(uint16_t *) dst = (uint16_t) v; break;
      case 4: *(uint32_t *) dst = (uint32_t) v; break;
      case 8: *(uint64_t *) dst = v;             break;
    }
  }
}

/* ---- Draw a float/double into memory at `dst` ---- */

static void
hegel__draw_fp_into (hegel_testcase * tc, hegel_schema * gen, void * dst)
{
  if (gen->fp.width == 4) {
    float v = hegel_draw_float (tc, (float) gen->fp.min, (float) gen->fp.max);
    *(float *) dst = v;
  } else {
    double v = hegel_draw_double (tc, gen->fp.min, gen->fp.max);
    *(double *) dst = v;
  }
}

/* ---- Draw a text string, return malloc'd buffer ---- */

static char *
hegel__draw_text (hegel_testcase * tc, int min_len, int max_len)
{
  int   len = hegel_draw_int (tc, min_len, max_len);
  char * buf = (char *) malloc ((size_t) len + 1);
  int   i;
  for (i = 0; i < len; i ++)
    buf[i] = (char) hegel_draw_int (tc, 'a', 'z');
  buf[len] = 0;
  return (buf);
}

/* ---- Draw a standalone value (for OPTIONAL inner) ---- */

static hegel_shape *
hegel__draw_alloc (hegel_testcase * tc, hegel_schema * gen, void ** out,
                   int depth)
{
  hegel_schema *      actual;
  hegel_shape *       shape;

  actual = gen;
  if (gen->kind == HEGEL_SCH_SELF)
    actual = gen->self_ref.target;

  switch (actual->kind) {

    case HEGEL_SCH_STRUCT:
      return (hegel__draw_struct (tc, actual, out, depth));

    case HEGEL_SCH_TEXT: {
      char * buf = hegel__draw_text (tc,
          actual->text_range.min_len, actual->text_range.max_len);
      *out = buf;
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_TEXT;
      shape->owned = buf;
      return (shape);
    }

    case HEGEL_SCH_INTEGER: {
      void * p = calloc (1, (size_t) actual->integer.width);
      hegel__draw_integer_into (tc, actual, p);
      *out = p;
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_SCALAR;
      shape->owned = p;
      return (shape);
    }

    default:
      *out = NULL;
      return (NULL);
  }
}

/* ---- Element byte size (for array allocation) ---- */

static size_t
hegel__elem_size (hegel_schema * elem)
{
  switch (elem->kind) {
    case HEGEL_SCH_INTEGER:  return ((size_t) elem->integer.width);
    case HEGEL_SCH_FLOAT:    return ((size_t) elem->fp.width);
    case HEGEL_SCH_TEXT:     return (sizeof (char *));
    case HEGEL_SCH_STRUCT:   return (sizeof (void *));
    case HEGEL_SCH_SELF:
      if (elem->self_ref.target != NULL)
        return (sizeof (void *));
      return (sizeof (int));
    default:                 return (sizeof (int));
  }
}

/* ---- Draw struct ---- */

static hegel_shape *
hegel__draw_struct (hegel_testcase * tc, hegel_schema * gen, void ** out,
                    int depth)
{
  void *              ptr;
  hegel_shape *       shape;
  int                 n;
  int                 i;

  n = gen->struct_def.n_fields;
  ptr = calloc (1, gen->struct_def.size);

  shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
  shape->kind = HEGEL_SHAPE_STRUCT;
  shape->owned = ptr;
  shape->struct_shape.n_fields = n;
  shape->struct_shape.fields = (hegel_shape **) calloc (
      (size_t) n, sizeof (hegel_shape *));

  hegel_start_span (tc, HEGEL_SPAN_TUPLE);
  for (i = 0; i < n; i ++)
    shape->struct_shape.fields[i] =
        hegel__draw_field (tc, gen->struct_def.fields[i], ptr, depth);
  hegel_stop_span (tc, 0);

  *out = ptr;
  return (shape);
}

/* ---- Draw field into parent struct ---- */

static hegel_shape *
hegel__draw_field (hegel_testcase * tc, hegel_schema * gen, void * parent,
                   int depth)
{
  hegel_shape *       shape;

  switch (gen->kind) {

    case HEGEL_SCH_INTEGER: {
      hegel__draw_integer_into (tc, gen,
          (char *) parent + gen->offset);
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_SCALAR;
      return (shape);
    }

    case HEGEL_SCH_FLOAT: {
      hegel__draw_fp_into (tc, gen,
          (char *) parent + gen->offset);
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_SCALAR;
      return (shape);
    }

    case HEGEL_SCH_TEXT: {
      char * buf = hegel__draw_text (tc,
          gen->text_range.min_len, gen->text_range.max_len);
      *(char **) ((char *) parent + gen->offset) = buf;
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_TEXT;
      shape->owned = buf;
      return (shape);
    }

    case HEGEL_SCH_OPTIONAL_PTR: {
      int present;
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_OPTIONAL;

      hegel_start_span (tc, HEGEL_SPAN_OPTIONAL);
      present = hegel_draw_int (tc, 0, 1);

      if (present && depth > 0) {
        void * child;
        shape->optional_shape.inner =
            hegel__draw_alloc (tc, gen->optional_ptr.inner, &child,
                               depth - 1);
        shape->optional_shape.is_some =
            (shape->optional_shape.inner != NULL);
        if (shape->optional_shape.is_some)
          *(void **) ((char *) parent + gen->offset) = child;
      } else {
        shape->optional_shape.is_some = 0;
        shape->optional_shape.inner = NULL;
      }

      hegel_stop_span (tc, 0);
      return (shape);
    }

    case HEGEL_SCH_ARRAY: {
      int             n;
      size_t          esz;
      hegel_schema *  elem;
      void *          arr;

      elem = gen->array_def.elem;
      if (elem->kind == HEGEL_SCH_SELF && elem->self_ref.target)
        elem = elem->self_ref.target;
      esz = hegel__elem_size (elem);

      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_ARRAY;

      hegel_start_span (tc, HEGEL_SPAN_LIST);
      n = hegel_draw_int (tc, gen->array_def.min_len,
                              gen->array_def.max_len);
      arr = calloc ((size_t) n + 1, esz);

      shape->array_shape.len = n;
      shape->array_shape.elems = (hegel_shape **) calloc (
          (size_t) n + 1, sizeof (hegel_shape *));
      shape->owned = arr;

      *(void **) ((char *) parent + gen->offset) = arr;
      *(int *) ((char *) parent + gen->array_def.len_offset) = n;

      for (int i = 0; i < n; i ++) {
        hegel_start_span (tc, HEGEL_SPAN_LIST_ELEMENT);

        if (elem->kind == HEGEL_SCH_INTEGER) {
          hegel__draw_integer_into (tc, elem,
              (char *) arr + (size_t) i * esz);
          shape->array_shape.elems[i] =
              (hegel_shape *) calloc (1, sizeof (hegel_shape));
          shape->array_shape.elems[i]->kind = HEGEL_SHAPE_SCALAR;

        } else if (elem->kind == HEGEL_SCH_FLOAT) {
          hegel__draw_fp_into (tc, elem,
              (char *) arr + (size_t) i * esz);
          shape->array_shape.elems[i] =
              (hegel_shape *) calloc (1, sizeof (hegel_shape));
          shape->array_shape.elems[i]->kind = HEGEL_SHAPE_SCALAR;

        } else if (elem->kind == HEGEL_SCH_TEXT) {
          void * txt;
          shape->array_shape.elems[i] =
              hegel__draw_alloc (tc, elem, &txt, depth);
          ((char **) arr)[i] = (char *) txt;

        } else if (elem->kind == HEGEL_SCH_STRUCT) {
          void * child;
          shape->array_shape.elems[i] =
              hegel__draw_struct (tc, elem, &child, depth - 1);
          ((void **) arr)[i] = child;
        }

        hegel_stop_span (tc, 0);
      }

      hegel_stop_span (tc, 0);
      return (shape);
    }

    case HEGEL_SCH_SELF:
    case HEGEL_SCH_STRUCT:
      break;
  }

  return (NULL);
}

/* ================================================================
** Top-level draw
** ================================================================ */

#define HEGEL_DEFAULT_MAX_DEPTH 5

static hegel_shape *
hegel_schema_draw_n (hegel_testcase * tc, hegel_schema * gen,
                     void ** out, int max_depth)
{
  if (gen->kind != HEGEL_SCH_STRUCT) {
    *out = NULL;
    return (NULL);
  }
  return (hegel__draw_struct (tc, gen, out, max_depth));
}

static hegel_shape *
hegel_schema_draw (hegel_testcase * tc, hegel_schema * gen, void ** out)
{
  return (hegel_schema_draw_n (tc, gen, out, HEGEL_DEFAULT_MAX_DEPTH));
}

/* ================================================================
** Free
** ================================================================ */

static void
hegel_shape_free (hegel_shape * s)
{
  int                 i;

  if (s == NULL) return;

  switch (s->kind) {
    case HEGEL_SHAPE_SCALAR:
      free (s->owned);
      break;
    case HEGEL_SHAPE_TEXT:
      free (s->owned);
      break;
    case HEGEL_SHAPE_STRUCT:
      for (i = 0; i < s->struct_shape.n_fields; i ++)
        hegel_shape_free (s->struct_shape.fields[i]);
      free (s->struct_shape.fields);
      free (s->owned);
      break;
    case HEGEL_SHAPE_OPTIONAL:
      if (s->optional_shape.is_some)
        hegel_shape_free (s->optional_shape.inner);
      break;
    case HEGEL_SHAPE_ARRAY:
      for (i = 0; i < s->array_shape.len; i ++)
        hegel_shape_free (s->array_shape.elems[i]);
      free (s->array_shape.elems);
      free (s->owned);
      break;
  }

  free (s);
}

static void
hegel_schema_free (hegel_schema * s)
{
  int                 i;

  if (s == NULL) return;

  switch (s->kind) {
    case HEGEL_SCH_STRUCT:
      for (i = 0; i < s->struct_def.n_fields; i ++)
        hegel_schema_free (s->struct_def.fields[i]);
      free (s->struct_def.fields);
      break;
    case HEGEL_SCH_OPTIONAL_PTR:
      hegel_schema_free (s->optional_ptr.inner);
      break;
    case HEGEL_SCH_ARRAY:
      hegel_schema_free (s->array_def.elem);
      break;
    case HEGEL_SCH_SELF:
      break;
    default:
      break;
  }

  free (s);
}

#ifdef __cplusplus
}
#endif

#endif /* HEGEL_GEN_H */
