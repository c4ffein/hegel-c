/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Implementation of the schema/shape system declared in hegel_gen.h.
** Pure C; uses only the primitives from hegel_c.h (hegel_draw_int,
** hegel_start_span, etc.).  Backend-agnostic — works with the Rust
** libhegel_c.a or a future pure-C backend. */

#include "hegel_gen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <limits.h>
#include <float.h>

/* ================================================================
** Refcount
** ================================================================ */

hegel_schema_t
hegel_schema_ref (hegel_schema_t s)
{
  if (s._raw != NULL) s._raw->refcount ++;
  return (s);
}

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
  s->refcount = 1;
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
hegel_schema_t hegel_schema_i8 (void) {
  return (hegel_schema_t){hegel__integer (0, 1, 1, INT8_MIN, INT8_MAX, 0, 0)};
}
hegel_schema_t hegel_schema_i8_range (int64_t lo, int64_t hi) {
  return (hegel_schema_t){hegel__integer (0, 1, 1, lo, hi, 0, 0)};
}
hegel_schema_t hegel_schema_i16 (void) {
  return (hegel_schema_t){hegel__integer (0, 2, 1, INT16_MIN, INT16_MAX, 0, 0)};
}
hegel_schema_t hegel_schema_i16_range (int64_t lo, int64_t hi) {
  return (hegel_schema_t){hegel__integer (0, 2, 1, lo, hi, 0, 0)};
}
hegel_schema_t hegel_schema_i32 (void) {
  return (hegel_schema_t){hegel__integer (0, 4, 1, INT32_MIN, INT32_MAX, 0, 0)};
}
hegel_schema_t hegel_schema_i32_range (int64_t lo, int64_t hi) {
  return (hegel_schema_t){hegel__integer (0, 4, 1, lo, hi, 0, 0)};
}
hegel_schema_t hegel_schema_i64 (void) {
  return (hegel_schema_t){hegel__integer (0, 8, 1, INT64_MIN, INT64_MAX, 0, 0)};
}
hegel_schema_t hegel_schema_i64_range (int64_t lo, int64_t hi) {
  return (hegel_schema_t){hegel__integer (0, 8, 1, lo, hi, 0, 0)};
}

/* platform-width signed */
hegel_schema_t hegel_schema_int (void) {
  return (hegel_schema_t){hegel__integer (0, (int) sizeof (int), 1, INT_MIN, INT_MAX, 0, 0)};
}
hegel_schema_t hegel_schema_int_range (int64_t lo, int64_t hi) {
  return (hegel_schema_t){hegel__integer (0, (int) sizeof (int), 1, lo, hi, 0, 0)};
}
hegel_schema_t hegel_schema_long (void) {
  return (hegel_schema_t){hegel__integer (0, (int) sizeof (long), 1, LONG_MIN, LONG_MAX, 0, 0)};
}
hegel_schema_t hegel_schema_long_range (int64_t lo, int64_t hi) {
  return (hegel_schema_t){hegel__integer (0, (int) sizeof (long), 1, lo, hi, 0, 0)};
}

/* ---- unsigned constructors ---- */

hegel_schema_t hegel_schema_u8 (void) {
  return (hegel_schema_t){hegel__integer (0, 1, 0, 0, 0, 0, UINT8_MAX)};
}
hegel_schema_t hegel_schema_u8_range (uint64_t lo, uint64_t hi) {
  return (hegel_schema_t){hegel__integer (0, 1, 0, 0, 0, lo, hi)};
}
hegel_schema_t hegel_schema_u16 (void) {
  return (hegel_schema_t){hegel__integer (0, 2, 0, 0, 0, 0, UINT16_MAX)};
}
hegel_schema_t hegel_schema_u16_range (uint64_t lo, uint64_t hi) {
  return (hegel_schema_t){hegel__integer (0, 2, 0, 0, 0, lo, hi)};
}
hegel_schema_t hegel_schema_u32 (void) {
  return (hegel_schema_t){hegel__integer (0, 4, 0, 0, 0, 0, UINT32_MAX)};
}
hegel_schema_t hegel_schema_u32_range (uint64_t lo, uint64_t hi) {
  return (hegel_schema_t){hegel__integer (0, 4, 0, 0, 0, lo, hi)};
}
hegel_schema_t hegel_schema_u64 (void) {
  return (hegel_schema_t){hegel__integer (0, 8, 0, 0, 0, 0, UINT64_MAX)};
}
hegel_schema_t hegel_schema_u64_range (uint64_t lo, uint64_t hi) {
  return (hegel_schema_t){hegel__integer (0, 8, 0, 0, 0, lo, hi)};
}

