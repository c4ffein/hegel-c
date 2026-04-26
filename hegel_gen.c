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
#include <stdbool.h>
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
  if (s._raw == NULL) return (s);
  s._raw->refcount ++;
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
** each schema (array_inline_def.len_offset), and build children + offsets
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
    case HEGEL_SCH_BIND:
      /* Non-positional: HEGEL_LET declares + draws + caches, but does
      ** not occupy a parent slot.  Place the value in the struct via
      ** a separate HEGEL_USE entry.  The layout pass treats size=0 as
      ** "skip, don't advance offset." */
      *out_size  = 0;
      *out_align = 1;
      return;
    case HEGEL_SCH_USE:
      /* Slot size/align come from the USE variant's width and float-ness. */
      if (s->use_def.is_float) {
        if (s->use_def.width == 8) {
          *out_size  = sizeof (double);
          *out_align = _Alignof (double);
        } else {
          *out_size  = sizeof (float);
          *out_align = _Alignof (float);
        }
      } else if (s->use_def.width == 8) {
        *out_size  = sizeof (int64_t);
        *out_align = _Alignof (int64_t);
      } else {
        *out_size  = sizeof (int);
        *out_align = _Alignof (int);
      }
      return;
    case HEGEL_SCH_LET_ARR:
      /* Non-positional like LET — declares + draws + caches an
      ** int array; no parent slot. */
      *out_size  = 0;
      *out_align = 1;
      return;
    case HEGEL_SCH_USE_AT:
      /* Reads cached_arr[current_index] as int — int-width slot. */
      *out_size  = sizeof (int);
      *out_align = _Alignof (int);
      return;
    case HEGEL_SCH_USE_PATH:
      /* int slot — same as USE_AT.  Wider variants TBD. */
      *out_size  = sizeof (int);
      *out_align = _Alignof (int);
      return;
    case HEGEL_SCH_ARR_OF:
      /* Single pointer slot in the parent.  Length does not occupy
      ** a field here — pair with HEGEL_LET / HEGEL_USE if the
      ** struct needs to store the length elsewhere. */
      *out_size  = sizeof (void *);
      *out_align = _Alignof (void *);
      return;
    case HEGEL_SCH_CONST_INT:
      *out_size  = sizeof (int);
      *out_align = _Alignof (int);
      return;
    case HEGEL_SCH_LEN_PREFIXED:
    case HEGEL_SCH_TERMINATED:
      /* Single pointer slot — the buffer pointer.  Length / sentinel
      ** are inside the buffer, not in any parent struct field. */
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

    /* Zero-size entries (HEGEL_LET) are non-positional — they run
    ** as side effects during draw but don't contribute to layout.
    ** Record SIZE_MAX as an offset sentinel so draw iteration can
    ** distinguish them. */
    if (slot0_size == 0) {
      children[i] = e;
      offsets[i]  = SIZE_MAX;
      continue;
    }

    slot0_off = hegel__align_up (cur, slot0_align);
    cur       = slot0_off + slot0_size;
    if (slot0_align > max_align) max_align = slot0_align;

    children[i] = e;
    offsets[i]  = slot0_off;

    /* ARRAY_INLINE has a second slot: the int count that follows the
    ** ptr.  Hardcode {sizeof(int), _Alignof(int)} and write the
    ** computed length-slot offset back into the schema. */
    if (e->kind == HEGEL_SCH_ARRAY_INLINE) {
      size_t slot1_off = hegel__align_up (cur, _Alignof (int));
      cur = slot1_off + sizeof (int);
      if (_Alignof (int) > max_align) max_align = _Alignof (int);
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

    if (e->kind == HEGEL_SCH_ARRAY_INLINE) {
      size_t slot1_off = hegel__align_up (cur, _Alignof (int));
      cur = slot1_off + sizeof (int);
      if (_Alignof (int) > max_align) max_align = _Alignof (int);
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
hegel_schema_array_inline (size_t len_offset, hegel_schema_t elem,
                           size_t elem_size, int min_len, int max_len)
{
  hegel_schema * e = elem._raw;
  /* Runtime check: ARRAY_INLINE elements must be STRUCT or UNION.
  ** Passing a VARIANT or scalar schema here would be silently wrong
  ** (VARIANT stores a pointer, which defeats "inline"; scalars should
  ** use HEGEL_ARR_OF). Fail loudly at setup time. */
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

hegel_schema_t
hegel_schema_bind (int binding_id, hegel_schema_t inner)
{
  hegel_schema * s;
  int            ok;
  if (inner._raw == NULL)
    hegel__abort ("hegel_schema_bind: inner schema is NULL");
  /* Accept INTEGER of width 4 or 8 (signed or unsigned), and FLOAT
  ** of width 4 or 8.  Smaller widths (1, 2) aren't useful for typical
  ** LET targets (sizes, counts, weights) and would just expand error
  ** surface — reject them.  Composed scalar schemas (MAP_INT, etc.)
  ** that ultimately produce an int are also fine. */
  ok = 0;
  switch (inner._raw->kind) {
    case HEGEL_SCH_INTEGER:
      ok = (inner._raw->integer.width == 4
         || inner._raw->integer.width == 8);
      break;
    case HEGEL_SCH_FLOAT:
      ok = (inner._raw->fp.width == 4 || inner._raw->fp.width == 8);
      break;
    case HEGEL_SCH_MAP_INT:
    case HEGEL_SCH_FILTER_INT:
    case HEGEL_SCH_FLAT_MAP_INT:
      ok = 1;   /* always produce int width */
      break;
    case HEGEL_SCH_MAP_I64:
    case HEGEL_SCH_FILTER_I64:
    case HEGEL_SCH_FLAT_MAP_I64:
      ok = 1;   /* always produce i64 */
      break;
    case HEGEL_SCH_MAP_DOUBLE:
    case HEGEL_SCH_FILTER_DOUBLE:
    case HEGEL_SCH_FLAT_MAP_DOUBLE:
      ok = 1;   /* always produce double */
      break;
    default:
      ok = 0;
  }
  if (!ok)
    hegel__abort ("hegel_schema_bind: HEGEL_LET inner must be a scalar "
                  "INTEGER (width 4 or 8) or FLOAT schema (got kind=%d)",
                  (int) inner._raw->kind);
  s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_BIND;
  s->refcount = 1;
  s->bind_def.binding_id = binding_id;
  s->bind_def.inner = inner._raw;   /* transfer ownership */
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_use (int binding_id)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_USE;
  s->refcount = 1;
  s->use_def.binding_id = binding_id;
  s->use_def.width      = (int) sizeof (int);
  s->use_def.is_signed  = 1;
  s->use_def.is_float   = 0;
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_use_i64 (int binding_id)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_USE;
  s->refcount = 1;
  s->use_def.binding_id = binding_id;
  s->use_def.width      = 8;
  s->use_def.is_signed  = 1;
  s->use_def.is_float   = 0;
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_use_u64 (int binding_id)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_USE;
  s->refcount = 1;
  s->use_def.binding_id = binding_id;
  s->use_def.width      = 8;
  s->use_def.is_signed  = 0;
  s->use_def.is_float   = 0;
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_use_float (int binding_id)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_USE;
  s->refcount = 1;
  s->use_def.binding_id = binding_id;
  s->use_def.width      = 4;
  s->use_def.is_signed  = 0;
  s->use_def.is_float   = 1;
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_use_double (int binding_id)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_USE;
  s->refcount = 1;
  s->use_def.binding_id = binding_id;
  s->use_def.width      = 8;
  s->use_def.is_signed  = 0;
  s->use_def.is_float   = 1;
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_let_arr (int binding_id, hegel_schema_t length,
                      hegel_schema_t elem)
{
  hegel_schema * s;
  if (length._raw == NULL)
    hegel__abort ("hegel_schema_let_arr: length schema is NULL");
  if (elem._raw == NULL)
    hegel__abort ("hegel_schema_let_arr: element schema is NULL");
  /* v1: int-width int element only.  Wider/typed array bindings can
  ** layer on top once the canonical pattern is shaken out. */
  if (elem._raw->kind != HEGEL_SCH_INTEGER
      || elem._raw->integer.width != (int) sizeof (int))
    hegel__abort ("hegel_schema_let_arr: v1 requires an int-width "
                  "HEGEL_INT element schema (got kind=%d width=%d)",
                  (int) elem._raw->kind,
                  elem._raw->kind == HEGEL_SCH_INTEGER
                    ? elem._raw->integer.width : 0);
  s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_LET_ARR;
  s->refcount = 1;
  s->let_arr_def.binding_id = binding_id;
  s->let_arr_def.length     = length._raw;   /* transfer ownership */
  s->let_arr_def.elem       = elem._raw;     /* transfer ownership */
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_use_at (int binding_id)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_USE_AT;
  s->refcount = 1;
  s->use_at_def.binding_id = binding_id;
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_use_path (const int * path)
{
  hegel_schema * s;
  int            len;
  int            saw_name;
  int            p;

  if (path == NULL)
    hegel__abort ("hegel_schema_use_path: NULL path");

  /* Validate: <PARENT>* <binding_id (>=0)> [<INDEX_HERE>] <END>.  The
  ** integer-literal compound-literal form makes it easy to mis-spell the
  ** path; up-front checks turn a quiet zero-write into a loud abort. */
  for (len = 0; path[len] != HEGEL__PATH_END; len ++) {
    if (len > 64)
      hegel__abort ("hegel_schema_use_path: path missing terminator or "
                    "absurdly long (len > 64).  Did you forget to use "
                    "the HEGEL_USE_PATH(...) macro?");
  }

  saw_name = 0;
  for (p = 0; p < len; p ++) {
    int step = path[p];
    if (step == HEGEL_PARENT) {
      if (saw_name)
        hegel__abort ("hegel_schema_use_path: HEGEL_PARENT must come "
                      "before the binding name.");
    } else if (step == HEGEL_INDEX_HERE) {
      if (!saw_name)
        hegel__abort ("hegel_schema_use_path: HEGEL_INDEX_HERE must come "
                      "after the binding name.");
      if (p != len - 1)
        hegel__abort ("hegel_schema_use_path: HEGEL_INDEX_HERE must be "
                      "the last step in the path.");
    } else if (step >= 0) {
      if (saw_name)
        hegel__abort ("hegel_schema_use_path: only one binding name "
                      "allowed per path (got id=%d after id was already set).",
                      step);
      saw_name = 1;
    } else {
      hegel__abort ("hegel_schema_use_path: unknown sentinel %d in path",
                    step);
    }
  }
  if (!saw_name)
    hegel__abort ("hegel_schema_use_path: path has no binding name.");

  s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_USE_PATH;
  s->refcount = 1;
  s->use_path_def.path     = (int *) malloc ((size_t) (len + 1) * sizeof (int));
  s->use_path_def.path_len = len;
  memcpy (s->use_path_def.path, path, (size_t) (len + 1) * sizeof (int));
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_const_int (int value)
{
  hegel_schema * s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_CONST_INT;
  s->refcount = 1;
  s->const_int_def.value = value;
  return (hegel_schema_t){s};
}

/* Shared length-validation: HEGEL_ARR_OF, HEGEL_LEN_PREFIXED_ARRAY,
** and HEGEL_TERMINATED_ARRAY all require length to be a named
** binding or a literal (no anonymous random draws). */
static void
hegel__validate_array_length (hegel_schema * length, const char * caller)
{
  if (length == NULL)
    hegel__abort ("%s: length schema is NULL", caller);
  switch (length->kind) {
    case HEGEL_SCH_USE:
    case HEGEL_SCH_USE_AT:
    case HEGEL_SCH_USE_PATH:
    case HEGEL_SCH_CONST_INT:
      return;
    case HEGEL_SCH_INTEGER:
      hegel__abort ("%s: length must be HEGEL_USE(name) or HEGEL_CONST(N).  "
                    "Raw HEGEL_INT(lo,hi) is not allowed; wrap in HEGEL_LET "
                    "first:  HEGEL_LET(name, HEGEL_INT(lo,hi)) + "
                    "HEGEL_USE(name).", caller);
      break;
    default:
      hegel__abort ("%s: length must be HEGEL_USE(name) or HEGEL_CONST(N); "
                    "got unsupported schema kind.", caller);
      break;
  }
}

/* Width-aware integer write — used by LEN_PREFIXED to lay down the
** length value at slot 0 and by TERMINATED to lay down the sentinel
** at slot n, in the elem type's bit-width. */
static void
hegel__write_int_at (void * dst, int width, int64_t value)
{
  switch (width) {
    case 1: { uint8_t  v = (uint8_t)  value; memcpy (dst, &v, 1); return; }
    case 2: { uint16_t v = (uint16_t) value; memcpy (dst, &v, 2); return; }
    case 4: { uint32_t v = (uint32_t) value; memcpy (dst, &v, 4); return; }
    case 8: { uint64_t v = (uint64_t) value; memcpy (dst, &v, 8); return; }
    default:
      hegel__abort ("hegel__write_int_at: unsupported width %d", width);
  }
}

/* Width-aware integer read — used by TERMINATED's runtime collision
** check to read back what was just drawn for sentinel comparison. */
static int64_t
hegel__read_int_at (void * src, int width, int is_signed)
{
  switch (width) {
    case 1: {
      uint8_t v;  memcpy (&v, src, 1);
      return is_signed ? (int64_t)(int8_t) v : (int64_t) v;
    }
    case 2: {
      uint16_t v; memcpy (&v, src, 2);
      return is_signed ? (int64_t)(int16_t) v : (int64_t) v;
    }
    case 4: {
      uint32_t v; memcpy (&v, src, 4);
      return is_signed ? (int64_t)(int32_t) v : (int64_t) v;
    }
    case 8: {
      int64_t v;  memcpy (&v, src, 8);
      return v;
    }
    default:
      hegel__abort ("hegel__read_int_at: unsupported width %d", width);
      return 0;
  }
}

/* Schema-build-time best-effort sentinel-collision check.  For a
** bounded HEGEL_INT or HEGEL_CONST elem we can prove or disprove
** collision statically; for derived schemas (USE / MAP / FILTER /
** FLAT_MAP) we defer to a runtime check during draw. */
static void
hegel__check_sentinel_collision (hegel_schema * elem, int64_t sentinel,
                                 const char * caller)
{
  switch (elem->kind) {
    case HEGEL_SCH_INTEGER: {
      int64_t lo, hi;
      if (elem->integer.is_signed) {
        lo = elem->integer.min_s;
        hi = elem->integer.max_s;
      } else {
        lo = (int64_t) elem->integer.min_u;
        hi = (elem->integer.max_u > (uint64_t) INT64_MAX)
                 ? INT64_MAX
                 : (int64_t) elem->integer.max_u;
      }
      if (sentinel >= lo && sentinel <= hi)
        hegel__abort ("%s: sentinel %lld lies within elem range [%lld, %lld] "
                      "— would collide with drawn elements.  Pick a sentinel "
                      "outside the elem range, or constrain the elem schema.",
                      caller, (long long) sentinel,
                      (long long) lo, (long long) hi);
      break;
    }
    case HEGEL_SCH_CONST_INT:
      if ((int64_t) elem->const_int_def.value == sentinel)
        hegel__abort ("%s: sentinel %lld equals HEGEL_CONST elem value — "
                      "every drawn element would be the sentinel.",
                      caller, (long long) sentinel);
      break;
    default:
      /* Composed/derived elem — runtime check enforces non-collision. */
      break;
  }
}

hegel_schema_t
hegel_schema_arr_of (hegel_schema_t length, hegel_schema_t elem)
{
  hegel_schema * s;
  hegel__validate_array_length (length._raw, "hegel_schema_arr_of");
  if (elem._raw == NULL)
    hegel__abort ("hegel_schema_arr_of: element schema is NULL");
  s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_ARR_OF;
  s->refcount = 1;
  s->arr_of_def.length = length._raw;   /* transfer ownership */
  s->arr_of_def.elem   = elem._raw;     /* transfer ownership */
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_len_prefixed_array (hegel_schema_t length, hegel_schema_t elem)
{
  hegel_schema * s;
  hegel__validate_array_length (length._raw,
                                "hegel_schema_len_prefixed_array");
  if (elem._raw == NULL)
    hegel__abort ("hegel_schema_len_prefixed_array: element schema is NULL");
  if (elem._raw->kind != HEGEL_SCH_INTEGER)
    hegel__abort ("hegel_schema_len_prefixed_array: element schema must be "
                  "HEGEL_SCH_INTEGER (got kind=%d).  v0 supports integer "
                  "elements only — wrap a non-integer schema separately if "
                  "you need richer payloads.", (int) elem._raw->kind);
  s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_LEN_PREFIXED;
  s->refcount = 1;
  s->len_prefixed_def.length = length._raw;
  s->len_prefixed_def.elem   = elem._raw;
  return (hegel_schema_t){s};
}

hegel_schema_t
hegel_schema_terminated_array (hegel_schema_t length, hegel_schema_t elem,
                               int64_t sentinel)
{
  hegel_schema * s;
  hegel__validate_array_length (length._raw,
                                "hegel_schema_terminated_array");
  if (elem._raw == NULL)
    hegel__abort ("hegel_schema_terminated_array: element schema is NULL");
  if (elem._raw->kind != HEGEL_SCH_INTEGER)
    hegel__abort ("hegel_schema_terminated_array: element schema must be "
                  "HEGEL_SCH_INTEGER (got kind=%d).  v0 supports integer "
                  "elements only.", (int) elem._raw->kind);
  hegel__check_sentinel_collision (elem._raw, sentinel,
                                   "hegel_schema_terminated_array");
  s = (hegel_schema *) calloc (1, sizeof (hegel_schema));
  s->kind = HEGEL_SCH_TERMINATED;
  s->refcount = 1;
  s->terminated_def.length   = length._raw;
  s->terminated_def.elem     = elem._raw;
  s->terminated_def.sentinel = sentinel;
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
    case HEGEL_SCH_BIND:
      /* Recurse through inner so SELF inside a HEGEL_LET resolves. */
      hegel__resolve_self (node->bind_def.inner, target);
      break;
    case HEGEL_SCH_USE:
    case HEGEL_SCH_USE_AT:
    case HEGEL_SCH_USE_PATH:
      /* Leaf — no inner schema to recurse into. */
      break;
    case HEGEL_SCH_LET_ARR:
      hegel__resolve_self (node->let_arr_def.length, target);
      hegel__resolve_self (node->let_arr_def.elem,   target);
      break;
    case HEGEL_SCH_ARR_OF:
      hegel__resolve_self (node->arr_of_def.length, target);
      hegel__resolve_self (node->arr_of_def.elem,   target);
      break;
    case HEGEL_SCH_CONST_INT:
      /* Leaf — no children. */
      break;
    case HEGEL_SCH_LEN_PREFIXED:
      hegel__resolve_self (node->len_prefixed_def.length, target);
      hegel__resolve_self (node->len_prefixed_def.elem,   target);
      break;
    case HEGEL_SCH_TERMINATED:
      hegel__resolve_self (node->terminated_def.length, target);
      hegel__resolve_self (node->terminated_def.elem,   target);
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
  /* Abort loudly rather than return a silent 0.  A common footgun is
  ** calling this on the int-slot shape (the HEGEL_USE writing the
  ** count, a trivial scalar leaf), when you meant the pointer-slot
  ** shape (which owns the HEGEL_SHAPE_ARRAY).  A silent 0 there
  ** masks the mistake. */
  if (s == NULL)
    hegel__abort ("hegel_shape_array_len: NULL shape");
  if (s->kind != HEGEL_SHAPE_ARRAY)
    hegel__abort ("hegel_shape_array_len: shape kind=%d, not "
                  "HEGEL_SHAPE_ARRAY — did you pass the count slot "
                  "instead of the HEGEL_ARR_OF pointer slot?",
                  s->kind);
  return (s->array_shape.len);
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

/* Per-draw binding registry, scoped to one struct instance.  A fresh
** instance is created on the stack at each hegel__draw_struct /
** _into_slot entry and threaded as an explicit parameter through
** hegel__draw_field and its recursive descendants.  Chained via
** `parent` so an inner HEGEL_USE can resolve an outer HEGEL_LET.
** Struct is stack-allocated; the binding table is inline (fixed
** size HEGEL__MAX_BINDINGS_PER_SCOPE). */

/* Binding table kind tags.  Selected by the inner schema kind/width
** at HEGEL_LET draw time; checked at HEGEL_USE_* draw time. */
#define HEGEL__BKIND_INT     1  /* signed,   width 4 */
#define HEGEL__BKIND_I64     2  /* signed,   width 8 */
#define HEGEL__BKIND_U64     3  /* unsigned, width 8 */
#define HEGEL__BKIND_FLOAT   4  /* float,    width 4 */
#define HEGEL__BKIND_DOUBLE  5  /* double,   width 8 */
#define HEGEL__BKIND_INT_ARR 6  /* int[], pointer in value cell */

#define HEGEL__MAX_BINDINGS_PER_SCOPE 16

typedef struct hegel__draw_ctx_s {
  /* Let-bindings: fixed-size inline table keyed by compile-time
  ** binding id (HEGEL_BINDING = __COUNTER__).  Values cached as
  ** int64_t; kind tag drives runtime type-check at USE.
  **
  ** For BKIND_INT_ARR: binding_values[i] holds a pointer (cast to
  ** int64_t) into a heap buffer; binding_array_lens[i] is its length.
  ** The buffer is freed at ctx-exit. */
  int              binding_ids       [HEGEL__MAX_BINDINGS_PER_SCOPE];
  int64_t          binding_values    [HEGEL__MAX_BINDINGS_PER_SCOPE];
  int              binding_kinds     [HEGEL__MAX_BINDINGS_PER_SCOPE];
  int              binding_array_lens[HEGEL__MAX_BINDINGS_PER_SCOPE];
  int              binding_n;
  /* current_index is set by HEGEL_ARR_OF while iterating its elements,
  ** at the ctx of the struct where the ARR_OF appears.  HEGEL_USE_AT
  ** reads this from the scope where the named binding lives.
  ** -1 means "not currently inside an ARR_OF iteration of this scope". */
  int              current_index;
  /* Lexical scope chain.  Set at init to the enclosing struct's ctx
  ** (or NULL at top level).  hegel__draw_ctx_lookup walks this chain
  ** when a binding isn't found locally, so a HEGEL_USE in an inner
  ** struct can resolve an outer HEGEL_LET. */
  struct hegel__draw_ctx_s * parent;
} hegel__draw_ctx;

static void
hegel__draw_ctx_init (hegel__draw_ctx * ctx, hegel__draw_ctx * parent)
{
  ctx->binding_n = 0;
  ctx->current_index = -1;
  ctx->parent = parent;
}

/* Free heap-allocated buffers held by array-kind bindings.  Called
** at the end of struct draw, before the local ctx goes out of scope. */
static void
hegel__draw_ctx_finalize (hegel__draw_ctx * ctx)
{
  int i;
  for (i = 0; i < ctx->binding_n; i ++) {
    if (ctx->binding_kinds[i] == HEGEL__BKIND_INT_ARR) {
      int * arr = (int *) (intptr_t) ctx->binding_values[i];
      free (arr);
      ctx->binding_values[i] = 0;
    }
  }
}

static void
hegel__draw_ctx_bind_full (hegel__draw_ctx * ctx, int binding_id,
                           int kind, int64_t value, int array_len)
{
  int i;
  /* Re-binding the same name in the same scope is almost always a
  ** user mistake (typo'd name collided with an earlier LET, or a
  ** copy-paste left two HEGEL_LETs with the same id).  Abort loudly
  ** rather than silently overwriting — silent overwrite would mask
  ** the bug and let the second LET "win" with no warning. */
  for (i = 0; i < ctx->binding_n; i ++) {
    if (ctx->binding_ids[i] == binding_id) {
      hegel__abort ("hegel__draw_ctx_bind: HEGEL_LET for binding id=%d "
                    "appears twice in the same scope.  Each binding id "
                    "may only be HEGEL_LET once per struct instance; "
                    "use a separate HEGEL_BINDING declaration for a "
                    "second value.",
                    binding_id);
    }
  }
  if (ctx->binding_n >= HEGEL__MAX_BINDINGS_PER_SCOPE)
    hegel__abort ("hegel__draw_ctx_bind: more than %d bindings in one "
                  "scope (raise HEGEL__MAX_BINDINGS_PER_SCOPE)",
                  HEGEL__MAX_BINDINGS_PER_SCOPE);
  ctx->binding_ids       [ctx->binding_n] = binding_id;
  ctx->binding_values    [ctx->binding_n] = value;
  ctx->binding_kinds     [ctx->binding_n] = kind;
  ctx->binding_array_lens[ctx->binding_n] = array_len;
  ctx->binding_n ++;
}

static void
hegel__draw_ctx_bind (hegel__draw_ctx * ctx, int binding_id,
                      int kind, int64_t value)
{
  hegel__draw_ctx_bind_full (ctx, binding_id, kind, value, 0);
}

static int
hegel__draw_ctx_lookup (hegel__draw_ctx * ctx, int binding_id,
                        int * out_kind, int64_t * out_value)
{
  /* Walk the scope chain from innermost outward: if the binding
  ** isn't bound in the current struct's ctx, try the enclosing
  ** struct's, and so on up to the root.  First hit wins (shadowing
  ** by inner scopes is a consequence, not an explicit feature). */
  hegel__draw_ctx *  s;
  int                i;
  for (s = ctx; s != NULL; s = s->parent) {
    for (i = 0; i < s->binding_n; i ++) {
      if (s->binding_ids[i] == binding_id) {
        *out_kind  = s->binding_kinds [i];
        *out_value = s->binding_values[i];
        return (1);
      }
    }
  }
  return (0);
}

/* Like _lookup but also returns the scope where the binding lives
** AND its array length (for array-kind bindings).  USE_AT needs the
** scope so it can read that scope's current_index. */
static int
hegel__draw_ctx_lookup_full (hegel__draw_ctx * ctx, int binding_id,
                             int * out_kind, int64_t * out_value,
                             int * out_arr_len,
                             hegel__draw_ctx ** out_scope)
{
  hegel__draw_ctx *  s;
  int                i;
  for (s = ctx; s != NULL; s = s->parent) {
    for (i = 0; i < s->binding_n; i ++) {
      if (s->binding_ids[i] == binding_id) {
        *out_kind    = s->binding_kinds     [i];
        *out_value   = s->binding_values    [i];
        *out_arr_len = s->binding_array_lens[i];
        *out_scope   = s;
        return (1);
      }
    }
  }
  return (0);
}

static hegel_shape *
hegel__draw_struct (hegel_testcase * tc, hegel_schema * gen, void ** out,
                    int depth, hegel__draw_ctx * parent_ctx);
static hegel_shape *
hegel__draw_field (hegel_testcase * tc, hegel_schema * gen,
                   void * parent, size_t offset, int depth,
                   hegel__draw_ctx * ctx);
static void hegel__draw_integer_into (hegel_testcase * tc,
                                      hegel_schema * gen, void * dst);
static void hegel__draw_fp_into       (hegel_testcase * tc,
                                       hegel_schema * gen, void * dst);

/* Side-effect-only draw for HEGEL_SCH_BIND (non-positional LET):
** draws the inner scalar into a stack scratch and caches the value in
** the ctx's binding table.  No slot in the parent struct, no shape
** field emitted.  Inner kind drives the cached binding kind so the
** matching HEGEL_USE_* can width-check at USE time. */
static void
hegel__draw_let_side_effect (hegel_testcase * tc, hegel_schema * gen,
                             hegel__draw_ctx * ctx)
{
  hegel_schema *   inner;
  int              bkind;
  int64_t          stored;

  if (gen->kind != HEGEL_SCH_BIND)
    hegel__abort ("hegel__draw_let_side_effect: expected HEGEL_SCH_BIND, "
                  "got kind=%d", (int) gen->kind);
  if (ctx == NULL)
    hegel__abort ("hegel__draw_let_side_effect: HEGEL_LET needs a draw "
                  "ctx (only valid inside HEGEL_STRUCT).");

  inner = gen->bind_def.inner;
  stored = 0;

  /* Pick BKIND from inner kind+width.  Composed scalar schemas
  ** (MAP/FILTER/FLAT_MAP) inherit the kind they produce. */
  switch (inner->kind) {
    case HEGEL_SCH_INTEGER:
      if (inner->integer.width == 8) {
        bkind = inner->integer.is_signed ? HEGEL__BKIND_I64
                                         : HEGEL__BKIND_U64;
      } else {
        bkind = HEGEL__BKIND_INT;
      }
      break;
    case HEGEL_SCH_MAP_INT:
    case HEGEL_SCH_FILTER_INT:
    case HEGEL_SCH_FLAT_MAP_INT:
      bkind = HEGEL__BKIND_INT;
      break;
    case HEGEL_SCH_MAP_I64:
    case HEGEL_SCH_FILTER_I64:
    case HEGEL_SCH_FLAT_MAP_I64:
      bkind = HEGEL__BKIND_I64;
      break;
    case HEGEL_SCH_FLOAT:
      bkind = (inner->fp.width == 8) ? HEGEL__BKIND_DOUBLE
                                     : HEGEL__BKIND_FLOAT;
      break;
    case HEGEL_SCH_MAP_DOUBLE:
    case HEGEL_SCH_FILTER_DOUBLE:
    case HEGEL_SCH_FLAT_MAP_DOUBLE:
      bkind = HEGEL__BKIND_DOUBLE;
      break;
    default:
      hegel__abort ("hegel__draw_let_side_effect: unsupported inner "
                    "kind=%d", (int) inner->kind);
      return;   /* unreachable */
  }

  /* Draw and stash the bit pattern in `stored` (int64_t cell). */
  switch (bkind) {
    case HEGEL__BKIND_INT: {
      int v = 0;
      hegel__draw_integer_into (tc, inner, &v);
      stored = (int64_t) v;
      break;
    }
    case HEGEL__BKIND_I64: {
      int64_t v = 0;
      hegel__draw_integer_into (tc, inner, &v);
      stored = v;
      break;
    }
    case HEGEL__BKIND_U64: {
      uint64_t v = 0;
      hegel__draw_integer_into (tc, inner, &v);
      memcpy (&stored, &v, sizeof (stored));
      break;
    }
    case HEGEL__BKIND_FLOAT: {
      float v = 0.0f;
      hegel__draw_fp_into (tc, inner, &v);
      memcpy (&stored, &v, sizeof (v));
      break;
    }
    case HEGEL__BKIND_DOUBLE: {
      double v = 0.0;
      hegel__draw_fp_into (tc, inner, &v);
      memcpy (&stored, &v, sizeof (v));
      break;
    }
  }

  hegel__draw_ctx_bind (ctx, gen->bind_def.binding_id, bkind, stored);
}

/* Side-effect-only draw for HEGEL_SCH_LET_ARR (non-positional):
** draws a length, then draws that many ints from elem; stashes the
** heap buffer pointer + length in the ctx's binding table.  The
** buffer is owned by the ctx and freed in hegel__draw_ctx_finalize. */
static void
hegel__draw_let_arr_side_effect (hegel_testcase * tc, hegel_schema * gen,
                                 hegel__draw_ctx * ctx)
{
  int       n = 0;
  int *     arr;
  int       i;

  if (gen->kind != HEGEL_SCH_LET_ARR)
    hegel__abort ("hegel__draw_let_arr_side_effect: expected HEGEL_SCH_LET_ARR, "
                  "got kind=%d", (int) gen->kind);
  if (ctx == NULL)
    hegel__abort ("hegel__draw_let_arr_side_effect: HEGEL_LET_ARR needs a "
                  "draw ctx (only valid inside HEGEL_STRUCT).");

  /* Length: reuse field-draw so HEGEL_USE / HEGEL_INT / HEGEL_CONST all
  ** work.  &n is the local "slot" the int gets written to. */
  hegel_shape * len_shape = hegel__draw_field (tc, gen->let_arr_def.length,
                                               &n, 0, 0, ctx);
  hegel_shape_free (len_shape);

  if (n < 0)
    hegel__abort ("hegel__draw_let_arr_side_effect: length produced n=%d", n);

  arr = (int *) calloc ((size_t) n + 1, sizeof (int));
  hegel_start_span (tc, HEGEL_SPAN_LIST);
  for (i = 0; i < n; i ++) {
    hegel_start_span (tc, HEGEL_SPAN_LIST_ELEMENT);
    hegel__draw_integer_into (tc, gen->let_arr_def.elem, &arr[i]);
    hegel_stop_span (tc, 0);
  }
  hegel_stop_span (tc, 0);

  hegel__draw_ctx_bind_full (ctx, gen->let_arr_def.binding_id,
                             HEGEL__BKIND_INT_ARR,
                             (int64_t) (intptr_t) arr, n);
}

static hegel_shape *
hegel__draw_struct_into_slot (hegel_testcase * tc, hegel_schema * gen,
                              void * slot, int depth,
                              hegel__draw_ctx * parent_ctx);

/* ---- Draw an integer into memory at `dst` ---- */

static void
hegel__draw_integer_into (hegel_testcase * tc, hegel_schema * gen, void * dst)
{
  switch (gen->kind) {

  case HEGEL_SCH_INTEGER:
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
    return;

  case HEGEL_SCH_MAP_INT: {
    int raw;
    hegel__draw_integer_into (tc, gen->map_int_def.source, &raw);
    *(int *) dst = gen->map_int_def.fn (raw, gen->map_int_def.ctx);
    return;
  }
  case HEGEL_SCH_FILTER_INT: {
    int raw;
    hegel__draw_integer_into (tc, gen->filter_int_def.source, &raw);
    if (! gen->filter_int_def.pred (raw, gen->filter_int_def.ctx))
      hegel_assume (tc, 0);
    *(int *) dst = raw;
    return;
  }
  case HEGEL_SCH_FLAT_MAP_INT: {
    int raw;
    hegel__draw_integer_into (tc, gen->flat_map_int_def.source, &raw);
    hegel_schema_t next = gen->flat_map_int_def.fn (raw,
        gen->flat_map_int_def.ctx);
    int result = 0;
    if (next._raw != NULL)
      hegel__draw_integer_into (tc, next._raw, &result);
    *(int *) dst = result;
    hegel_schema_free (next);
    return;
  }

  case HEGEL_SCH_MAP_I64: {
    int64_t raw;
    hegel__draw_integer_into (tc, gen->map_i64_def.source, &raw);
    *(int64_t *) dst = gen->map_i64_def.fn (raw, gen->map_i64_def.ctx);
    return;
  }
  case HEGEL_SCH_FILTER_I64: {
    int64_t raw;
    hegel__draw_integer_into (tc, gen->filter_i64_def.source, &raw);
    if (! gen->filter_i64_def.pred (raw, gen->filter_i64_def.ctx))
      hegel_assume (tc, 0);
    *(int64_t *) dst = raw;
    return;
  }
  case HEGEL_SCH_FLAT_MAP_I64: {
    int64_t raw;
    hegel__draw_integer_into (tc, gen->flat_map_i64_def.source, &raw);
    hegel_schema_t next = gen->flat_map_i64_def.fn (raw,
        gen->flat_map_i64_def.ctx);
    int64_t result = 0;
    if (next._raw != NULL)
      hegel__draw_integer_into (tc, next._raw, &result);
    *(int64_t *) dst = result;
    hegel_schema_free (next);
    return;
  }

  case HEGEL_SCH_ONE_OF_SCALAR: {
    /* Pick one case by index, recurse into it.  Emit the same two
    ** spans as the top-level ONE_OF_SCALAR path in hegel__draw_field
    ** so shrink quality is preserved when ONE_OF is wrapped in a
    ** combinator (map/filter/flat_map). */
    int nc = gen->one_of_scalar_def.n_cases;
    hegel_start_span (tc, HEGEL_SPAN_ONE_OF);
    int choice = hegel_draw_int (tc, 0, nc - 1);
    hegel_schema * chosen = gen->one_of_scalar_def.cases[choice];
    hegel_start_span (tc, HEGEL_SPAN_ENUM_VARIANT);
    hegel__draw_integer_into (tc, chosen, dst);
    hegel_stop_span (tc, 0);
    hegel_stop_span (tc, 0);
    return;
  }

  default:
    /* Non-integer source in an integer context — leave dst zero. */
    return;
  }
}

/* ---- Draw a float/double into memory at `dst` ---- */

static void
hegel__draw_fp_into (hegel_testcase * tc, hegel_schema * gen, void * dst)
{
  switch (gen->kind) {

  case HEGEL_SCH_FLOAT:
    if (gen->fp.width == 4) {
      float v = hegel_draw_float (tc, (float) gen->fp.min, (float) gen->fp.max);
      *(float *) dst = v;
    } else {
      double v = hegel_draw_double (tc, gen->fp.min, gen->fp.max);
      *(double *) dst = v;
    }
    return;

  case HEGEL_SCH_MAP_DOUBLE: {
    double raw;
    hegel__draw_fp_into (tc, gen->map_double_def.source, &raw);
    *(double *) dst = gen->map_double_def.fn (raw, gen->map_double_def.ctx);
    return;
  }
  case HEGEL_SCH_FILTER_DOUBLE: {
    double raw;
    hegel__draw_fp_into (tc, gen->filter_double_def.source, &raw);
    if (! gen->filter_double_def.pred (raw, gen->filter_double_def.ctx))
      hegel_assume (tc, 0);
    *(double *) dst = raw;
    return;
  }
  case HEGEL_SCH_FLAT_MAP_DOUBLE: {
    double raw;
    hegel__draw_fp_into (tc, gen->flat_map_double_def.source, &raw);
    hegel_schema_t next = gen->flat_map_double_def.fn (raw,
        gen->flat_map_double_def.ctx);
    double result = 0.0;
    if (next._raw != NULL)
      hegel__draw_fp_into (tc, next._raw, &result);
    *(double *) dst = result;
    hegel_schema_free (next);
    return;
  }

  case HEGEL_SCH_ONE_OF_SCALAR: {
    /* Pick one case by index, recurse into it.  Emit the same two
    ** spans as the top-level ONE_OF_SCALAR path in hegel__draw_field
    ** so shrink quality is preserved when ONE_OF is wrapped in a
    ** combinator (map/filter/flat_map). */
    int nc = gen->one_of_scalar_def.n_cases;
    hegel_start_span (tc, HEGEL_SPAN_ONE_OF);
    int choice = hegel_draw_int (tc, 0, nc - 1);
    hegel_schema * chosen = gen->one_of_scalar_def.cases[choice];
    hegel_start_span (tc, HEGEL_SPAN_ENUM_VARIANT);
    hegel__draw_fp_into (tc, chosen, dst);
    hegel_stop_span (tc, 0);
    hegel_stop_span (tc, 0);
    return;
  }

  default:
    /* Non-float source in a float context — leave dst zero. */
    return;
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
                   int depth, hegel__draw_ctx * parent_ctx)
{
  hegel_schema *      actual;
  hegel_shape *       shape;

  actual = gen;
  if (gen->kind == HEGEL_SCH_SELF)
    actual = gen->self_ref.target;

  switch (actual->kind) {

    case HEGEL_SCH_STRUCT:
      return (hegel__draw_struct (tc, actual, out, depth, parent_ctx));

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
      inner = hegel__draw_struct (tc, chosen, &child, depth - 1, parent_ctx);
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
    case HEGEL_SCH_OPTIONAL_PTR:  return (sizeof (void *));
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
                              void * slot, int depth,
                              hegel__draw_ctx * parent_ctx)
{
  int                 nf = gen->struct_def.n_children;
  hegel_shape *       sh;
  int                 f;
  hegel__draw_ctx     local_ctx;

  sh = (hegel_shape *) calloc (1, sizeof (hegel_shape));
  sh->kind = HEGEL_SHAPE_STRUCT;
  sh->schema = gen;
  sh->owned = NULL;
  hegel__alloc_struct_fields (sh, nf);

  /* Per-struct-instance ctx: sub-structs drawn from within (inline,
  ** array elements, variant cases) get their own ctx so their
  ** bindings are scoped locally.  `parent_ctx` links the scope
  ** chain — a HEGEL_USE here can reach a HEGEL_LET in the
  ** enclosing struct. */
  hegel__draw_ctx_init (&local_ctx, parent_ctx);

  hegel_start_span (tc, HEGEL_SPAN_TUPLE);
  for (f = 0; f < nf; f ++) {
    size_t          field_off = gen->struct_def.offsets[f];
    hegel_schema *  field_sch = gen->struct_def.children[f];
    if (field_sch->kind == HEGEL_SCH_BIND) {
      /* Non-positional LET — run as side effect, no slot write,
      ** no shape field emitted. */
      hegel__draw_let_side_effect (tc, field_sch, &local_ctx);
      sh->struct_shape.fields[f] = NULL;
    } else if (field_sch->kind == HEGEL_SCH_LET_ARR) {
      hegel__draw_let_arr_side_effect (tc, field_sch, &local_ctx);
      sh->struct_shape.fields[f] = NULL;
    } else {
      sh->struct_shape.fields[f] =
          hegel__draw_field (tc, field_sch, slot, field_off, depth - 1,
                             &local_ctx);
    }
  }
  hegel_stop_span (tc, 0);

  hegel__draw_ctx_finalize (&local_ctx);
  return (sh);
}

/* ---- Draw struct ---- */

static hegel_shape *
hegel__draw_struct (hegel_testcase * tc, hegel_schema * gen, void ** out,
                    int depth, hegel__draw_ctx * parent_ctx)
{
  void *              ptr;
  hegel_shape *       shape;
  int                 n;
  int                 i;
  hegel__draw_ctx     local_ctx;

  n = gen->struct_def.n_children;
  ptr = calloc (1, gen->struct_def.size);

  shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
  shape->kind = HEGEL_SHAPE_STRUCT;
  shape->schema = gen;
  shape->owned = ptr;
  hegel__alloc_struct_fields (shape, n);

  /* Per-struct-instance ctx with local binding table.  Chained via
  ** `parent_ctx` so nested USE sites can reach outer LETs. */
  hegel__draw_ctx_init (&local_ctx, parent_ctx);

  hegel_start_span (tc, HEGEL_SPAN_TUPLE);
  for (i = 0; i < n; i ++) {
    size_t          field_off = gen->struct_def.offsets[i];
    hegel_schema *  field_sch = gen->struct_def.children[i];
    if (field_sch->kind == HEGEL_SCH_BIND) {
      hegel__draw_let_side_effect (tc, field_sch, &local_ctx);
      shape->struct_shape.fields[i] = NULL;
    } else if (field_sch->kind == HEGEL_SCH_LET_ARR) {
      hegel__draw_let_arr_side_effect (tc, field_sch, &local_ctx);
      shape->struct_shape.fields[i] = NULL;
    } else {
      shape->struct_shape.fields[i] =
          hegel__draw_field (tc, field_sch, ptr, field_off, depth, &local_ctx);
    }
  }
  hegel_stop_span (tc, 0);

  hegel__draw_ctx_finalize (&local_ctx);
  *out = ptr;
  return (shape);
}

/* ---- Draw field into parent struct ---- */

static hegel_shape *
hegel__draw_field (hegel_testcase * tc, hegel_schema * gen,
                   void * parent, size_t offset, int depth,
                   hegel__draw_ctx * ctx)
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

      if (present && depth == 0) {
        /* The draw *wanted* to recurse but the depth bound stops it.
        ** That's a signal the schema's termination probability is too
        ** low (or max_depth is too shallow) — treat as a health check
        ** failure rather than silently truncating. */
        hegel_health_fail ("max recursion depth reached — raise "
                           "max_depth via hegel_schema_draw_n, or "
                           "adjust schema so recursion terminates "
                           "earlier");
        /* unreachable */
      }

      if (present) {
        void * child;
        shape->optional_shape.inner =
            hegel__draw_alloc (tc, gen->optional_ptr.inner, &child,
                               depth - 1, ctx);
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
              hegel__draw_struct_into_slot (tc, elem, slot, depth, ctx);

        } else if (elem->kind == HEGEL_SCH_UNION
                || elem->kind == HEGEL_SCH_UNION_UNTAGGED) {
          /* Inline union element — draw it into slot.  Offset 0
          ** because the union's internal offsets are relative to
          ** the slot start (just like top-level fields).  Forward
          ** the parent struct's ctx: union fields never reach
          ** SUBSCHEMA today, but forwarding keeps the chain
          ** consistent if that changes. */
          shape->array_shape.elems[i] =
              hegel__draw_field (tc, elem, slot, 0, depth - 1, ctx);

        } else {
          /* Scalar element in inline array — shouldn't happen
          ** (use HEGEL_ARR_OF for scalar arrays), but handle it. */
          shape->array_shape.elems[i] =
              hegel__draw_field (tc, elem, slot, 0, depth - 1, ctx);
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
          hegel__draw_struct_into_slot (tc, chosen, dst, depth + 1, ctx);

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
            hegel__draw_struct (tc, chosen, &child, depth - 1, ctx);
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
      ** the parent struct block; the returned shape has owned=NULL.
      ** Pass `ctx` as the inner struct's parent_ctx so HEGEL_USE in
      ** the inner can resolve a HEGEL_LET in this enclosing struct. */
      return (hegel__draw_struct_into_slot (tc, gen, dst, depth, ctx));

    case HEGEL_SCH_SELF:
    case HEGEL_SCH_ONE_OF_STRUCT:
      /* ONE_OF_STRUCT can't be used directly as a struct field —
      ** use HEGEL_VARIANT for that.  It's only for use as an ARRAY
      ** element or inside HEGEL_OPTIONAL (handled via draw_alloc). */
      break;

    case HEGEL_SCH_BIND:
    case HEGEL_SCH_LET_ARR:
      /* HEGEL_LET / HEGEL_LET_ARR are non-positional: handled as side
      ** effects by hegel__draw_struct / _into_slot directly, never via
      ** this slot-writing path.  Reaching here means the struct draw
      ** iterator didn't detect the kind — a bug. */
      hegel__abort ("hegel__draw_field: HEGEL_SCH_BIND / HEGEL_SCH_LET_ARR "
                    "is non-positional and must be handled by the side-"
                    "effect path, not through the slot-draw path.");
      break;

    case HEGEL_SCH_CONST_INT: {
      *(int *) dst = gen->const_int_def.value;
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_SCALAR;
      shape->schema = gen;
      return (shape);
    }

    case HEGEL_SCH_USE: {
      int       kind;
      int64_t   value;
      int       expected;
      if (ctx == NULL)
        hegel__abort ("hegel__draw_field: HEGEL_USE reached without a "
                      "draw ctx — HEGEL_USE only makes sense inside a "
                      "HEGEL_STRUCT.");
      if (!hegel__draw_ctx_lookup (ctx, gen->use_def.binding_id,
                                   &kind, &value))
        hegel__abort ("hegel__draw_field: HEGEL_USE(id=%d) references a "
                      "binding that has not been HEGEL_LET in the "
                      "enclosing HEGEL_STRUCT.  Check draw order and "
                      "binding scope.",
                      gen->use_def.binding_id);
      /* Map USE schema's width / signedness / float to expected BKIND.
      ** Mismatch with the binding's stored kind is a hard abort —
      ** reading an i64 binding through HEGEL_USE (int) would silently
      ** truncate, which is exactly the bug we want to catch loudly. */
      if (gen->use_def.is_float) {
        expected = (gen->use_def.width == 8) ? HEGEL__BKIND_DOUBLE
                                             : HEGEL__BKIND_FLOAT;
      } else if (gen->use_def.width == 8) {
        expected = gen->use_def.is_signed ? HEGEL__BKIND_I64
                                          : HEGEL__BKIND_U64;
      } else {
        expected = HEGEL__BKIND_INT;
      }
      if (kind != expected)
        hegel__abort ("hegel__draw_field: HEGEL_USE(id=%d) kind mismatch — "
                      "binding stored as %d, USE expects %d.  Check that "
                      "HEGEL_LET inner kind matches the HEGEL_USE_* variant.",
                      gen->use_def.binding_id, kind, expected);
      switch (expected) {
        case HEGEL__BKIND_INT:
          *(int *) dst = (int) value;
          break;
        case HEGEL__BKIND_I64:
          *(int64_t *) dst = value;
          break;
        case HEGEL__BKIND_U64: {
          uint64_t u;
          memcpy (&u, &value, sizeof (u));
          *(uint64_t *) dst = u;
          break;
        }
        case HEGEL__BKIND_FLOAT: {
          float f;
          memcpy (&f, &value, sizeof (f));
          *(float *) dst = f;
          break;
        }
        case HEGEL__BKIND_DOUBLE: {
          double d;
          memcpy (&d, &value, sizeof (d));
          *(double *) dst = d;
          break;
        }
      }
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_SCALAR;
      shape->schema = gen;
      return (shape);
    }

    case HEGEL_SCH_USE_AT: {
      int                kind;
      int64_t            value;
      int                arr_len;
      hegel__draw_ctx *  decl_scope;
      int *              arr;
      int                idx;

      if (ctx == NULL)
        hegel__abort ("hegel__draw_field: HEGEL_USE_AT reached without a "
                      "draw ctx — only valid inside a HEGEL_STRUCT.");
      if (!hegel__draw_ctx_lookup_full (ctx, gen->use_at_def.binding_id,
                                        &kind, &value, &arr_len, &decl_scope))
        hegel__abort ("hegel__draw_field: HEGEL_USE_AT(id=%d) references a "
                      "binding that has not been HEGEL_LET_ARR'd in any "
                      "enclosing HEGEL_STRUCT.",
                      gen->use_at_def.binding_id);
      if (kind != HEGEL__BKIND_INT_ARR)
        hegel__abort ("hegel__draw_field: HEGEL_USE_AT(id=%d) expected an "
                      "array binding (HEGEL_LET_ARR), got kind=%d.  USE_AT "
                      "needs a LET_ARR; plain LET goes through HEGEL_USE.",
                      gen->use_at_def.binding_id, kind);

      idx = decl_scope->current_index;
      if (idx < 0)
        hegel__abort ("hegel__draw_field: HEGEL_USE_AT(id=%d) is not "
                      "currently inside an HEGEL_ARR_OF iteration of the "
                      "scope where the binding was declared.  USE_AT only "
                      "makes sense as an element/length inside an ARR_OF "
                      "running in that scope.",
                      gen->use_at_def.binding_id);
      if (idx >= arr_len)
        hegel__abort ("hegel__draw_field: HEGEL_USE_AT(id=%d) index=%d out "
                      "of bounds (array len=%d).  The current ARR_OF "
                      "iteration is longer than the bound array — make "
                      "the surrounding ARR_OF use the same length source.",
                      gen->use_at_def.binding_id, idx, arr_len);

      arr = (int *) (intptr_t) value;
      *(int *) dst = arr[idx];
      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_SCALAR;
      shape->schema = gen;
      return (shape);
    }

    case HEGEL_SCH_USE_PATH: {
      int *              path;
      int                len;
      int                p;
      int                skip;
      int                binding_id;
      int                has_index;
      hegel__draw_ctx *  start;
      int                kind;
      int64_t            value;
      int                arr_len;
      hegel__draw_ctx *  decl_scope;
      int                i;

      if (ctx == NULL)
        hegel__abort ("hegel__draw_field: HEGEL_USE_PATH reached without "
                      "a draw ctx — only valid inside a HEGEL_STRUCT.");

      path = gen->use_path_def.path;
      len  = gen->use_path_def.path_len;

      /* Re-parse the path each draw — it's tiny (≤ 64 ints) and the
      ** code stays self-contained.  Validation in the constructor
      ** already ensured the shape is well-formed. */
      p    = 0;
      skip = 0;
      while (p < len && path[p] == HEGEL_PARENT) { skip ++; p ++; }
      binding_id = path[p ++];
      has_index  = (p < len && path[p] == HEGEL_INDEX_HERE) ? 1 : 0;

      /* Skip N scopes outward before starting the lookup. */
      start = ctx;
      for (i = 0; i < skip; i ++) {
        if (start->parent == NULL)
          hegel__abort ("hegel__draw_field: HEGEL_USE_PATH walked off the "
                        "top of the scope chain (HEGEL_PARENT count=%d "
                        "exceeds nesting depth at this site).", skip);
        start = start->parent;
      }

      if (!hegel__draw_ctx_lookup_full (start, binding_id, &kind, &value,
                                        &arr_len, &decl_scope))
        hegel__abort ("hegel__draw_field: HEGEL_USE_PATH(id=%d, skip=%d) "
                      "binding not found at or above the requested scope.",
                      binding_id, skip);

      if (has_index) {
        int idx;
        if (kind != HEGEL__BKIND_INT_ARR)
          hegel__abort ("hegel__draw_field: HEGEL_USE_PATH with "
                        "HEGEL_INDEX_HERE expects an array binding "
                        "(HEGEL_LET_ARR), got kind=%d for id=%d",
                        kind, binding_id);
        idx = decl_scope->current_index;
        if (idx < 0)
          hegel__abort ("hegel__draw_field: HEGEL_USE_PATH(id=%d) "
                        "HEGEL_INDEX_HERE requires the binding's scope to "
                        "be in an HEGEL_ARR_OF iteration.", binding_id);
        if (idx >= arr_len)
          hegel__abort ("hegel__draw_field: HEGEL_USE_PATH(id=%d) index=%d "
                        "out of bounds (array len=%d).",
                        binding_id, idx, arr_len);
        *(int *) dst = ((int *) (intptr_t) value)[idx];
      } else {
        if (kind != HEGEL__BKIND_INT)
          hegel__abort ("hegel__draw_field: HEGEL_USE_PATH(id=%d) without "
                        "HEGEL_INDEX_HERE expects an int scalar binding, "
                        "got kind=%d.  Add HEGEL_INDEX_HERE for arrays, or "
                        "use HEGEL_USE_*_I64/U64/etc. for wider scalars.",
                        binding_id, kind);
        *(int *) dst = (int) value;
      }

      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind = HEGEL_SHAPE_SCALAR;
      shape->schema = gen;
      return (shape);
    }

    case HEGEL_SCH_ARR_OF: {
      /* Evaluate the length schema into a local int by reusing the
      ** field-draw path with &local_len as the "parent".  HEGEL_USE
      ** reads from ctx, HEGEL_SCH_INTEGER draws fresh — both land as
      ** int at offset 0. */
      int             n = 0;
      size_t          esz;
      hegel_schema *  elem;
      void *          arr;
      hegel_shape *   len_shape;
      int             i;

      if (ctx == NULL)
        hegel__abort ("hegel__draw_field: HEGEL_ARR_OF requires a draw "
                      "ctx — only valid inside HEGEL_STRUCT.");

      /* Length must produce int-width.  HEGEL_USE_I64 / U64 / DOUBLE
      ** would overrun the local int — reject up front with a clear
      ** message.  Drawn-int / CONST_INT / int-width USE all OK. */
      if (gen->arr_of_def.length->kind == HEGEL_SCH_USE
          && (gen->arr_of_def.length->use_def.is_float
              || gen->arr_of_def.length->use_def.width != (int) sizeof (int)))
        hegel__abort ("hegel__draw_field: HEGEL_ARR_OF length must be "
                      "int-width — got HEGEL_USE_* with width=%d "
                      "is_float=%d.  Use the plain HEGEL_USE for lengths.",
                      gen->arr_of_def.length->use_def.width,
                      (int) gen->arr_of_def.length->use_def.is_float);

      len_shape = hegel__draw_field (tc, gen->arr_of_def.length,
                                     &n, 0, depth, ctx);
      hegel_shape_free (len_shape);     /* length lives on the stack */

      if (n < 0)
        hegel__abort ("hegel__draw_field: HEGEL_ARR_OF length produced "
                      "negative n=%d", n);

      elem = gen->arr_of_def.elem;
      esz  = hegel__elem_size (elem);
      arr  = calloc ((size_t) n + 1, esz);

      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind   = HEGEL_SHAPE_ARRAY;
      shape->schema = gen;
      shape->owned  = arr;
      shape->array_shape.len   = n;
      shape->array_shape.elems = (hegel_shape **) calloc (
          (size_t) n + 1, sizeof (hegel_shape *));

      *(void **) dst = arr;

      /* Save / restore ctx->current_index so HEGEL_USE_AT inside the
      ** elements can read "the i of THIS ARR_OF in this scope".  A
      ** nested ARR_OF in an inner struct gets its own ctx and does
      ** not clobber ours. */
      int saved_idx = ctx->current_index;
      hegel_start_span (tc, HEGEL_SPAN_LIST);
      for (i = 0; i < n; i ++) {
        ctx->current_index = i;
        hegel_start_span (tc, HEGEL_SPAN_LIST_ELEMENT);
        if (elem->kind == HEGEL_SCH_INTEGER
            || elem->kind == HEGEL_SCH_MAP_INT
            || elem->kind == HEGEL_SCH_FILTER_INT
            || elem->kind == HEGEL_SCH_FLAT_MAP_INT) {
          /* All these kinds produce an int via hegel__draw_integer_into. */
          hegel__draw_integer_into (tc, elem,
              (char *) arr + (size_t) i * esz);
          shape->array_shape.elems[i] = (hegel_shape *) calloc (
              1, sizeof (hegel_shape));
          shape->array_shape.elems[i]->kind   = HEGEL_SHAPE_SCALAR;
          shape->array_shape.elems[i]->schema = elem;
        } else if (elem->kind == HEGEL_SCH_STRUCT) {
          /* Array of pointers to separately-allocated structs.  Pass
          ** `ctx` as the element struct's parent so its HEGEL_USE
          ** can reach outer bindings; the element's own HEGEL_LETs
          ** get a fresh per-element ctx inside hegel__draw_struct,
          ** so bindings in different elements don't collide. */
          void * child;
          shape->array_shape.elems[i] =
              hegel__draw_struct (tc, elem, &child, depth - 1, ctx);
          ((void **) arr)[i] = child;
        } else if (elem->kind == HEGEL_SCH_ONE_OF_STRUCT) {
          /* Each element: pick a case, allocate its struct, store raw
          ** void*.  hegel__draw_alloc handles the ONE_OF_STRUCT path. */
          void * child;
          shape->array_shape.elems[i] =
              hegel__draw_alloc (tc, elem, &child, depth, ctx);
          ((void **) arr)[i] = child;
        } else if (elem->kind == HEGEL_SCH_OPTIONAL_PTR) {
          /* Per-element NULL-or-drawn-inner.  Used for
          ** HEGEL_ARR_OF(length, HEGEL_SELF()) n-ary trees. */
          hegel_shape * opt_sh = (hegel_shape *) calloc (
              1, sizeof (hegel_shape));
          int           present;

          opt_sh->kind   = HEGEL_SHAPE_OPTIONAL;
          opt_sh->schema = elem;

          hegel_start_span (tc, HEGEL_SPAN_OPTIONAL);
          present = hegel_draw_int (tc, 0, 1);

          if (present && depth == 0) {
            hegel_health_fail ("max recursion depth reached in "
                               "HEGEL_ARR_OF element — raise "
                               "max_depth or terminate recursion.");
            /* unreachable */
          }

          if (present) {
            void * child;
            opt_sh->optional_shape.inner =
                hegel__draw_alloc (tc, elem->optional_ptr.inner,
                                   &child, depth - 1, ctx);
            opt_sh->optional_shape.is_some =
                (opt_sh->optional_shape.inner != NULL);
            ((void **) arr)[i] =
                opt_sh->optional_shape.is_some ? child : NULL;
          } else {
            opt_sh->optional_shape.is_some = 0;
            opt_sh->optional_shape.inner   = NULL;
            ((void **) arr)[i] = NULL;
          }

          hegel_stop_span (tc, 0);
          shape->array_shape.elems[i] = opt_sh;
        } else {
          hegel__abort ("hegel__draw_field: HEGEL_ARR_OF supports "
                        "INTEGER, STRUCT, ONE_OF_STRUCT, and "
                        "OPTIONAL_PTR elements (got kind=%d)",
                        (int) elem->kind);
        }
        hegel_stop_span (tc, 0);
      }
      hegel_stop_span (tc, 0);
      ctx->current_index = saved_idx;

      return (shape);
    }

    case HEGEL_SCH_LEN_PREFIXED:
    case HEGEL_SCH_TERMINATED: {
      /* Both kinds share the same shape:
      **   - Draw a length n via the length schema
      **   - Allocate a buffer of (n+1) * elem_size bytes
      **   - LEN_PREFIXED: write n at slot 0, draw elements at slots 1..n
      **   - TERMINATED:  draw elements at slots 0..n-1, write sentinel at slot n
      ** Both place the buffer pointer in the parent slot.  */
      int             n = 0;
      int             prefix;
      int             elem_w;
      int             elem_signed;
      int64_t         max_repr;
      int64_t         sentinel = 0;
      size_t          esz;
      hegel_schema *  length_sch;
      hegel_schema *  elem;
      void *          arr;
      hegel_shape *   len_shape;
      int             i;

      if (ctx == NULL)
        hegel__abort ("hegel__draw_field: HEGEL_LEN_PREFIXED_ARRAY / "
                      "HEGEL_TERMINATED_ARRAY require a draw ctx — only "
                      "valid inside HEGEL_STRUCT.");

      if (gen->kind == HEGEL_SCH_LEN_PREFIXED) {
        length_sch = gen->len_prefixed_def.length;
        elem       = gen->len_prefixed_def.elem;
        prefix     = 1;
      } else {
        length_sch = gen->terminated_def.length;
        elem       = gen->terminated_def.elem;
        sentinel   = gen->terminated_def.sentinel;
        prefix     = 0;
      }

      /* Length must produce int-width — same constraint as ARR_OF. */
      if (length_sch->kind == HEGEL_SCH_USE
          && (length_sch->use_def.is_float
              || length_sch->use_def.width != (int) sizeof (int)))
        hegel__abort ("hegel__draw_field: length must be int-width — got "
                      "HEGEL_USE_* with width=%d is_float=%d.",
                      length_sch->use_def.width,
                      (int) length_sch->use_def.is_float);

      len_shape = hegel__draw_field (tc, length_sch, &n, 0, depth, ctx);
      hegel_shape_free (len_shape);

      if (n < 0)
        hegel__abort ("hegel__draw_field: negative length n=%d", n);

      esz         = (size_t) elem->integer.width;
      elem_w      = elem->integer.width;
      elem_signed = elem->integer.is_signed;

      /* For LEN_PREFIXED only: drawn n must fit in elem type. */
      if (prefix) {
        if (elem_signed)
          max_repr = elem->integer.max_s;
        else
          max_repr = (elem->integer.max_u > (uint64_t) INT64_MAX)
                         ? INT64_MAX
                         : (int64_t) elem->integer.max_u;
        if ((int64_t) n > max_repr)
          hegel__abort ("HEGEL_LEN_PREFIXED_ARRAY: drawn length %d exceeds "
                        "elem type's max representable value %lld.  "
                        "Constrain the LET range to fit in the elem width.",
                        n, (long long) max_repr);
      }

      arr = calloc ((size_t) n + 1, esz);

      shape = (hegel_shape *) calloc (1, sizeof (hegel_shape));
      shape->kind   = HEGEL_SHAPE_ARRAY;
      shape->schema = gen;
      shape->owned  = arr;
      shape->array_shape.len   = n + 1;
      shape->array_shape.elems = (hegel_shape **) calloc (
          (size_t) n + 1, sizeof (hegel_shape *));

      *(void **) dst = arr;

      hegel_start_span (tc, HEGEL_SPAN_LIST);

      if (prefix) {
        /* Slot 0 = length, no draw — purely synthetic. */
        hegel__write_int_at (arr, elem_w, (int64_t) n);
        shape->array_shape.elems[0] = (hegel_shape *) calloc (
            1, sizeof (hegel_shape));
        shape->array_shape.elems[0]->kind   = HEGEL_SHAPE_SCALAR;
        shape->array_shape.elems[0]->schema = elem;
      }

      /* Drawn elements: slots [prefix .. prefix + n - 1]. */
      {
        int saved_idx = ctx->current_index;
        for (i = 0; i < n; i ++) {
          char * slot = (char *) arr + (size_t) (i + prefix) * esz;
          ctx->current_index = i;
          hegel_start_span (tc, HEGEL_SPAN_LIST_ELEMENT);
          hegel__draw_integer_into (tc, elem, slot);
          if (gen->kind == HEGEL_SCH_TERMINATED) {
            int64_t v = hegel__read_int_at (slot, elem_w, elem_signed);
            if (v == sentinel)
              hegel__abort ("HEGEL_TERMINATED_ARRAY: drawn element %lld at "
                            "index %d collides with sentinel %lld.  The "
                            "elem schema can produce the sentinel value — "
                            "constrain it (e.g. HEGEL_INT(1, 127) instead "
                            "of HEGEL_INT(0, 127) for null-terminated "
                            "strings).", (long long) v, i,
                            (long long) sentinel);
          }
          shape->array_shape.elems[i + prefix] = (hegel_shape *) calloc (
              1, sizeof (hegel_shape));
          shape->array_shape.elems[i + prefix]->kind   = HEGEL_SHAPE_SCALAR;
          shape->array_shape.elems[i + prefix]->schema = elem;
          hegel_stop_span (tc, 0);
        }
        ctx->current_index = saved_idx;
      }

      if (!prefix) {
        /* Slot n = sentinel, no draw — purely synthetic. */
        hegel__write_int_at ((char *) arr + (size_t) n * esz,
                             elem_w, sentinel);
        shape->array_shape.elems[n] = (hegel_shape *) calloc (
            1, sizeof (hegel_shape));
        shape->array_shape.elems[n]->kind   = HEGEL_SHAPE_SCALAR;
        shape->array_shape.elems[n]->schema = elem;
      }

      hegel_stop_span (tc, 0);

      return (shape);
    }
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
  /* hegel__draw_struct installs its own ctx — per-struct-instance
  ** scoping, so bindings in different struct instances (even of the
  ** same schema) stay independent.  NULL parent_ctx marks this as
  ** the root of the scope chain. */
  return (hegel__draw_struct (tc, g, out, max_depth, NULL));
}

hegel_shape *
hegel_schema_draw (hegel_testcase * tc, hegel_schema_t gen, void ** out)
{
  return (hegel_schema_draw_n (tc, gen, out, HEGEL_DEFAULT_MAX_DEPTH));
}

/* ---- Unified top-level entry: write at caller address ------------
**
** One signature for every kind.  Semantics branch on schema kind:
**
**   STRUCT:            allocate, fill, write the pointer at *addr,
**                      return a SHAPE_STRUCT that owns the allocation.
**   INTEGER / FLOAT /
**   TEXT / REGEX /
**   MAP_* / FILTER_* /
**   FLAT_MAP_* /
**   ONE_OF_SCALAR /
**   OPTIONAL_PTR /
**   UNION / _UNTAGGED /
**   VARIANT:           write the drawn value (scalar, string pointer,
**                      variant cluster, etc.) at `addr`; return shape
**                      for scalars (informational leaf) or composite
**                      shape for compound kinds.  Scalars write by
**                      value — no allocation, no further free needed
**                      beyond the returned leaf shape.
**
** Kinds explicitly rejected at top level (abort with a diagnostic):
**
**   ARRAY_INLINE:      len_offset is relative to a parent struct; at
**                      top level it would collide with the pointer
**                      slot at offset 0.  Wrap in a struct.
**   SELF:              only meaningful inside a recursive struct.
**   ONE_OF_STRUCT:     intended as an HEGEL_ARR_OF element or OPTIONAL
**                      inner.
**   BIND / USE /
**   ARR_OF:            need a parent struct for their draw ctx.
*/

hegel_shape *
hegel_schema_draw_at_n (hegel_testcase * tc, void * addr,
                        hegel_schema_t gen, int max_depth)
{
  hegel_schema * g = gen._raw;

  if (g == NULL || addr == NULL)
    return (NULL);

  switch (g->kind) {

    case HEGEL_SCH_STRUCT: {
      /* Allocating path — identical to hegel_schema_draw's existing
      ** semantics.  The caller's addr is treated as `void **` because
      ** what lands there is a pointer to the allocation. */
      void *         ptr = NULL;
      hegel_shape *  sh;
      sh = hegel__draw_struct (tc, g, &ptr, max_depth, NULL);
      *(void **) addr = ptr;
      return (sh);
    }

    case HEGEL_SCH_INTEGER:
    case HEGEL_SCH_FLOAT:
    case HEGEL_SCH_TEXT:
    case HEGEL_SCH_REGEX:
    case HEGEL_SCH_OPTIONAL_PTR:
    case HEGEL_SCH_UNION:
    case HEGEL_SCH_UNION_UNTAGGED:
    case HEGEL_SCH_VARIANT:
    case HEGEL_SCH_MAP_INT:
    case HEGEL_SCH_FILTER_INT:
    case HEGEL_SCH_FLAT_MAP_INT:
    case HEGEL_SCH_MAP_I64:
    case HEGEL_SCH_FILTER_I64:
    case HEGEL_SCH_FLAT_MAP_I64:
    case HEGEL_SCH_MAP_DOUBLE:
    case HEGEL_SCH_FILTER_DOUBLE:
    case HEGEL_SCH_FLAT_MAP_DOUBLE:
    case HEGEL_SCH_ONE_OF_SCALAR:
    case HEGEL_SCH_CONST_INT:
      /* Direct write at addr.  No binding ctx needed — these kinds
      ** don't touch the binding table. */
      return (hegel__draw_field (tc, g, addr, 0, max_depth, NULL));

    case HEGEL_SCH_ARRAY_INLINE:
      hegel__abort ("hegel_schema_draw_at: HEGEL_ARRAY_INLINE cannot be "
                    "drawn at top level — it needs a parent struct for "
                    "its length slot.  Wrap it in HEGEL_STRUCT.");
      break;

    case HEGEL_SCH_SELF:
      hegel__abort ("hegel_schema_draw_at: HEGEL_SELF is only valid "
                    "inside a recursive struct schema.");
      break;

    case HEGEL_SCH_ONE_OF_STRUCT:
      hegel__abort ("hegel_schema_draw_at: HEGEL_ONE_OF_STRUCT is a "
                    "pointer-producing element schema — use it as an "
                    "HEGEL_ARR_OF element or inside HEGEL_OPTIONAL.");
      break;

    case HEGEL_SCH_BIND:
    case HEGEL_SCH_USE:
    case HEGEL_SCH_LET_ARR:
    case HEGEL_SCH_USE_AT:
    case HEGEL_SCH_USE_PATH:
      hegel__abort ("hegel_schema_draw_at: HEGEL_LET / HEGEL_USE / "
                    "HEGEL_LET_ARR / HEGEL_USE_AT / HEGEL_USE_PATH only "
                    "make sense inside HEGEL_STRUCT — the binding scope "
                    "is the enclosing struct.");
      break;

    case HEGEL_SCH_ARR_OF:
      hegel__abort ("hegel_schema_draw_at: HEGEL_ARR_OF needs a parent "
                    "struct for its draw ctx and binding lookups — wrap "
                    "it in HEGEL_STRUCT.");
      break;
    case HEGEL_SCH_LEN_PREFIXED:
    case HEGEL_SCH_TERMINATED:
      hegel__abort ("hegel_schema_draw_at: HEGEL_LEN_PREFIXED_ARRAY / "
                    "HEGEL_TERMINATED_ARRAY need a parent struct for the "
                    "draw ctx and binding lookups — wrap in HEGEL_STRUCT.");
      break;
  }

  return (NULL);
}

hegel_shape *
hegel_schema_draw_at (hegel_testcase * tc, void * addr, hegel_schema_t gen)
{
  return (hegel_schema_draw_at_n (tc, addr, gen, HEGEL_DEFAULT_MAX_DEPTH));
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
    case HEGEL_SCH_BIND:
      hegel__schema_free_raw (s->bind_def.inner);
      break;
    case HEGEL_SCH_USE:
      /* Leaf — no owned children. */
      break;
    case HEGEL_SCH_LET_ARR:
      hegel__schema_free_raw (s->let_arr_def.length);
      hegel__schema_free_raw (s->let_arr_def.elem);
      break;
    case HEGEL_SCH_USE_AT:
      /* Leaf — no owned children. */
      break;
    case HEGEL_SCH_USE_PATH:
      free (s->use_path_def.path);
      break;
    case HEGEL_SCH_ARR_OF:
      hegel__schema_free_raw (s->arr_of_def.length);
      hegel__schema_free_raw (s->arr_of_def.elem);
      break;
    case HEGEL_SCH_CONST_INT:
      /* Leaf — no owned children. */
      break;
    case HEGEL_SCH_LEN_PREFIXED:
      hegel__schema_free_raw (s->len_prefixed_def.length);
      hegel__schema_free_raw (s->len_prefixed_def.elem);
      break;
    case HEGEL_SCH_TERMINATED:
      hegel__schema_free_raw (s->terminated_def.length);
      hegel__schema_free_raw (s->terminated_def.elem);
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
