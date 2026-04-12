/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
#ifndef HEGEL_GEN_H
#define HEGEL_GEN_H

/*
** Structured generation helpers for hegel-c.
**
** Three-layer architecture:
**   1. Schema (generator tree) — describes what to generate.
**      Built once by the user via hegel_schema_*() helpers.
**      The user-facing type is `hegel_schema_t`, a thin wrapper
**      around the internal node pointer.
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
** Example (binary tree with optional label + children):
**
**   typedef struct Tree {
**     int val;
**     char *label;
**     struct Tree *left, *right;
**   } Tree;
**
**   hegel_schema_t s = hegel_schema_struct(sizeof(Tree),
**       HEGEL_INT(Tree, val, -1000, 1000),
**       HEGEL_OPTIONAL(Tree, label, hegel_schema_text(0, 8)),
**       HEGEL_SELF(Tree, left),
**       HEGEL_SELF(Tree, right));
**
** Notice: no trailing NULL.  The variadic macros inject a sentinel
** (H_END) internally, so user code just lists fields.
*/

#include "hegel_c.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
** Internal: the raw schema node
** ================================================================
**
** This is the actual data structure.  Users never touch it directly
** — they use `hegel_schema_t` (below), which wraps a pointer to it. */

typedef enum {
  HEGEL_SCH_INTEGER,
  HEGEL_SCH_FLOAT,
  HEGEL_SCH_TEXT,
  HEGEL_SCH_STRUCT,
  HEGEL_SCH_OPTIONAL_PTR,
  HEGEL_SCH_ARRAY,
  HEGEL_SCH_ARRAY_INLINE,
  HEGEL_SCH_UNION,
  HEGEL_SCH_UNION_UNTAGGED,
  HEGEL_SCH_VARIANT,
  HEGEL_SCH_SELF
} hegel_schema_kind;

typedef struct hegel_schema {
  hegel_schema_kind       kind;
  size_t                  offset;
  int                     refcount;
  union {
    struct {
      int               width;
      int               is_signed;
      int64_t           min_s;
      int64_t           max_s;
      uint64_t          min_u;
      uint64_t          max_u;
    }                                                  integer;
    struct {
      int               width;
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
      size_t            len_offset;
      struct hegel_schema * elem;
      size_t            elem_size;
      int               min_len;
      int               max_len;
    }                                                  array_inline_def;
    struct {
      size_t            tag_offset;
      int               n_cases;
      struct hegel_schema *** cases;
    }                                                  union_def;
    struct {
      size_t            tag_offset;
      size_t            ptr_offset;
      int               n_cases;
      struct hegel_schema ** cases;
    }                                                  variant_def;
    struct {
      struct hegel_schema * target;
    }                                                  self_ref;
  };
} hegel_schema;

/* ================================================================
** User-facing: hegel_schema_t wrapper
** ================================================================
**
** All public API works in terms of `hegel_schema_t`, a thin struct
** containing a single pointer to the internal node.  This gives a
** consistent typed API — no raw pointers in user code.
**
** === Why a struct wrapper instead of a typedef alias? ===
**
** The obvious alternative is:
**
**     typedef hegel_schema *hegel_schema_t;   // a plain alias
**
** …but a C typedef is just an alias.  `hegel_schema_t` and
** `hegel_schema *` would be the SAME type to the compiler, and you
** could freely mix them with any other pointer.  Zero type-system
** benefit.
**
** By wrapping in a struct with one field, `hegel_schema_t` becomes
** a DISTINCT nominal type.  The compiler refuses to convert between
** `hegel_schema_t` and `hegel_schema *` (or any other type) without
** explicit `.` access.  That's what gives us compile-time separation.
**
** The cost: zero at runtime.  `sizeof(hegel_schema_t) == sizeof(void*)`;
** passing one by value compiles to passing a single register.  The
** struct wrapping is erased entirely by the optimizer.  This is the
** same pattern as Rust's `struct Wrapper(Inner);` newtype idiom —
** zero-cost at runtime, distinct at compile time.
**
** Users should never need to touch `_raw` — it's the library's
** internal handle, exposed only for the variadic macros to use.
** Normal usage is: assign, pass to functions, free.
**
** === The H_END sentinel ===
**
** Variadic macros like `hegel_schema_struct(size, ...)` build a
** compound literal array of `hegel_schema_t`.  They need a terminator
** the iterator can recognize — but `NULL` has type `(void *)0`, which
** isn't a valid `hegel_schema_t`.  So we define `H_END` as an explicit
** `hegel_schema_t` sentinel with `_raw == NULL`, and the macros append
** it automatically.  Users never write trailing `NULL`.
*/

typedef struct { hegel_schema * _raw; } hegel_schema_t;

#define H_END ((hegel_schema_t){ NULL })

/* ================================================================
** Shape (drawn-value tree)
** ================================================================ */