/* ---- _at variants: set offset for direct struct fields ---- */

/* Internal: copy a schema and set its offset. */
hegel_schema_t
hegel__at (hegel_schema_t s, size_t offset)
{
  s._raw->offset = offset;
  return (s);
}


/* ================================================================
** Schema constructors — floats
** ================================================================ */

static hegel_schema *
hegel__fp (size_t offset, int width, double min_val, double max_val)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_FLOAT;
  s->offset = offset;
  s->refcount = 1;
  s->fp.width = width;
  s->fp.min = min_val;
  s->fp.max = max_val;
  return (s);
}

hegel_schema_t hegel_schema_float (void) {
  return (hegel_schema_t){hegel__fp (0, 4, -FLT_MAX, FLT_MAX)};
}
hegel_schema_t hegel_schema_float_range (double lo, double hi) {
  return (hegel_schema_t){hegel__fp (0, 4, lo, hi)};
}
hegel_schema_t hegel_schema_double (void) {
  return (hegel_schema_t){hegel__fp (0, 8, -DBL_MAX, DBL_MAX)};
}
hegel_schema_t hegel_schema_double_range (double lo, double hi) {
  return (hegel_schema_t){hegel__fp (0, 8, lo, hi)};
}


/* ================================================================
** Schema constructors — text, optional, array, struct, self
** ================================================================ */

hegel_schema_t
hegel_schema_text_at (size_t offset, int min_len, int max_len)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_TEXT;
  s->offset = offset;
  s->refcount = 1;
  s->text_range.min_len = min_len;
  s->text_range.max_len = max_len;
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_text (int min_len, int max_len)
{
  return (hegel_schema_text_at (0, min_len, max_len));
}

hegel_schema_t
hegel_schema_optional_ptr_at (size_t offset, hegel_schema_t inner)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_OPTIONAL_PTR;
  s->offset = offset;
  s->refcount = 1;
  s->optional_ptr.inner = inner._raw;  /* transfer ownership, unwrap */
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_array_at (size_t ptr_offset, size_t len_offset,
                       hegel_schema_t elem, int min_len, int max_len)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_ARRAY;
  s->offset = ptr_offset;
  s->refcount = 1;
  s->array_def.len_offset = len_offset;
  s->array_def.elem = elem._raw;  /* transfer ownership, unwrap */
  s->array_def.min_len = min_len;
  s->array_def.max_len = max_len;
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_array_inline_at (size_t ptr_offset, size_t len_offset,
                              hegel_schema_t elem, size_t elem_size,
                              int min_len, int max_len)
{
  hegel_schema * e = elem._raw;
  /* Runtime check: ARRAY_INLINE elements must be STRUCT or UNION.
  ** Passing a VARIANT or scalar schema here would be silently wrong
  ** (VARIANT stores a pointer, which defeats "inline"; scalars should
  ** use HEGEL_ARRAY). Fail loudly at setup time. */
  if (e->kind != HEGEL_SCH_STRUCT
      && e->kind != HEGEL_SCH_UNION
      && e->kind != HEGEL_SCH_UNION_UNTAGGED) {
    fprintf (stderr,
        "hegel_schema_array_inline_at: elem kind %d not supported "
        "(must be STRUCT, UNION, or UNION_UNTAGGED)\n",
        (int) e->kind);
    abort ();
  }

  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_ARRAY_INLINE;
  s->offset = ptr_offset;
  s->refcount = 1;
  s->array_inline_def.len_offset = len_offset;
  s->array_inline_def.elem = e;  /* transfer ownership */
  s->array_inline_def.elem_size = elem_size;
  s->array_inline_def.min_len = min_len;
  s->array_inline_def.max_len = max_len;
  return (hegel_schema_t){s};
}

/* Union constructors.  `case_list` is a NULL-terminated array of pointers
** to H_END-terminated field arrays.  Each field array comes from a
** HEGEL_CASE(...) macro compound literal on the caller's stack — we
** deep-copy into heap storage before returning. */
hegel_schema_t
hegel__union (size_t tag_offset, hegel_schema_kind kind,
              hegel_schema_t ** case_list)
{
  int                 n;
  hegel_schema *      s;

  for (n = 0; case_list[n] != NULL; n ++) {}

  s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = kind;
  s->offset = 0;
  s->refcount = 1;
  s->union_def.tag_offset = tag_offset;
  s->union_def.n_cases = n;
  s->union_def.cases = (hegel_schema ***) malloc (
      (size_t) n * sizeof (hegel_schema **));
  for (int i = 0; i < n; i ++) {
    /* case_list[i] is a H_END-terminated array of hegel_schema_t.
    ** Count fields, allocate, unwrap. */
    int nf;
    for (nf = 0; case_list[i][nf]._raw != NULL; nf ++) {}
    s->union_def.cases[i] = (hegel_schema **) malloc (
        (size_t) (nf + 1) * sizeof (hegel_schema *));
    for (int j = 0; j < nf; j ++)
      s->union_def.cases[i][j] = case_list[i][j]._raw;  /* unwrap + transfer */
    s->union_def.cases[i][nf] = NULL;
  }
  return (hegel_schema_t){s};
}

