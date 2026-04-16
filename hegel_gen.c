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
** Internal fatal-error helper
** ================================================================
**
** Every call site that detects a non-recoverable schema misuse
** (layout mismatch, INLINE_REF type check, nested union-in-case,
** wrong ARRAY_INLINE element kind, ...) goes through this macro
** so there's one place to prepend a prefix, add __FILE__:__LINE__
** later if wanted, or swap in a longjmp-based recovery for a
** "schema-build-should-fail" testing mode. Internal only. */
#define hegel__abort(fmt, ...) do {                                   \
    fprintf (stderr, "hegel: " fmt "\n" __VA_OPT__(,) __VA_ARGS__);   \
    abort ();                                                         \
  } while (0)

/* ================================================================
** Refcount
** ================================================================ */

hegel_schema_t
hegel_schema_ref (hegel_schema_t s)
{
  if (s._raw != NULL) s._raw->refcount ++;
  return (s);
}

/* HEGEL_INLINE_REF check: the referenced schema must be a struct of
** the expected size, otherwise the parent layout would lie about the
** slot it occupies.  Called inline from the macro so the error fires
** at schema-build time, not at first draw. */
hegel_schema_t
hegel__inline_ref_check (size_t declared_size, hegel_schema_t sch)
{
  if (sch._raw == NULL || sch._raw->kind != HEGEL_SCH_STRUCT) {
    hegel__abort ("HEGEL_INLINE_REF: schema must be a HEGEL_STRUCT "
                  "(got kind=%d)",
                  sch._raw != NULL ? (int) sch._raw->kind : -1);
  }
  if (sch._raw->struct_def.size != declared_size) {
    hegel__abort ("HEGEL_INLINE_REF: sizeof(T)=%zu but referenced schema "
                  "has size=%zu",
                  declared_size, sch._raw->struct_def.size);
  }
  return (sch);
}

/* ================================================================
** Layout pass — positional HEGEL_STRUCT
** ================================================================
**
** Given a list of `hegel_schema_t` entries describing ordered fields
** of a struct, compute each entry's byte offset in the parent struct
** using C's layout rules, fix up kind-specific sub-offsets inside
** each schema (array_def.len_offset), and build children + offsets
** arrays.  Size and alignment for each entry are derived from its
** schema kind via `hegel__schema_slot_info`.
**
** Layout rule: each slot starts at
**   offset = align_up(current_end, slot_align)
** and the struct total rounds up to `alignof(T)` at the end.
**
** Arrays occupy two slots in the parent (pointer + int count);
** unions and variants occupy one cluster slot whose internal
** offsets are baked into the schema itself. */

static size_t
hegel__align_up (size_t x, size_t align)
{
  return ((x + align - 1) & ~(align - 1));
}

static void hegel__resolve_self (hegel_schema * node, hegel_schema * target);

/* Kind-dispatch: given a schema, return how many bytes it occupies in
** a parent struct's value memory, and what alignment it needs.  For
** multi-slot kinds (ARRAY, ARRAY_INLINE) this returns the primary
** (pointer) slot; the caller is expected to know the secondary-slot
** dimensions are {sizeof(int), _Alignof(int)} via hardcoded knowledge
** of the kind. */
static void
hegel__schema_slot_info (const hegel_schema * s,
                         size_t * out_size, size_t * out_align)
{
  if (s == NULL) { *out_size = 0; *out_align = 1; return; }

  switch (s->kind) {
    case HEGEL_SCH_INTEGER:
      *out_size  = (size_t) s->integer.width;
      *out_align = (size_t) s->integer.width;
      return;
    case HEGEL_SCH_FLOAT:
      *out_size  = (size_t) s->fp.width;
      *out_align = (size_t) s->fp.width;
      return;
    case HEGEL_SCH_TEXT:
    case HEGEL_SCH_REGEX:
      *out_size  = sizeof (char *);
      *out_align = _Alignof (char *);
      return;
    case HEGEL_SCH_OPTIONAL_PTR:
      *out_size  = sizeof (void *);
      *out_align = _Alignof (void *);
      return;
    case HEGEL_SCH_ARRAY:
    case HEGEL_SCH_ARRAY_INLINE:
      /* Primary slot only: the pointer.  Callers that need the
      ** length-slot dimensions use {sizeof(int), _Alignof(int)}. */
      *out_size  = sizeof (void *);
      *out_align = _Alignof (void *);
      return;
    case HEGEL_SCH_STRUCT:
      *out_size  = s->struct_def.size;
      *out_align = s->struct_def.align > 0 ? s->struct_def.align : 1;
      return;
    case HEGEL_SCH_UNION:
    case HEGEL_SCH_UNION_UNTAGGED:
      *out_size  = s->union_def.cluster_size;
      *out_align = s->union_def.cluster_align > 0 ? s->union_def.cluster_align : 1;
      return;
    case HEGEL_SCH_VARIANT:
      *out_size  = s->variant_def.cluster_size;
      *out_align = s->variant_def.cluster_align > 0 ? s->variant_def.cluster_align : 1;
      return;
    case HEGEL_SCH_ONE_OF_STRUCT:
      *out_size  = sizeof (void *);
      *out_align = _Alignof (void *);
      return;
    case HEGEL_SCH_MAP_INT:
    case HEGEL_SCH_FILTER_INT:
    case HEGEL_SCH_FLAT_MAP_INT:
      *out_size  = sizeof (int);
      *out_align = _Alignof (int);
      return;
    case HEGEL_SCH_MAP_I64:
    case HEGEL_SCH_FILTER_I64:
    case HEGEL_SCH_FLAT_MAP_I64:
      *out_size  = sizeof (int64_t);
      *out_align = _Alignof (int64_t);
      return;
    case HEGEL_SCH_MAP_DOUBLE:
    case HEGEL_SCH_FILTER_DOUBLE:
    case HEGEL_SCH_FLAT_MAP_DOUBLE:
      *out_size  = sizeof (double);
      *out_align = _Alignof (double);
      return;
    case HEGEL_SCH_ONE_OF_SCALAR:
      /* Size/align depend on the cases' shared scalar kind; the
      ** HEGEL_ONE_OF_{INT,I64,DOUBLE} macros know this at expansion
      ** time and fill in slot size directly today.  For kind-dispatch
      ** we infer from the first case's kind. */
      if (s->one_of_scalar_def.n_cases > 0
          && s->one_of_scalar_def.cases[0] != NULL) {
        hegel__schema_slot_info (s->one_of_scalar_def.cases[0],
                                 out_size, out_align);
        return;
      }
      *out_size  = sizeof (int);
      *out_align = _Alignof (int);
      return;
    case HEGEL_SCH_SELF:
      /* Self-refs always occupy a pointer slot in the parent. */
      *out_size  = sizeof (void *);
      *out_align = _Alignof (void *);
      return;
  }
  *out_size = 0;
  *out_align = 1;
}