typedef enum {
  HEGEL_SHAPE_SCALAR,
  HEGEL_SHAPE_TEXT,
  HEGEL_SHAPE_STRUCT,
  HEGEL_SHAPE_OPTIONAL,
  HEGEL_SHAPE_ARRAY,
  HEGEL_SHAPE_VARIANT
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
    struct {
      int               tag;
      struct hegel_shape * inner;
    }                                                  variant_shape;
  };
} hegel_shape;

/* ================================================================
** Refcount
** ================================================================
**
** Every schema starts life with refcount=1.  Passing a schema to a
** parent TRANSFERS that reference — the parent does not bump.  For
** sharing across multiple parents, call hegel_schema_ref() to add a
** reference BEFORE passing:
**
**     hegel_schema_t color = hegel_schema_struct(...);
**     hegel_schema_ref(color);
**     hegel_schema_t p = hegel_schema_struct(...,
**         HEGEL_ARRAY_INLINE(..., color, ...));
**     hegel_schema_t s = hegel_schema_struct(...,
**         HEGEL_ARRAY_INLINE(..., color, ...));
**     hegel_schema_free(p);
**     hegel_schema_free(s); */

hegel_schema_t hegel_schema_ref (hegel_schema_t s);

/* ================================================================
** Schema constructors — integers
** ================================================================ */

hegel_schema_t hegel_schema_i8    (void);
hegel_schema_t hegel_schema_i8_range    (int64_t lo, int64_t hi);
hegel_schema_t hegel_schema_i16   (void);
hegel_schema_t hegel_schema_i16_range   (int64_t lo, int64_t hi);
hegel_schema_t hegel_schema_i32   (void);
hegel_schema_t hegel_schema_i32_range   (int64_t lo, int64_t hi);
hegel_schema_t hegel_schema_i64   (void);
hegel_schema_t hegel_schema_i64_range   (int64_t lo, int64_t hi);
hegel_schema_t hegel_schema_int   (void);
hegel_schema_t hegel_schema_int_range   (int64_t lo, int64_t hi);
hegel_schema_t hegel_schema_long  (void);
hegel_schema_t hegel_schema_long_range  (int64_t lo, int64_t hi);
hegel_schema_t hegel_schema_u8    (void);
hegel_schema_t hegel_schema_u8_range    (uint64_t lo, uint64_t hi);
hegel_schema_t hegel_schema_u16   (void);
hegel_schema_t hegel_schema_u16_range   (uint64_t lo, uint64_t hi);
hegel_schema_t hegel_schema_u32   (void);
hegel_schema_t hegel_schema_u32_range   (uint64_t lo, uint64_t hi);
hegel_schema_t hegel_schema_u64   (void);
hegel_schema_t hegel_schema_u64_range   (uint64_t lo, uint64_t hi);

/* Internal: set a schema's offset.  Called by _at macros. */
hegel_schema_t hegel__at (hegel_schema_t s, size_t offset);

#define hegel_schema_i8_at(off)                  hegel__at (hegel_schema_i8 (), (off))
#define hegel_schema_i8_range_at(off, lo, hi)    hegel__at (hegel_schema_i8_range ((lo), (hi)), (off))
#define hegel_schema_i16_at(off)                 hegel__at (hegel_schema_i16 (), (off))
#define hegel_schema_i16_range_at(off, lo, hi)   hegel__at (hegel_schema_i16_range ((lo), (hi)), (off))
#define hegel_schema_i32_at(off)                 hegel__at (hegel_schema_i32 (), (off))
#define hegel_schema_i32_range_at(off, lo, hi)   hegel__at (hegel_schema_i32_range ((lo), (hi)), (off))
#define hegel_schema_i64_at(off)                 hegel__at (hegel_schema_i64 (), (off))
#define hegel_schema_i64_range_at(off, lo, hi)   hegel__at (hegel_schema_i64_range ((lo), (hi)), (off))
#define hegel_schema_int_at(off)                 hegel__at (hegel_schema_int (), (off))
#define hegel_schema_int_range_at(off, lo, hi)   hegel__at (hegel_schema_int_range ((lo), (hi)), (off))
#define hegel_schema_long_at(off)                hegel__at (hegel_schema_long (), (off))
#define hegel_schema_long_range_at(off, lo, hi)  hegel__at (hegel_schema_long_range ((lo), (hi)), (off))
#define hegel_schema_u8_at(off)                  hegel__at (hegel_schema_u8 (), (off))
#define hegel_schema_u8_range_at(off, lo, hi)    hegel__at (hegel_schema_u8_range ((lo), (hi)), (off))
#define hegel_schema_u16_at(off)                 hegel__at (hegel_schema_u16 (), (off))
#define hegel_schema_u16_range_at(off, lo, hi)   hegel__at (hegel_schema_u16_range ((lo), (hi)), (off))
#define hegel_schema_u32_at(off)                 hegel__at (hegel_schema_u32 (), (off))
#define hegel_schema_u32_range_at(off, lo, hi)   hegel__at (hegel_schema_u32_range ((lo), (hi)), (off))
#define hegel_schema_u64_at(off)                 hegel__at (hegel_schema_u64 (), (off))
#define hegel_schema_u64_range_at(off, lo, hi)   hegel__at (hegel_schema_u64_range ((lo), (hi)), (off))