/* Variant constructor.  `cases` is a NULL-terminated array of STRUCT
** schemas — one per variant.  The chosen one gets allocated and its
** pointer stored at ptr_offset. */
hegel_schema_t
hegel_schema_variant_v (size_t tag_offset, size_t ptr_offset,
                        hegel_schema_t * case_list)
{
  int                 n;
  hegel_schema *      s;

  /* case_list is H_END-terminated (from the HEGEL_VARIANT macro) */
  for (n = 0; case_list[n]._raw != NULL; n ++) {}

  s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_VARIANT;
  s->offset = 0;
  s->refcount = 1;
  s->variant_def.tag_offset = tag_offset;
  s->variant_def.ptr_offset = ptr_offset;
  s->variant_def.n_cases = n;
  s->variant_def.cases = (hegel_schema **) malloc (
      (size_t) n * sizeof (hegel_schema *));
  for (int i = 0; i < n; i ++)
    s->variant_def.cases[i] = case_list[i]._raw;  /* unwrap + transfer */
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_self (void)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_SELF;
  s->refcount = 1;
  s->self_ref.target = NULL;   /* target is a back-ref, not owned */
  return (hegel_schema_t){s};
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
    case HEGEL_SCH_ARRAY_INLINE:
      hegel__resolve_self (node->array_inline_def.elem, target);
      break;
    case HEGEL_SCH_STRUCT:
      for (int i = 0; i < node->struct_def.n_fields; i ++)
        hegel__resolve_self (node->struct_def.fields[i], target);
      break;
    case HEGEL_SCH_UNION:
    case HEGEL_SCH_UNION_UNTAGGED:
      for (int i = 0; i < node->union_def.n_cases; i ++)
        for (int j = 0; node->union_def.cases[i][j] != NULL; j ++)
          hegel__resolve_self (node->union_def.cases[i][j], target);
      break;
    case HEGEL_SCH_VARIANT:
      for (int i = 0; i < node->variant_def.n_cases; i ++)
        hegel__resolve_self (node->variant_def.cases[i], target);
      break;
    default:
      break;
  }
}

/* ---- Struct constructor (variadic) ---- */

hegel_schema_t
hegel_schema_struct_v (size_t size, hegel_schema_t * field_list)
{
  int                 n;
  hegel_schema *      s;

  /* field_list is H_END-terminated */
  for (n = 0; field_list[n]._raw != NULL; n ++) {}

  s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_STRUCT;
  s->offset = 0;
  s->refcount = 1;
  s->struct_def.size = size;
  s->struct_def.n_fields = n;
  s->struct_def.fields = (hegel_schema **) malloc (
      (size_t) n * sizeof (hegel_schema *));
  for (int i = 0; i < n; i ++)
    s->struct_def.fields[i] = field_list[i]._raw;  /* unwrap + transfer */

  hegel__resolve_self (s, s);
  return (hegel_schema_t){s};
}


/* ================================================================
** Convenience macros — arg-count overloading
** ================================================================
**
** HEGEL_INT(T, f)           → full int range
** HEGEL_INT(T, f, lo, hi)   → constrained
**
** Uses the standard C99 VA_ARGS counting trick. */


/* Signed integer macros */






/* Unsigned integer macros */




/* Float macros */


/* Text + structural macros (no overloading needed) */





/* Tagged union (inline data, tag written to struct):
** HEGEL_UNION(Shape, tag,
**     HEGEL_CASE(field1, field2, ..., NULL),
**     HEGEL_CASE(field3, ..., NULL),
**     NULL)  */


/* Tag-less union (tag lives in shape tree only, not in struct): */

/* Variant (tag + pointer to separately allocated struct):
** HEGEL_VARIANT(Shape, tag, value,
**     struct_schema_A,
**     struct_schema_B,
**     NULL)  */

/* ================================================================
** Shape accessors — read metadata from shape tree
** ================================================================ */