/* Shift every field offset in a union schema's case schemas by
** `shift` bytes.  Used when the union body starts at a non-zero
** offset relative to the union's own root (e.g. after the int tag).
** The case is a struct-kind schema whose offsets[] array holds the
** body-relative field positions; we just bump each. */
static void
hegel__shift_union_cases (hegel_schema * u, size_t shift)
{
  int i;
  for (i = 0; i < u->union_def.n_cases; i ++) {
    hegel_schema * c = u->union_def.cases[i];
    int j;
    for (j = 0; j < c->struct_def.n_children; j ++)
      c->struct_def.offsets[j] += shift;
  }
}

hegel_schema_t
hegel__struct_build (size_t declared_size, size_t declared_align,
                     hegel_schema_t * entries)
{
  int               n;
  int               i;
  size_t            cur = 0;
  size_t            max_align = 1;
  hegel_schema **   children;
  size_t *          offsets;

  /* Count H_END-terminated entries. */
  for (n = 0; entries[n]._raw != NULL; n ++) {}

  children = (hegel_schema **) malloc ((size_t) n * sizeof (hegel_schema *));
  offsets  = (size_t *)        malloc ((size_t) n * sizeof (size_t));

  for (i = 0; i < n; i ++) {
    hegel_schema * e = entries[i]._raw;
    size_t         slot0_size, slot0_align;
    size_t         slot0_off;

    hegel__schema_slot_info (e, &slot0_size, &slot0_align);

    slot0_off = hegel__align_up (cur, slot0_align);
    cur       = slot0_off + slot0_size;
    if (slot0_align > max_align) max_align = slot0_align;

    children[i] = e;
    offsets[i]  = slot0_off;

    /* Arrays have a second slot: the int count that follows the ptr.
    ** We hardcode {sizeof(int), _Alignof(int)} by kind and write the
    ** computed length-slot offset back into the array schema. */
    if (e->kind == HEGEL_SCH_ARRAY || e->kind == HEGEL_SCH_ARRAY_INLINE) {
      size_t slot1_off = hegel__align_up (cur, _Alignof (int));
      cur = slot1_off + sizeof (int);
      if (_Alignof (int) > max_align) max_align = _Alignof (int);
      if (e->kind == HEGEL_SCH_ARRAY)
        e->array_def.len_offset = slot1_off;
      else
        e->array_inline_def.len_offset = slot1_off;
    }
  }

  /* Round struct size up to its alignment. */
  {
    size_t computed_total = hegel__align_up (cur, max_align);
    if (computed_total != declared_size) {
      hegel__abort ("hegel__struct_build: sizeof(T)=%zu but layout "
                    "computed %zu (max_align=%zu).  Check that your "
                    "HEGEL_STRUCT entries match the struct fields in "
                    "order and type.",
                    declared_size, computed_total, max_align);
    }
    if (declared_align < max_align) {
      hegel__abort ("hegel__struct_build: alignof(T)=%zu but layout "
                    "needs %zu.",
                    declared_align, max_align);
    }
  }

  {
    hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
    s->kind = HEGEL_SCH_STRUCT;
    s->refcount = 1;
    s->struct_def.size       = declared_size;
    s->struct_def.align      = declared_align;
    s->struct_def.n_children = n;
    s->struct_def.children   = children;   /* transfer */
    s->struct_def.offsets    = offsets;    /* transfer */
    hegel__resolve_self (s, s);
    return (hegel_schema_t){s};
  }
}

/* ================================================================
** HEGEL_UNION / HEGEL_UNION_UNTAGGED — positional form
** ================================================================
**
** Each `HEGEL_CASE(...)` expands to an array of layout entries.
** `hegel__union_make` walks those cases, lays out each case's
** fields relative to body base 0, collects the resulting bindings,
** and builds the union schema with placeholder tag_offset = 0.
** The parent `HEGEL_STRUCT` layout pass then:
**   - sets union_def.tag_offset to the real tag slot offset
**   - shifts every binding offset in every case by the body base
**
** Body size = max case size; body align = max case align. */

static hegel_schema *
hegel__lay_case (hegel_schema_t * case_entries,
                 size_t * out_body_size, size_t * out_body_align)
{
  int               nf = 0;
  int               i;
  size_t            cur = 0;
  size_t            max_align = 1;
  hegel_schema *    cs;
  hegel_schema **   children;
  size_t *          offsets;

  while (case_entries[nf]._raw != NULL) nf ++;

  children = (hegel_schema **) malloc ((size_t) nf * sizeof (hegel_schema *));
  offsets  = (size_t *)        malloc ((size_t) nf * sizeof (size_t));

  for (i = 0; i < nf; i ++) {
    hegel_schema * e = case_entries[i]._raw;
    size_t         slot0_size, slot0_align;
    size_t         slot0_off;

    /* Nested unions/variants inside union cases: not supported. */
    if (e->kind == HEGEL_SCH_UNION || e->kind == HEGEL_SCH_UNION_UNTAGGED
        || e->kind == HEGEL_SCH_VARIANT) {
      hegel__abort ("hegel__lay_case: nested union/variant in case "
                    "not supported (kind=%d)", (int) e->kind);
    }

    hegel__schema_slot_info (e, &slot0_size, &slot0_align);
    slot0_off = hegel__align_up (cur, slot0_align);
    cur       = slot0_off + slot0_size;
    if (slot0_align > max_align) max_align = slot0_align;

    children[i] = e;
    offsets[i]  = slot0_off;

    if (e->kind == HEGEL_SCH_ARRAY || e->kind == HEGEL_SCH_ARRAY_INLINE) {
      size_t slot1_off = hegel__align_up (cur, _Alignof (int));
      cur = slot1_off + sizeof (int);
      if (_Alignof (int) > max_align) max_align = _Alignof (int);
      if (e->kind == HEGEL_SCH_ARRAY)
        e->array_def.len_offset = slot1_off;
      else
        e->array_inline_def.len_offset = slot1_off;
    }
  }

  *out_body_size  = hegel__align_up (cur, max_align);
  *out_body_align = max_align;

  cs = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  cs->kind = HEGEL_SCH_STRUCT;
  cs->refcount = 1;
  cs->struct_def.size       = *out_body_size;
  cs->struct_def.align      = max_align;
  cs->struct_def.n_children = nf;
  cs->struct_def.children   = children;
  cs->struct_def.offsets    = offsets;
  return (cs);
}