/* ================================================================
** Schema constructors — floats
** ================================================================ */

hegel_schema_t hegel_schema_float        (void);
hegel_schema_t hegel_schema_float_range  (double lo, double hi);
hegel_schema_t hegel_schema_double       (void);
hegel_schema_t hegel_schema_double_range (double lo, double hi);

#define hegel_schema_float_at(off)                hegel__at (hegel_schema_float (), (off))
#define hegel_schema_float_range_at(off, lo, hi)  hegel__at (hegel_schema_float_range ((lo), (hi)), (off))
#define hegel_schema_double_at(off)               hegel__at (hegel_schema_double (), (off))
#define hegel_schema_double_range_at(off, lo, hi) hegel__at (hegel_schema_double_range ((lo), (hi)), (off))

/* ================================================================
** Schema constructors — text, optional, array, struct, self, variant
** ================================================================ */

hegel_schema_t hegel_schema_text_at (size_t offset, int min_len, int max_len);
hegel_schema_t hegel_schema_text    (int min_len, int max_len);

hegel_schema_t hegel_schema_optional_ptr_at (size_t offset, hegel_schema_t inner);

hegel_schema_t hegel_schema_array_at (size_t ptr_offset, size_t len_offset,
                                      hegel_schema_t elem,
                                      int min_len, int max_len);

hegel_schema_t hegel_schema_array_inline_at (size_t ptr_offset, size_t len_offset,
                                             hegel_schema_t elem,
                                             size_t elem_size,
                                             int min_len, int max_len);

/* Internal: called by HEGEL_UNION / HEGEL_UNION_UNTAGGED macros.
** case_list is a NULL-terminated array of H_END-terminated
** hegel_schema_t arrays (one per variant). */
hegel_schema_t hegel__union (size_t tag_offset, hegel_schema_kind kind,
                             hegel_schema_t ** case_list);

hegel_schema_t hegel_schema_variant_v (size_t tag_offset, size_t ptr_offset,
                                       hegel_schema_t * case_list);

hegel_schema_t hegel_schema_self (void);

/* Variadic struct constructor.  Terminated by H_END (added by macro). */
hegel_schema_t hegel_schema_struct_v (size_t size, hegel_schema_t * field_list);

#define hegel_schema_struct(size, ...)                          \
  hegel_schema_struct_v ((size),                                \
      (hegel_schema_t[]){ __VA_ARGS__, H_END })

/* ================================================================
** Convenience macros — arg-count overloading
** ================================================================ */

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

/* Text + structural macros */
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

#define HEGEL_ARRAY_INLINE(type, ptr_field, len_field, elem, elem_sz, lo, hi) \
  hegel_schema_array_inline_at (offsetof (type, ptr_field),    \
      offsetof (type, len_field), (elem), (elem_sz), (lo), (hi))

/* HEGEL_CASE wraps a variant case's fields in an H_END-terminated array. */
#define HEGEL_CASE(...) (hegel_schema_t[]){ __VA_ARGS__, H_END }

#define HEGEL_UNION(type, tag_field, ...) \
  hegel__union (offsetof (type, tag_field), HEGEL_SCH_UNION, \
      (hegel_schema_t *[]){ __VA_ARGS__, NULL })

#define HEGEL_UNION_UNTAGGED(...) \
  hegel__union ((size_t) -1, HEGEL_SCH_UNION_UNTAGGED, \
      (hegel_schema_t *[]){ __VA_ARGS__, NULL })

#define HEGEL_VARIANT(type, tag_field, ptr_field, ...) \
  hegel_schema_variant_v (offsetof (type, tag_field),  \
      offsetof (type, ptr_field),                      \
      (hegel_schema_t[]){ __VA_ARGS__, H_END })

/* ================================================================
** Draw and free
** ================================================================ */

#define HEGEL_DEFAULT_MAX_DEPTH 5

hegel_shape * hegel_schema_draw_n (hegel_testcase * tc, hegel_schema_t gen,
                                   void ** out, int max_depth);

hegel_shape * hegel_schema_draw   (hegel_testcase * tc, hegel_schema_t gen,
                                   void ** out);

void hegel_shape_free  (hegel_shape * s);
void hegel_schema_free (hegel_schema_t s);

/* ================================================================
** Shape accessors
** ================================================================ */

int           hegel_shape_tag       (hegel_shape * s);
int           hegel_shape_array_len (hegel_shape * s);
int           hegel_shape_is_some   (hegel_shape * s);
hegel_shape * hegel_shape_field     (hegel_shape * s, int index);

#ifdef __cplusplus
}
#endif

#endif /* HEGEL_GEN_H */