int
hegel_shape_tag (hegel_shape * s)
{
  if (s != NULL && s->kind == HEGEL_SHAPE_VARIANT)
    return (s->variant_shape.tag);
  return (-1);
}

int
hegel_shape_array_len (hegel_shape * s)
{
  if (s != NULL && s->kind == HEGEL_SHAPE_ARRAY)
    return (s->array_shape.len);
  return (0);
}

int
hegel_shape_is_some (hegel_shape * s)
{
  if (s != NULL && s->kind == HEGEL_SHAPE_OPTIONAL)
    return (s->optional_shape.is_some);
  return (0);
}

/* Access a specific field's shape from a struct shape (by index). */
hegel_shape *
hegel_shape_field (hegel_shape * s, int index)
{
  if (s == NULL || s->kind != HEGEL_SHAPE_STRUCT) return (NULL);
  if (index < 0 || index >= s->struct_shape.n_fields) return (NULL);
  return (s->struct_shape.fields[index]);
}

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

    case HEGEL_SCH_ARRAY_INLINE: {
      int             n;
      size_t          esz;
      hegel_schema *  elem;
      void *          arr;

      elem = gen->array_inline_def.elem;
      if (elem->kind == HEGEL_SCH_SELF && elem->self_ref.target)
        elem = elem->self_ref.target;
      esz = gen->array_inline_def.elem_size;

      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_ARRAY;

      hegel_start_span (tc, HEGEL_SPAN_LIST);
      n = hegel_draw_int (tc, gen->array_inline_def.min_len,
                              gen->array_inline_def.max_len);
      arr = calloc ((size_t) n + 1, esz);

      shape->array_shape.len = n;
      shape->array_shape.elems = (hegel_shape **) calloc (
          (size_t) n + 1, sizeof (hegel_shape *));
      shape->owned = arr;

      *(void **) ((char *) parent + gen->offset) = arr;
      *(int *) ((char *) parent + gen->array_inline_def.len_offset) = n;

      for (int i = 0; i < n; i ++) {
        void * slot = (char *) arr + (size_t) i * esz;
        hegel_start_span (tc, HEGEL_SPAN_LIST_ELEMENT);

        /* Draw fields of elem schema into the slot (as if slot
        ** is the parent struct for the element's fields). */
        if (elem->kind == HEGEL_SCH_STRUCT) {
          /* Inline struct: draw each field into slot. */
          hegel_shape * es = (hegel_shape *) calloc (1, sizeof (hegel_shape));
          es->kind = HEGEL_SHAPE_STRUCT;
          es->owned = NULL; /* slot is part of array alloc, not separate */
          es->struct_shape.n_fields = elem->struct_def.n_fields;
          es->struct_shape.fields = (hegel_shape **) calloc (
              (size_t) elem->struct_def.n_fields, sizeof (hegel_shape *));
          hegel_start_span (tc, HEGEL_SPAN_TUPLE);
          for (int f = 0; f < elem->struct_def.n_fields; f ++)
            es->struct_shape.fields[f] =
                hegel__draw_field (tc, elem->struct_def.fields[f],
                                   slot, depth - 1);
          hegel_stop_span (tc, 0);
          shape->array_shape.elems[i] = es;

        } else if (elem->kind == HEGEL_SCH_UNION
                || elem->kind == HEGEL_SCH_UNION_UNTAGGED) {
          /* Inline union element — draw it into slot. */
          shape->array_shape.elems[i] =
              hegel__draw_field (tc, elem, slot, depth - 1);

        } else {
          /* Scalar element in inline array — shouldn't happen
          ** (use HEGEL_ARRAY for scalar arrays), but handle it. */
          shape->array_shape.elems[i] =
              hegel__draw_field (tc, elem, slot, depth - 1);
        }

        hegel_stop_span (tc, 0);
      }

      hegel_stop_span (tc, 0);
      return (shape);
    }

    case HEGEL_SCH_UNION:
    case HEGEL_SCH_UNION_UNTAGGED: {
      int             tag;
      int             nc;
      hegel_schema ** chosen_fields;

      nc = gen->union_def.n_cases;

      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_VARIANT;

      hegel_start_span (tc, HEGEL_SPAN_ONE_OF);
      tag = hegel_draw_int (tc, 0, nc - 1);
      shape->variant_shape.tag = tag;

      /* Write tag to struct if tagged (not UNTAGGED). */
      if (gen->kind == HEGEL_SCH_UNION)
        *(int *) ((char *) parent + gen->union_def.tag_offset) = tag;

      /* Draw the chosen case's fields into parent struct. */
      chosen_fields = gen->union_def.cases[tag];
      hegel_start_span (tc, HEGEL_SPAN_ENUM_VARIANT);

      /* Build a struct-like shape for the variant's fields. */
      {
        int nf;
        for (nf = 0; chosen_fields[nf] != NULL; nf ++) {}
        hegel_shape * vs = (hegel_shape *) calloc (1, sizeof (hegel_shape));
        vs->kind = HEGEL_SHAPE_STRUCT;
        vs->owned = NULL; /* fields are inline in parent, not separate */
        vs->struct_shape.n_fields = nf;
        vs->struct_shape.fields = (hegel_shape **) calloc (
            (size_t) nf, sizeof (hegel_shape *));
        for (int i = 0; i < nf; i ++)
          vs->struct_shape.fields[i] =
              hegel__draw_field (tc, chosen_fields[i], parent, depth);
        shape->variant_shape.inner = vs;
      }

      hegel_stop_span (tc, 0);
      hegel_stop_span (tc, 0);
      return (shape);
    }

    case HEGEL_SCH_VARIANT: {
      int             tag;
      int             nc;
      hegel_schema *  chosen;

      nc = gen->variant_def.n_cases;

      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_VARIANT;

      hegel_start_span (tc, HEGEL_SPAN_ONE_OF);
      tag = hegel_draw_int (tc, 0, nc - 1);
      shape->variant_shape.tag = tag;

      /* Write tag to struct. */
      *(int *) ((char *) parent + gen->variant_def.tag_offset) = tag;

      /* Allocate and draw the chosen variant struct. */
      chosen = gen->variant_def.cases[tag];
      hegel_start_span (tc, HEGEL_SPAN_ENUM_VARIANT);
      {
        void * child;
        shape->variant_shape.inner =
            hegel__draw_struct (tc, chosen, &child, depth - 1);
        *(void **) ((char *) parent + gen->variant_def.ptr_offset) = child;
      }
      hegel_stop_span (tc, 0);

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


hegel_shape *
hegel_schema_draw_n (hegel_testcase * tc, hegel_schema_t gen,
                     void ** out, int max_depth)
{
  hegel_schema * g = gen._raw;
  if (g == NULL || g->kind != HEGEL_SCH_STRUCT) {
    *out = NULL;
    return (NULL);
  }
  return (hegel__draw_struct (tc, g, out, max_depth));
}

hegel_shape *
hegel_schema_draw (hegel_testcase * tc, hegel_schema_t gen, void ** out)
{
  return (hegel_schema_draw_n (tc, gen, out, HEGEL_DEFAULT_MAX_DEPTH));
}

/* ================================================================
** Free
** ================================================================ */

void
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
    case HEGEL_SHAPE_VARIANT:
      hegel_shape_free (s->variant_shape.inner);
      break;
  }

  free (s);
}