hegel_schema_t
hegel__union_make (hegel_schema_kind kind,
                   hegel_schema_t ** case_list)
{
  int               n_cases = 0;
  int               i;
  hegel_schema *    u;
  size_t            body_size = 0;
  size_t            body_align = 1;

  while (case_list[n_cases] != NULL) n_cases ++;

  u = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  u->kind = kind;
  u->refcount = 1;
  u->union_def.n_cases = n_cases;
  u->union_def.cases = (hegel_schema **) malloc (
      (size_t) n_cases * sizeof (hegel_schema *));

  for (i = 0; i < n_cases; i ++) {
    size_t         case_size;
    size_t         case_align;
    hegel_schema * case_schema;

    case_schema = hegel__lay_case (case_list[i], &case_size, &case_align);

    if (case_size > body_size)   body_size = case_size;
    if (case_align > body_align) body_align = case_align;

    u->union_def.cases[i] = case_schema;
  }

  /* Compute self-relative internal offsets: tag at 0 (if tagged),
  ** body starts at align_up(sizeof(int), body_align) for tagged or
  ** at 0 for untagged.  Shift every case field offset by the body
  ** position so case fields land at the right place relative to the
  ** union's own base pointer.  Cluster size = body_offset + body_size,
  ** aligned up to the union's overall alignment. */
  {
    size_t tag_off    = 0;
    size_t body_off;
    size_t cluster_align;
    size_t cluster_size;

    if (kind == HEGEL_SCH_UNION) {
      cluster_align = _Alignof (int) > body_align ? _Alignof (int) : body_align;
      body_off = hegel__align_up (sizeof (int), body_align);
    } else {
      cluster_align = body_align;
      body_off = 0;
    }
    cluster_size = hegel__align_up (body_off + body_size, cluster_align);

    u->union_def.tag_offset    = tag_off;
    u->union_def.cluster_size  = cluster_size;
    u->union_def.cluster_align = cluster_align;
    hegel__shift_union_cases (u, body_off);
  }

  return (hegel_schema_t){u};
}

/* HEGEL_VARIANT positional form.  Takes an H_END-terminated list of
** struct schemas; builds the variant_def with placeholder tag_offset
** and ptr_offset.  The parent HEGEL_STRUCT layout pass fills those
** in with the real slot offsets. */

hegel_schema_t
hegel__variant_make (hegel_schema_t * case_list)
{
  int              n_cases = 0;
  int              i;
  hegel_schema *   v;
  size_t           tag_off;
  size_t           ptr_off;
  size_t           cluster_align;
  size_t           cluster_size;

  while (case_list[n_cases]._raw != NULL) n_cases ++;

  v = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  v->kind = HEGEL_SCH_VARIANT;
  v->refcount = 1;
  v->variant_def.n_cases = n_cases;
  v->variant_def.cases = (hegel_schema **) malloc (
      (size_t) n_cases * sizeof (hegel_schema *));
  for (i = 0; i < n_cases; i ++)
    v->variant_def.cases[i] = case_list[i]._raw;

  /* Self-relative layout: int tag at 0, void* at align_up(4, ptrof). */
  tag_off = 0;
  ptr_off = hegel__align_up (sizeof (int), _Alignof (void *));
  cluster_align = _Alignof (void *) > _Alignof (int)
                  ? _Alignof (void *) : _Alignof (int);
  cluster_size = hegel__align_up (ptr_off + sizeof (void *), cluster_align);

  v->variant_def.tag_offset    = tag_off;
  v->variant_def.ptr_offset    = ptr_off;
  v->variant_def.cluster_size  = cluster_size;
  v->variant_def.cluster_align = cluster_align;

  return (hegel_schema_t){v};
}

/* ================================================================
** Schema constructors — integers
** ================================================================
**
** Internal helper: build an integer schema node with explicit params. */

static hegel_schema *
hegel__integer (int width, int is_signed,
                int64_t min_s, int64_t max_s,
                uint64_t min_u, uint64_t max_u)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_INTEGER;
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

hegel_schema_t hegel_schema_i8 (void) {
  return (hegel_schema_t){hegel__integer (1, 1, INT8_MIN, INT8_MAX, 0, 0)};
}
hegel_schema_t hegel_schema_i8_range (int64_t lo, int64_t hi) {
  return (hegel_schema_t){hegel__integer (1, 1, lo, hi, 0, 0)};
}
hegel_schema_t hegel_schema_i16 (void) {
  return (hegel_schema_t){hegel__integer (2, 1, INT16_MIN, INT16_MAX, 0, 0)};
}
hegel_schema_t hegel_schema_i16_range (int64_t lo, int64_t hi) {
  return (hegel_schema_t){hegel__integer (2, 1, lo, hi, 0, 0)};
}
hegel_schema_t hegel_schema_i32 (void) {
  return (hegel_schema_t){hegel__integer (4, 1, INT32_MIN, INT32_MAX, 0, 0)};
}
hegel_schema_t hegel_schema_i32_range (int64_t lo, int64_t hi) {
  return (hegel_schema_t){hegel__integer (4, 1, lo, hi, 0, 0)};
}
hegel_schema_t hegel_schema_i64 (void) {
  return (hegel_schema_t){hegel__integer (8, 1, INT64_MIN, INT64_MAX, 0, 0)};
}
hegel_schema_t hegel_schema_i64_range (int64_t lo, int64_t hi) {
  return (hegel_schema_t){hegel__integer (8, 1, lo, hi, 0, 0)};
}

/* platform-width signed */
hegel_schema_t hegel_schema_int (void) {
  return (hegel_schema_t){hegel__integer ((int) sizeof (int), 1, INT_MIN, INT_MAX, 0, 0)};
}
hegel_schema_t hegel_schema_int_range (int64_t lo, int64_t hi) {
  return (hegel_schema_t){hegel__integer ((int) sizeof (int), 1, lo, hi, 0, 0)};
}
hegel_schema_t hegel_schema_long (void) {
  return (hegel_schema_t){hegel__integer ((int) sizeof (long), 1, LONG_MIN, LONG_MAX, 0, 0)};
}
hegel_schema_t hegel_schema_long_range (int64_t lo, int64_t hi) {
  return (hegel_schema_t){hegel__integer ((int) sizeof (long), 1, lo, hi, 0, 0)};
}

/* ---- unsigned constructors ---- */

hegel_schema_t hegel_schema_u8 (void) {
  return (hegel_schema_t){hegel__integer (1, 0, 0, 0, 0, UINT8_MAX)};
}
hegel_schema_t hegel_schema_u8_range (uint64_t lo, uint64_t hi) {
  return (hegel_schema_t){hegel__integer (1, 0, 0, 0, lo, hi)};
}
hegel_schema_t hegel_schema_u16 (void) {
  return (hegel_schema_t){hegel__integer (2, 0, 0, 0, 0, UINT16_MAX)};
}
hegel_schema_t hegel_schema_u16_range (uint64_t lo, uint64_t hi) {
  return (hegel_schema_t){hegel__integer (2, 0, 0, 0, lo, hi)};
}
hegel_schema_t hegel_schema_u32 (void) {
  return (hegel_schema_t){hegel__integer (4, 0, 0, 0, 0, UINT32_MAX)};
}
hegel_schema_t hegel_schema_u32_range (uint64_t lo, uint64_t hi) {
  return (hegel_schema_t){hegel__integer (4, 0, 0, 0, lo, hi)};
}
hegel_schema_t hegel_schema_u64 (void) {
  return (hegel_schema_t){hegel__integer (8, 0, 0, 0, 0, UINT64_MAX)};
}
hegel_schema_t hegel_schema_u64_range (uint64_t lo, uint64_t hi) {
  return (hegel_schema_t){hegel__integer (8, 0, 0, 0, lo, hi)};
}

/* ================================================================
** Schema constructors — floats
** ================================================================ */

static hegel_schema *
hegel__fp (int width, double min_val, double max_val)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_FLOAT;
  s->refcount = 1;
  s->fp.width = width;
  s->fp.min = min_val;
  s->fp.max = max_val;
  return (s);
}

hegel_schema_t hegel_schema_float (void) {
  return (hegel_schema_t){hegel__fp (4, -FLT_MAX, FLT_MAX)};
}
hegel_schema_t hegel_schema_float_range (double lo, double hi) {
  return (hegel_schema_t){hegel__fp (4, lo, hi)};
}
hegel_schema_t hegel_schema_double (void) {
  return (hegel_schema_t){hegel__fp (8, -DBL_MAX, DBL_MAX)};
}
hegel_schema_t hegel_schema_double_range (double lo, double hi) {
  return (hegel_schema_t){hegel__fp (8, lo, hi)};
}


/* ================================================================
** Schema constructors — text, optional, array, struct, self
** ================================================================ */

hegel_schema_t
hegel_schema_text (int min_len, int max_len)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_TEXT;
  s->refcount = 1;
  s->text_range.min_len = min_len;
  s->text_range.max_len = max_len;
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_optional_ptr (hegel_schema_t inner)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_OPTIONAL_PTR;
  s->refcount = 1;
  s->optional_ptr.inner = inner._raw;  /* transfer ownership, unwrap */
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_array (size_t len_offset, hegel_schema_t elem,
                    int min_len, int max_len)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_ARRAY;
  s->refcount = 1;
  s->array_def.len_offset = len_offset;
  s->array_def.elem = elem._raw;  /* transfer ownership, unwrap */
  s->array_def.min_len = min_len;
  s->array_def.max_len = max_len;
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_array_inline (size_t len_offset, hegel_schema_t elem,
                           size_t elem_size, int min_len, int max_len)
{
  hegel_schema * e = elem._raw;
  /* Runtime check: ARRAY_INLINE elements must be STRUCT or UNION.
  ** Passing a VARIANT or scalar schema here would be silently wrong
  ** (VARIANT stores a pointer, which defeats "inline"; scalars should
  ** use HEGEL_ARRAY). Fail loudly at setup time. */
  if (e->kind != HEGEL_SCH_STRUCT
      && e->kind != HEGEL_SCH_UNION
      && e->kind != HEGEL_SCH_UNION_UNTAGGED) {
    hegel__abort ("hegel_schema_array_inline: elem kind %d not supported "
                  "(must be STRUCT, UNION, or UNION_UNTAGGED)",
                  (int) e->kind);
  }

  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_ARRAY_INLINE;
  s->refcount = 1;
  s->array_inline_def.len_offset = len_offset;
  s->array_inline_def.elem = e;  /* transfer ownership */
  s->array_inline_def.elem_size = elem_size;
  s->array_inline_def.min_len = min_len;
  s->array_inline_def.max_len = max_len;
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

/* One-of-struct: pick one of several STRUCT schemas.  No parent
** offsets — this is a standalone pointer-producing generator. */
hegel_schema_t
hegel_schema_one_of_struct_v (hegel_schema_t * case_list)
{
  int                 n;
  hegel_schema *      s;

  /* case_list is H_END-terminated */
  for (n = 0; case_list[n]._raw != NULL; n ++) {}

  s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_ONE_OF_STRUCT;
  s->refcount = 1;
  /* Reuse the variant_def union member — offsets unused. */
  s->variant_def.tag_offset = (size_t) -1;
  s->variant_def.ptr_offset = (size_t) -1;
  s->variant_def.n_cases = n;
  s->variant_def.cases = (hegel_schema **) malloc (
      (size_t) n * sizeof (hegel_schema *));
  for (int i = 0; i < n; i ++)
    s->variant_def.cases[i] = case_list[i]._raw;  /* unwrap + transfer */
  return (hegel_schema_t){s};
}

/* ---- Functional combinators: map / filter / flat_map (int) ---- */

hegel_schema_t
hegel_schema_map_int (hegel_schema_t source,
                      int (*fn)(int, void *), void *ctx)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_MAP_INT;
  s->refcount = 1;
  s->map_int_def.source = source._raw;   /* ownership transferred */
  s->map_int_def.fn = fn;
  s->map_int_def.ctx = ctx;
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_filter_int (hegel_schema_t source,
                         int (*pred)(int, void *), void *ctx)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_FILTER_INT;
  s->refcount = 1;
  s->filter_int_def.source = source._raw;
  s->filter_int_def.pred = pred;
  s->filter_int_def.ctx = ctx;
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_flat_map_int (hegel_schema_t source,
                           hegel_schema_t (*fn)(int, void *), void *ctx)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_FLAT_MAP_INT;
  s->refcount = 1;
  s->flat_map_int_def.source = source._raw;
  s->flat_map_int_def.fn = fn;
  s->flat_map_int_def.ctx = ctx;
  return (hegel_schema_t){s};
}

/* ---- i64 combinators (same shape as int, different callback types) ---- */

hegel_schema_t
hegel_schema_map_i64 (hegel_schema_t source,
                      int64_t (*fn)(int64_t, void *), void *ctx)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_MAP_I64;
  s->refcount = 1;
  s->map_i64_def.source = source._raw;
  s->map_i64_def.fn = fn;
  s->map_i64_def.ctx = ctx;
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_filter_i64 (hegel_schema_t source,
                         int (*pred)(int64_t, void *), void *ctx)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_FILTER_I64;
  s->refcount = 1;
  s->filter_i64_def.source = source._raw;
  s->filter_i64_def.pred = pred;
  s->filter_i64_def.ctx = ctx;
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_flat_map_i64 (hegel_schema_t source,
                           hegel_schema_t (*fn)(int64_t, void *), void *ctx)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_FLAT_MAP_I64;
  s->refcount = 1;
  s->flat_map_i64_def.source = source._raw;
  s->flat_map_i64_def.fn = fn;
  s->flat_map_i64_def.ctx = ctx;
  return (hegel_schema_t){s};
}

/* ---- double combinators ---- */

hegel_schema_t
hegel_schema_map_double (hegel_schema_t source,
                         double (*fn)(double, void *), void *ctx)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_MAP_DOUBLE;
  s->refcount = 1;
  s->map_double_def.source = source._raw;
  s->map_double_def.fn = fn;
  s->map_double_def.ctx = ctx;
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_filter_double (hegel_schema_t source,
                            int (*pred)(double, void *), void *ctx)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_FILTER_DOUBLE;
  s->refcount = 1;
  s->filter_double_def.source = source._raw;
  s->filter_double_def.pred = pred;
  s->filter_double_def.ctx = ctx;
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_flat_map_double (hegel_schema_t source,
                              hegel_schema_t (*fn)(double, void *), void *ctx)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_FLAT_MAP_DOUBLE;
  s->refcount = 1;
  s->flat_map_double_def.source = source._raw;
  s->flat_map_double_def.fn = fn;
  s->flat_map_double_def.ctx = ctx;
  return (hegel_schema_t){s};
}