static void
hegel__schema_free_raw (hegel_schema * s)
{
  int                 i;

  if (s == NULL) return;

  /* Decrement refcount; only actually free when it hits 0. */
  s->refcount --;
  if (s->refcount > 0) return;

  switch (s->kind) {
    case HEGEL_SCH_STRUCT:
      for (i = 0; i < s->struct_def.n_fields; i ++)
        hegel__schema_free_raw (s->struct_def.fields[i]);
      free (s->struct_def.fields);
      break;
    case HEGEL_SCH_OPTIONAL_PTR:
      hegel__schema_free_raw (s->optional_ptr.inner);
      break;
    case HEGEL_SCH_ARRAY:
      hegel__schema_free_raw (s->array_def.elem);
      break;
    case HEGEL_SCH_ARRAY_INLINE:
      hegel__schema_free_raw (s->array_inline_def.elem);
      break;
    case HEGEL_SCH_UNION:
    case HEGEL_SCH_UNION_UNTAGGED:
      for (i = 0; i < s->union_def.n_cases; i ++) {
        int j;
        for (j = 0; s->union_def.cases[i][j] != NULL; j ++)
          hegel__schema_free_raw (s->union_def.cases[i][j]);
        free (s->union_def.cases[i]);
      }
      free (s->union_def.cases);
      break;
    case HEGEL_SCH_VARIANT:
      for (i = 0; i < s->variant_def.n_cases; i ++)
        hegel__schema_free_raw (s->variant_def.cases[i]);
      free (s->variant_def.cases);
      break;
    case HEGEL_SCH_SELF:
      break;
    default:
      break;
  }

  free (s);
}

void
hegel_schema_free (hegel_schema_t s)
{
  hegel__schema_free_raw (s._raw);
}