/* ---- One-of for scalar schemas ---- */

hegel_schema_t
hegel_schema_one_of_scalar_v (hegel_schema_t * case_list)
{
  int                 n;
  hegel_schema *      s;

  for (n = 0; case_list[n]._raw != NULL; n ++) {}

  s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_ONE_OF_SCALAR;
  s->refcount = 1;
  s->one_of_scalar_def.n_cases = n;
  s->one_of_scalar_def.cases = (hegel_schema **) malloc (
      (size_t) n * sizeof (hegel_schema *));
  for (int i = 0; i < n; i ++)
    s->one_of_scalar_def.cases[i] = case_list[i]._raw;
  return (hegel_schema_t){s};
}

/* ---- Regex-generated text ---- */

hegel_schema_t
hegel_schema_regex (const char * pattern, int capacity)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_REGEX;
  s->refcount = 1;
  {
    size_t plen = strlen (pattern);
    s->regex_def.pattern = (char *) malloc (plen + 1);
    memcpy (s->regex_def.pattern, pattern, plen + 1);
  }
  s->regex_def.capacity = capacity;
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
      for (int i = 0; i < node->struct_def.n_children; i ++)
        hegel__resolve_self (node->struct_def.children[i], target);
      break;
    case HEGEL_SCH_UNION:
    case HEGEL_SCH_UNION_UNTAGGED:
      for (int i = 0; i < node->union_def.n_cases; i ++)
        hegel__resolve_self (node->union_def.cases[i], target);
      break;
    case HEGEL_SCH_VARIANT:
    case HEGEL_SCH_ONE_OF_STRUCT:
      for (int i = 0; i < node->variant_def.n_cases; i ++)
        hegel__resolve_self (node->variant_def.cases[i], target);
      break;
    case HEGEL_SCH_MAP_INT:
      hegel__resolve_self (node->map_int_def.source, target);
      break;
    case HEGEL_SCH_FILTER_INT:
      hegel__resolve_self (node->filter_int_def.source, target);
      break;
    case HEGEL_SCH_FLAT_MAP_INT:
      hegel__resolve_self (node->flat_map_int_def.source, target);
      break;
    case HEGEL_SCH_MAP_I64:
      hegel__resolve_self (node->map_i64_def.source, target);
      break;
    case HEGEL_SCH_FILTER_I64:
      hegel__resolve_self (node->filter_i64_def.source, target);
      break;
    case HEGEL_SCH_FLAT_MAP_I64:
      hegel__resolve_self (node->flat_map_i64_def.source, target);
      break;
    case HEGEL_SCH_MAP_DOUBLE:
      hegel__resolve_self (node->map_double_def.source, target);
      break;
    case HEGEL_SCH_FILTER_DOUBLE:
      hegel__resolve_self (node->filter_double_def.source, target);
      break;
    case HEGEL_SCH_FLAT_MAP_DOUBLE:
      hegel__resolve_self (node->flat_map_double_def.source, target);
      break;
    case HEGEL_SCH_ONE_OF_SCALAR:
      for (int i = 0; i < node->one_of_scalar_def.n_cases; i ++)
        hegel__resolve_self (node->one_of_scalar_def.cases[i], target);
      break;
    default:
      break;
  }
}

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

/* Read the i-th field's value-memory offset for a STRUCT shape.
** Reads through the shape's schema backpointer, which every STRUCT
** shape now has.  The synthetic variant-body case was retired when
** union cases became real struct schemas. */
static size_t
hegel__shape_field_offset (hegel_shape * s, int i)
{
  return (s->schema->struct_def.offsets[i]);
}

/* Offset-based lookup.  Given a STRUCT shape and a field offset
** (typically `offsetof(T, field)`), return the shape node
** corresponding to that field's drawn value.
**
** With HEGEL_INLINE, a top-level binding's offset may name an inline
** sub-struct, and the caller may request an offset pointing *inside*
** that sub-struct.  Two recursion passes handle this:
**   1. Exact match: if the requested offset matches a binding
**      exactly, return that field.  If that field is itself a
**      struct shape (inline sub-struct), descend with sub-offset 0
**      so HEGEL_SHAPE_GET(sh, T, sub.leaf) where leaf is the first
**      field of `sub` still returns the leaf scalar, not the
**      wrapper struct shape.
**   2. Interior match: for each struct-kind child, recurse with
**      (offset − child_offset).  This only returns non-NULL if the
**      relative offset hits a real binding in the sub-struct, so
**      it's self-limiting.  Non-overlapping binding ranges mean at
**      most one child will return a non-NULL match. */
hegel_shape *
hegel_shape_get_offset (hegel_shape * s, size_t offset)
{
  if (s == NULL || s->kind != HEGEL_SHAPE_STRUCT) return (NULL);
  if (s->struct_shape.fields == NULL) return (NULL);
  {
    int i;

    /* Pass 1: exact match, descending into inline sub-structs. */
    for (i = 0; i < s->struct_shape.n_fields; i ++) {
      if (hegel__shape_field_offset (s, i) == offset) {
        hegel_shape * child = s->struct_shape.fields[i];
        if (child != NULL && child->kind == HEGEL_SHAPE_STRUCT
            && child->struct_shape.fields != NULL) {
          hegel_shape * deeper = hegel_shape_get_offset (child, 0);
          if (deeper != NULL) return (deeper);
        }
        return (child);
      }
    }

    /* Pass 2: the offset may fall inside an inline sub-struct. */
    for (i = 0; i < s->struct_shape.n_fields; i ++) {
      hegel_shape * child = s->struct_shape.fields[i];
      size_t child_offset = hegel__shape_field_offset (s, i);
      if (child != NULL && child->kind == HEGEL_SHAPE_STRUCT
          && child->struct_shape.fields != NULL
          && offset > child_offset) {
        hegel_shape * deeper =
            hegel_shape_get_offset (child, offset - child_offset);
        if (deeper != NULL) return (deeper);
      }
    }
  }
  return (NULL);
}

/* ================================================================
** Draw
** ================================================================ */

static hegel_shape *
hegel__draw_struct (hegel_testcase * tc, hegel_schema * gen, void ** out,
                    int depth);
static hegel_shape *
hegel__draw_field (hegel_testcase * tc, hegel_schema * gen,
                   void * parent, size_t offset, int depth);
static hegel_shape *
hegel__draw_struct_into_slot (hegel_testcase * tc, hegel_schema * gen,
                              void * slot, int depth);

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
      shape->schema = actual;
      shape->owned = buf;
      return (shape);
    }

    case HEGEL_SCH_INTEGER: {
      void * p = calloc (1, (size_t) actual->integer.width);
      hegel__draw_integer_into (tc, actual, p);
      *out = p;
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_SCALAR;
      shape->schema = actual;
      shape->owned = p;
      return (shape);
    }

    case HEGEL_SCH_ONE_OF_STRUCT: {
      /* Pick a variant, allocate the chosen struct, return the ptr.
      ** No parent writes — this is the whole point of this schema kind. */
      int nc = actual->variant_def.n_cases;
      hegel_schema * chosen;
      void * child;
      hegel_shape * inner;

      hegel_start_span (tc, HEGEL_SPAN_ONE_OF);
      int tag = hegel_draw_int (tc, 0, nc - 1);
      chosen = actual->variant_def.cases[tag];

      hegel_start_span (tc, HEGEL_SPAN_ENUM_VARIANT);
      inner = hegel__draw_struct (tc, chosen, &child, depth - 1);
      hegel_stop_span (tc, 0);
      hegel_stop_span (tc, 0);

      *out = child;
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_VARIANT;
      shape->schema = actual;
      shape->variant_shape.tag = tag;
      shape->variant_shape.inner = inner;
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
    case HEGEL_SCH_INTEGER:       return ((size_t) elem->integer.width);
    case HEGEL_SCH_FLOAT:         return ((size_t) elem->fp.width);
    case HEGEL_SCH_TEXT:          return (sizeof (char *));
    case HEGEL_SCH_STRUCT:        return (sizeof (void *));
    case HEGEL_SCH_ONE_OF_STRUCT: return (sizeof (void *));
    case HEGEL_SCH_SELF:
      if (elem->self_ref.target != NULL)
        return (sizeof (void *));
      return (sizeof (int));
    default:                      return (sizeof (int));
  }
}

/* ---- Allocate struct_shape.fields.  Just an array of shape pointers.
** Field offsets used to sit in a trailer here, but every struct shape
** now carries a schema backpointer that has the offsets in its
** struct_def.offsets array — so the trailer is redundant. */

static void
hegel__alloc_struct_fields (hegel_shape * shape, int n)
{
  shape->struct_shape.n_fields = n;
  shape->struct_shape.fields = (hegel_shape **) calloc (
      (size_t) n, sizeof (hegel_shape *));
}

/* ---- Draw a STRUCT schema into a pre-allocated slot ----
** Used for inline-by-value sub-structs: the storage for the struct's
** fields is owned by the caller (parent struct block, or an
** HEGEL_ARRAY_INLINE element slot), not by this call.  The returned
** shape has `owned = NULL` to mark that shape_free must not touch
** the slot memory. */

static hegel_shape *
hegel__draw_struct_into_slot (hegel_testcase * tc, hegel_schema * gen,
                              void * slot, int depth)
{
  int                 nf = gen->struct_def.n_children;
  hegel_shape *       sh;
  int                 f;

  sh = (hegel_shape *) calloc (1, sizeof (hegel_shape));
  sh->kind = HEGEL_SHAPE_STRUCT;
  sh->schema = gen;
  sh->owned = NULL;
  hegel__alloc_struct_fields (sh, nf);

  hegel_start_span (tc, HEGEL_SPAN_TUPLE);
  for (f = 0; f < nf; f ++) {
    size_t          field_off = gen->struct_def.offsets[f];
    hegel_schema *  field_sch = gen->struct_def.children[f];
    sh->struct_shape.fields[f] =
        hegel__draw_field (tc, field_sch, slot, field_off, depth - 1);
  }
  hegel_stop_span (tc, 0);

  return (sh);
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

  n = gen->struct_def.n_children;
  ptr = calloc (1, gen->struct_def.size);

  shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
  shape->kind = HEGEL_SHAPE_STRUCT;
  shape->schema = gen;
  shape->owned = ptr;
  hegel__alloc_struct_fields (shape, n);

  hegel_start_span (tc, HEGEL_SPAN_TUPLE);
  for (i = 0; i < n; i ++) {
    size_t          field_off = gen->struct_def.offsets[i];
    hegel_schema *  field_sch = gen->struct_def.children[i];
    shape->struct_shape.fields[i] =
        hegel__draw_field (tc, field_sch, ptr, field_off, depth);
  }
  hegel_stop_span (tc, 0);

  *out = ptr;
  return (shape);
}

/* ---- Draw field into parent struct ---- */

static hegel_shape *
hegel__draw_field (hegel_testcase * tc, hegel_schema * gen,
                   void * parent, size_t offset, int depth)
{
  hegel_shape *       shape;
  void *              dst = (char *) parent + offset;

  switch (gen->kind) {

    case HEGEL_SCH_INTEGER: {
      hegel__draw_integer_into (tc, gen, dst);
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_SCALAR;
      shape->schema = gen;
      return (shape);
    }

    case HEGEL_SCH_FLOAT: {
      hegel__draw_fp_into (tc, gen, dst);
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_SCALAR;
      shape->schema = gen;
      return (shape);
    }

    case HEGEL_SCH_TEXT: {
      char * buf = hegel__draw_text (tc,
          gen->text_range.min_len, gen->text_range.max_len);
      *(char **) dst = buf;
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_TEXT;
      shape->schema = gen;
      shape->owned = buf;
      return (shape);
    }

    case HEGEL_SCH_OPTIONAL_PTR: {
      int present;
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_OPTIONAL;
      shape->schema = gen;

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
          *(void **) dst = child;
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
      shape->schema = gen;

      hegel_start_span (tc, HEGEL_SPAN_LIST);
      n = hegel_draw_int (tc, gen->array_def.min_len,
                              gen->array_def.max_len);
      arr = calloc ((size_t) n + 1, esz);

      shape->array_shape.len = n;
      shape->array_shape.elems = (hegel_shape **) calloc (
          (size_t) n + 1, sizeof (hegel_shape *));
      shape->owned = arr;

      *(void **) dst = arr;
      *(int *) ((char *) parent + gen->array_def.len_offset) = n;

      for (int i = 0; i < n; i ++) {
        hegel_start_span (tc, HEGEL_SPAN_LIST_ELEMENT);

        if (elem->kind == HEGEL_SCH_INTEGER) {
          hegel__draw_integer_into (tc, elem,
              (char *) arr + (size_t) i * esz);
          shape->array_shape.elems[i] =
              (hegel_shape *) calloc (1, sizeof (hegel_shape));
          shape->array_shape.elems[i]->kind = HEGEL_SHAPE_SCALAR;
          shape->array_shape.elems[i]->schema = elem;

        } else if (elem->kind == HEGEL_SCH_FLOAT) {
          hegel__draw_fp_into (tc, elem,
              (char *) arr + (size_t) i * esz);
          shape->array_shape.elems[i] =
              (hegel_shape *) calloc (1, sizeof (hegel_shape));
          shape->array_shape.elems[i]->kind = HEGEL_SHAPE_SCALAR;
          shape->array_shape.elems[i]->schema = elem;

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

        } else if (elem->kind == HEGEL_SCH_ONE_OF_STRUCT) {
          /* Pick a struct schema per element, allocate, store ptr. */
          void * child;
          shape->array_shape.elems[i] =
              hegel__draw_alloc (tc, elem, &child, depth);
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
      shape->schema = gen;

      hegel_start_span (tc, HEGEL_SPAN_LIST);
      n = hegel_draw_int (tc, gen->array_inline_def.min_len,
                              gen->array_inline_def.max_len);
      arr = calloc ((size_t) n + 1, esz);

      shape->array_shape.len = n;
      shape->array_shape.elems = (hegel_shape **) calloc (
          (size_t) n + 1, sizeof (hegel_shape *));
      shape->owned = arr;

      *(void **) dst = arr;
      *(int *) ((char *) parent + gen->array_inline_def.len_offset) = n;

      for (int i = 0; i < n; i ++) {
        void * slot = (char *) arr + (size_t) i * esz;
        hegel_start_span (tc, HEGEL_SPAN_LIST_ELEMENT);

        /* Draw fields of elem schema into the slot (as if slot
        ** is the parent struct for the element's fields). */
        if (elem->kind == HEGEL_SCH_STRUCT) {
          /* Inline struct: draw each field into slot.  Shared with
          ** the HEGEL_INLINE field path via hegel__draw_struct_into_slot. */
          shape->array_shape.elems[i] =
              hegel__draw_struct_into_slot (tc, elem, slot, depth);

        } else if (elem->kind == HEGEL_SCH_UNION
                || elem->kind == HEGEL_SCH_UNION_UNTAGGED) {
          /* Inline union element — draw it into slot.  Offset 0
          ** because the union's internal offsets are relative to
          ** the slot start (just like top-level fields). */
          shape->array_shape.elems[i] =
              hegel__draw_field (tc, elem, slot, 0, depth - 1);

        } else {
          /* Scalar element in inline array — shouldn't happen
          ** (use HEGEL_ARRAY for scalar arrays), but handle it. */
          shape->array_shape.elems[i] =
              hegel__draw_field (tc, elem, slot, 0, depth - 1);
        }

        hegel_stop_span (tc, 0);
      }

      hegel_stop_span (tc, 0);
      return (shape);
    }

    case HEGEL_SCH_UNION:
    case HEGEL_SCH_UNION_UNTAGGED: {
      int              tag;
      int              nc;
      hegel_schema *   chosen;
      /* Union internal offsets (tag_offset, case field offsets) are
      ** self-relative.  `dst = parent + offset` is the union's own
      ** base pointer; the case writes happen at dst + internal. */

      nc = gen->union_def.n_cases;

      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_VARIANT;
      shape->schema = gen;

      hegel_start_span (tc, HEGEL_SPAN_ONE_OF);
      tag = hegel_draw_int (tc, 0, nc - 1);
      shape->variant_shape.tag = tag;

      /* Write tag at union-base + tag_offset (if tagged). */
      if (gen->kind == HEGEL_SCH_UNION)
        *(int *) ((char *) dst + gen->union_def.tag_offset) = tag;

      /* Draw the chosen case's fields into the union base.  Under
      ** option A, each case is a real struct schema, so we can
      ** delegate to the shared helper.  `depth + 1` is passed so
      ** the inner hegel__draw_field calls see `depth`, matching
      ** the original manual loop's behavior. */
      chosen = gen->union_def.cases[tag];
      hegel_start_span (tc, HEGEL_SPAN_ENUM_VARIANT);
      shape->variant_shape.inner =
          hegel__draw_struct_into_slot (tc, chosen, dst, depth + 1);

      hegel_stop_span (tc, 0);
      hegel_stop_span (tc, 0);
      return (shape);
    }

    case HEGEL_SCH_VARIANT: {
      int             tag;
      int             nc;
      hegel_schema *  chosen;
      /* Variant internal offsets are self-relative.  dst is the
      ** variant's own base (parent + binding offset). */

      nc = gen->variant_def.n_cases;

      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_VARIANT;
      shape->schema = gen;

      hegel_start_span (tc, HEGEL_SPAN_ONE_OF);
      tag = hegel_draw_int (tc, 0, nc - 1);
      shape->variant_shape.tag = tag;

      /* Write tag at variant-base + tag_offset. */
      *(int *) ((char *) dst + gen->variant_def.tag_offset) = tag;

      /* Allocate and draw the chosen variant struct. */
      chosen = gen->variant_def.cases[tag];
      hegel_start_span (tc, HEGEL_SPAN_ENUM_VARIANT);
      {
        void * child;
        shape->variant_shape.inner =
            hegel__draw_struct (tc, chosen, &child, depth - 1);
        *(void **) ((char *) dst + gen->variant_def.ptr_offset) = child;
      }
      hegel_stop_span (tc, 0);

      hegel_stop_span (tc, 0);
      return (shape);
    }

    case HEGEL_SCH_MAP_INT: {
      /* Draw source into a temp int, apply fn, write result at dst. */
      hegel_schema * src = gen->map_int_def.source;
      int raw;
      hegel__draw_integer_into (tc, src, &raw);
      int mapped = gen->map_int_def.fn (raw, gen->map_int_def.ctx);
      *(int *) dst = mapped;
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_SCALAR;
      shape->schema = gen;
      return (shape);
    }

    case HEGEL_SCH_FILTER_INT: {
      /* Draw source, call pred, assume(0) to discard if pred fails.
      ** hegel_assume internally calls the testcase's assume which
      ** triggers a filter_too_much health check after 50 discards —
      ** use filter sparingly for narrow predicates. */
      hegel_schema * src = gen->filter_int_def.source;
      int raw;
      hegel__draw_integer_into (tc, src, &raw);
      if (! gen->filter_int_def.pred (raw, gen->filter_int_def.ctx)) {
        hegel_assume (tc, 0);   /* discards the whole test case */
      }
      *(int *) dst = raw;
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_SCALAR;
      shape->schema = gen;
      return (shape);
    }

    case HEGEL_SCH_FLAT_MAP_INT: {
      /* Draw source, pass to callback to get a fresh schema,
      ** draw from that schema (must be INTEGER), write result,
      ** free the temporary schema. */
      hegel_schema * src = gen->flat_map_int_def.source;
      int raw;
      hegel__draw_integer_into (tc, src, &raw);
      hegel_schema_t next = gen->flat_map_int_def.fn (raw,
          gen->flat_map_int_def.ctx);
      if (next._raw != NULL
          && next._raw->kind == HEGEL_SCH_INTEGER) {
        int result;
        hegel__draw_integer_into (tc, next._raw, &result);
        *(int *) dst = result;
      } else {
        /* Callback returned a non-INTEGER schema or NULL — this is
        ** a user error but we fail gracefully by writing 0. */
        *(int *) dst = 0;
      }
      /* Free the temporary schema returned by the callback. */
      hegel_schema_free (next);
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_SCALAR;
      shape->schema = gen;
      return (shape);
    }

    case HEGEL_SCH_MAP_I64: {
      hegel_schema * src = gen->map_i64_def.source;
      int64_t raw;
      hegel__draw_integer_into (tc, src, &raw);
      int64_t mapped = gen->map_i64_def.fn (raw, gen->map_i64_def.ctx);
      *(int64_t *) dst = mapped;
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_SCALAR;
      shape->schema = gen;
      return (shape);
    }

    case HEGEL_SCH_FILTER_I64: {
      hegel_schema * src = gen->filter_i64_def.source;
      int64_t raw;
      hegel__draw_integer_into (tc, src, &raw);
      if (! gen->filter_i64_def.pred (raw, gen->filter_i64_def.ctx))
        hegel_assume (tc, 0);
      *(int64_t *) dst = raw;
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_SCALAR;
      shape->schema = gen;
      return (shape);
    }

    case HEGEL_SCH_FLAT_MAP_I64: {
      hegel_schema * src = gen->flat_map_i64_def.source;
      int64_t raw;
      hegel__draw_integer_into (tc, src, &raw);
      hegel_schema_t next = gen->flat_map_i64_def.fn (raw,
          gen->flat_map_i64_def.ctx);
      if (next._raw != NULL
          && next._raw->kind == HEGEL_SCH_INTEGER) {
        int64_t result;
        hegel__draw_integer_into (tc, next._raw, &result);
        *(int64_t *) dst = result;
      } else {
        *(int64_t *) dst = 0;
      }
      hegel_schema_free (next);
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_SCALAR;
      shape->schema = gen;
      return (shape);
    }

    case HEGEL_SCH_MAP_DOUBLE: {
      hegel_schema * src = gen->map_double_def.source;
      double raw;
      hegel__draw_fp_into (tc, src, &raw);
      double mapped = gen->map_double_def.fn (raw, gen->map_double_def.ctx);
      *(double *) dst = mapped;
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_SCALAR;
      shape->schema = gen;
      return (shape);
    }

    case HEGEL_SCH_FILTER_DOUBLE: {
      hegel_schema * src = gen->filter_double_def.source;
      double raw;
      hegel__draw_fp_into (tc, src, &raw);
      if (! gen->filter_double_def.pred (raw, gen->filter_double_def.ctx))
        hegel_assume (tc, 0);
      *(double *) dst = raw;
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_SCALAR;
      shape->schema = gen;
      return (shape);
    }

    case HEGEL_SCH_FLAT_MAP_DOUBLE: {
      hegel_schema * src = gen->flat_map_double_def.source;
      double raw;
      hegel__draw_fp_into (tc, src, &raw);
      hegel_schema_t next = gen->flat_map_double_def.fn (raw,
          gen->flat_map_double_def.ctx);
      if (next._raw != NULL
          && next._raw->kind == HEGEL_SCH_FLOAT) {
        double result;
        hegel__draw_fp_into (tc, next._raw, &result);
        *(double *) dst = result;
      } else {
        *(double *) dst = 0.0;
      }
      hegel_schema_free (next);
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_SCALAR;
      shape->schema = gen;
      return (shape);
    }

    case HEGEL_SCH_ONE_OF_SCALAR: {
      /* Pick one of the case schemas, dispatch on its kind. */
      int nc = gen->one_of_scalar_def.n_cases;
      hegel_start_span (tc, HEGEL_SPAN_ONE_OF);
      int choice = hegel_draw_int (tc, 0, nc - 1);
      hegel_schema * chosen = gen->one_of_scalar_def.cases[choice];
      hegel_start_span (tc, HEGEL_SPAN_ENUM_VARIANT);
      if (chosen->kind == HEGEL_SCH_INTEGER) {
        hegel__draw_integer_into (tc, chosen, dst);
      } else if (chosen->kind == HEGEL_SCH_FLOAT) {
        hegel__draw_fp_into (tc, chosen, dst);
      }
      hegel_stop_span (tc, 0);
      hegel_stop_span (tc, 0);
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_SCALAR;
      shape->schema = gen;
      return (shape);
    }

    case HEGEL_SCH_REGEX: {
      /* Allocate a buffer, call hegel_draw_regex, store the pointer. */
      char * buf = (char *) malloc ((size_t) gen->regex_def.capacity);
      hegel_draw_regex (tc, gen->regex_def.pattern, buf,
                        gen->regex_def.capacity);
      *(char **) dst = buf;
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_TEXT;
      shape->schema = gen;
      shape->owned = buf;
      return (shape);
    }

    case HEGEL_SCH_STRUCT:
      /* Inline-by-value sub-struct: draw the nested struct's fields
      ** directly into the parent slot at `dst`.  Storage is owned by
      ** the parent struct block; the returned shape has owned=NULL. */
      return (hegel__draw_struct_into_slot (tc, gen, dst, depth));

    case HEGEL_SCH_SELF:
    case HEGEL_SCH_ONE_OF_STRUCT:
      /* ONE_OF_STRUCT can't be used directly as a struct field —
      ** use HEGEL_VARIANT for that.  It's only for use as an ARRAY
      ** element or inside HEGEL_OPTIONAL (handled via draw_alloc). */
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
      for (i = 0; i < s->struct_def.n_children; i ++)
        hegel__schema_free_raw (s->struct_def.children[i]);
      free (s->struct_def.children);
      free (s->struct_def.offsets);
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
      for (i = 0; i < s->union_def.n_cases; i ++)
        hegel__schema_free_raw (s->union_def.cases[i]);
      free (s->union_def.cases);
      break;
    case HEGEL_SCH_VARIANT:
    case HEGEL_SCH_ONE_OF_STRUCT:
      for (i = 0; i < s->variant_def.n_cases; i ++)
        hegel__schema_free_raw (s->variant_def.cases[i]);
      free (s->variant_def.cases);
      break;
    case HEGEL_SCH_MAP_INT:
      hegel__schema_free_raw (s->map_int_def.source);
      break;
    case HEGEL_SCH_FILTER_INT:
      hegel__schema_free_raw (s->filter_int_def.source);
      break;
    case HEGEL_SCH_FLAT_MAP_INT:
      hegel__schema_free_raw (s->flat_map_int_def.source);
      break;
    case HEGEL_SCH_MAP_I64:
      hegel__schema_free_raw (s->map_i64_def.source);
      break;
    case HEGEL_SCH_FILTER_I64:
      hegel__schema_free_raw (s->filter_i64_def.source);
      break;
    case HEGEL_SCH_FLAT_MAP_I64:
      hegel__schema_free_raw (s->flat_map_i64_def.source);
      break;
    case HEGEL_SCH_MAP_DOUBLE:
      hegel__schema_free_raw (s->map_double_def.source);
      break;
    case HEGEL_SCH_FILTER_DOUBLE:
      hegel__schema_free_raw (s->filter_double_def.source);
      break;
    case HEGEL_SCH_FLAT_MAP_DOUBLE:
      hegel__schema_free_raw (s->flat_map_double_def.source);
      break;
    case HEGEL_SCH_ONE_OF_SCALAR:
      for (i = 0; i < s->one_of_scalar_def.n_cases; i ++)
        hegel__schema_free_raw (s->one_of_scalar_def.cases[i]);
      free (s->one_of_scalar_def.cases);
      break;
    case HEGEL_SCH_REGEX:
      free (s->regex_def.pattern);
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
