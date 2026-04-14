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
** === Schemas vs field bindings ===
**
** A schema describes WHAT to generate (an int in [0,100], a text
** of length 0..8, a struct with these fields).  A schema is a
** pure value — it has no position, no "where does this write to."
**
** A field binding pairs a schema with an offset into a parent
** struct.  The HEGEL_INT / HEGEL_TEXT / HEGEL_OPTIONAL / …
** macros produce bindings, not schemas.  `hegel_schema_struct`
** takes a variadic list of bindings; each binding tells it
** "draw from this schema, then write the result at this offset
** inside the parent struct."
**
** Because schemas are pure values, the same schema can be
** reused at different positions (bump refcount via
** hegel_schema_ref() before the second use).
**
** Notice: no trailing NULL.  The variadic macros inject a sentinel
** (H_END / H_END_BINDING) internally, so user code just lists fields.
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
  HEGEL_SCH_REGEX,           /* text constrained by a regex pattern  */
  HEGEL_SCH_STRUCT,
  HEGEL_SCH_OPTIONAL_PTR,
  HEGEL_SCH_ARRAY,
  HEGEL_SCH_ARRAY_INLINE,
  HEGEL_SCH_UNION,
  HEGEL_SCH_UNION_UNTAGGED,
  HEGEL_SCH_VARIANT,
  HEGEL_SCH_ONE_OF_STRUCT,
  HEGEL_SCH_MAP_INT,
  HEGEL_SCH_FILTER_INT,
  HEGEL_SCH_FLAT_MAP_INT,
  HEGEL_SCH_MAP_I64,
  HEGEL_SCH_FILTER_I64,
  HEGEL_SCH_FLAT_MAP_I64,
  HEGEL_SCH_MAP_DOUBLE,
  HEGEL_SCH_FILTER_DOUBLE,
  HEGEL_SCH_FLAT_MAP_DOUBLE,
  HEGEL_SCH_ONE_OF_SCALAR,   /* pick one of several scalar schemas   */
  HEGEL_SCH_SELF
} hegel_schema_kind;

/* Forward declarations + typedefs so wrapper types can be defined
** before the internal struct body. */
typedef struct hegel_schema hegel_schema;
typedef struct { hegel_schema * _raw; } hegel_schema_t;

/* A field binding: "draw from `schema`, write result at `offset`
** inside the parent composite."  Parent struct schemas own a
** flat array of these.  `offset` is a byte offset.  `schema` is
** refcount-owned by the binding: when the enclosing composite is
** freed, each binding's schema is refcount-decremented.
**
** Passing a schema to `hegel__bind` transfers the reference —
** if you want to reuse a schema at two different offsets, call
** hegel_schema_ref() before the second hegel__bind(). */
typedef struct hegel_field_binding {
  size_t          offset;
  hegel_schema *  schema;
} hegel_field_binding;

/* Value-type wrapper for bindings.  Unlike `hegel_schema_t`, which
** wraps a pointer, a binding IS a value (offset + pointer) so we
** use the struct directly — no extra indirection. */
typedef struct hegel_field_binding hegel_binding_t;

/* ================================================================
** Layout entries — the positional API
** ================================================================
**
** The HEGEL_STRUCT / HEGEL_INT / HEGEL_TEXT / … macros don't take
** field names or offsetof calls.  Instead, each macro produces a
** `hegel_layout_entry` describing:
**   - the schema to draw from
**   - how many bytes this entry occupies in the parent struct
**   - what alignment the parent needs to give it
**
** `HEGEL_STRUCT(T, ...)` walks the entry list at runtime, computes
** each entry's offset using the same rules the C compiler uses for
** struct layout (each field starts at the smallest offset ≥
** previous_end that's aligned to the field's required alignment,
** total size rounds up to `alignof(T)`), fixes up kind-specific
** sub-offsets inside each schema (e.g. `array_def.len_offset`),
** builds bindings, and hands them to `hegel_schema_struct_v`.
**
** A runtime assertion checks that the computed total == `sizeof(T)`.
** If they disagree, the user's struct fields don't match the macro
** list in order or type — abort with a diagnostic.
**
** Some entries occupy **two slots** in the parent:
**   - HEGEL_ARRAY / HEGEL_ARRAY_INLINE: pointer slot + int count slot
**   - HEGEL_UNION: int tag slot + union body slot
**   - HEGEL_VARIANT: int tag slot + void* slot
** Single-slot entries leave `slot1_size == 0`. */

typedef enum {
  HEGEL_LAY_SIMPLE,          /* 1 slot, schema writes at entry base */
  HEGEL_LAY_ARRAY,           /* 2 slots: void*, int count */
  HEGEL_LAY_ARRAY_INLINE,    /* 2 slots: void*, int count */
  HEGEL_LAY_UNION,           /* 2 slots: int tag, union body */
  HEGEL_LAY_UNION_UNTAGGED,  /* 1 slot:  union body only */
  HEGEL_LAY_VARIANT          /* 2 slots: int tag, void* ptr */
} hegel_layout_kind;

typedef struct hegel_layout_entry {
  hegel_layout_kind  kind;
  hegel_schema *     schema;
  size_t             slot0_size;
  size_t             slot0_align;
  size_t             slot1_size;   /* 0 → single-slot entry */
  size_t             slot1_align;
  /* For UNION / UNION_UNTAGGED, each case's fields were laid out
  ** relative to "body base 0" by the HEGEL_CASE macros.  The struct
  ** layout pass shifts those offsets by the computed body base. */
} hegel_layout_entry;

#define HEGEL_LAYOUT_END ((hegel_layout_entry){ 0, NULL, 0, 0, 0, 0 })

/* Driver: compute offsets, fix up schemas, build bindings, assert
** `sizeof(T) == computed_total`, and return the finished schema. */
hegel_schema_t hegel__struct_build (size_t declared_size,
                                    size_t declared_align,
                                    hegel_layout_entry * entries);

#define HEGEL_STRUCT(T, ...) \
  hegel__struct_build (sizeof (T), _Alignof (T), \
      (hegel_layout_entry[]){ __VA_ARGS__, HEGEL_LAYOUT_END })

struct hegel_schema {
  hegel_schema_kind       kind;
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
      int               n_bindings;
      hegel_field_binding * bindings;  /* flat array */
    }                                                  struct_def;
    struct {
      struct hegel_schema * inner;
    }                                                  optional_ptr;
    struct {
      size_t            len_offset;    /* relative to parent base */
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
      /* Each case is a NULL-terminated array of bindings
      ** (terminator: a binding with schema == NULL). */
      hegel_field_binding ** cases;
    }                                                  union_def;
    struct {
      size_t            tag_offset;
      size_t            ptr_offset;
      int               n_cases;
      struct hegel_schema ** cases;
    }                                                  variant_def;
    struct {
      struct hegel_schema * source;
      int (*fn)(int value, void *ctx);
      void *  ctx;
    }                                                  map_int_def;
    struct {
      struct hegel_schema * source;
      int (*pred)(int value, void *ctx);
      void *  ctx;
    }                                                  filter_int_def;
    struct {
      struct hegel_schema * source;
      /* Callback returns a fresh schema that produces an int.
      ** The returned schema is consumed (freed) after each draw. */
      hegel_schema_t (*fn)(int value, void *ctx);
      void *  ctx;
    }                                                  flat_map_int_def;
    struct {
      struct hegel_schema * source;
      int64_t (*fn)(int64_t value, void *ctx);
      void *  ctx;
    }                                                  map_i64_def;
    struct {
      struct hegel_schema * source;
      int (*pred)(int64_t value, void *ctx);
      void *  ctx;
    }                                                  filter_i64_def;
    struct {
      struct hegel_schema * source;
      hegel_schema_t (*fn)(int64_t value, void *ctx);
      void *  ctx;
    }                                                  flat_map_i64_def;
    struct {
      struct hegel_schema * source;
      double (*fn)(double value, void *ctx);
      void *  ctx;
    }                                                  map_double_def;
    struct {
      struct hegel_schema * source;
      int (*pred)(double value, void *ctx);
      void *  ctx;
    }                                                  filter_double_def;
    struct {
      struct hegel_schema * source;
      hegel_schema_t (*fn)(double value, void *ctx);
      void *  ctx;
    }                                                  flat_map_double_def;
    struct {
      /* Pick one of several scalar schemas.  All cases must be
      ** INTEGER or FLOAT; at draw time we dispatch on the chosen
      ** case's kind/width.  The user is responsible for making
      ** sure all cases write to the same C type (e.g., all int). */
      int               n_cases;
      struct hegel_schema ** cases;
    }                                                  one_of_scalar_def;
    struct {
      char *            pattern;   /* malloc'd copy */
      int               capacity;  /* output buffer size */
    }                                                  regex_def;
    struct {
      struct hegel_schema * target;
    }                                                  self_ref;
  };
};

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
** struct wrapping is erased entirely by the optimizer.
**
** === The H_END / H_END_BINDING sentinels ===
**
** Variadic macros build compound-literal arrays terminated by a
** zeroed sentinel.  H_END terminates schema arrays; H_END_BINDING
** terminates binding arrays.  Users never write trailing NULL — the
** macros append the sentinel automatically.
*/

#define H_END         ((hegel_schema_t){ NULL })
#define H_END_BINDING ((hegel_binding_t){ 0, NULL })

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
** sharing across multiple parents (or multiple positions via
** bindings), call hegel_schema_ref() to add a reference BEFORE
** passing:
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
** Schema constructors — integers (pure values, no offset)
** ================================================================ */

hegel_schema_t hegel_schema_i8          (void);
hegel_schema_t hegel_schema_i8_range    (int64_t lo, int64_t hi);
hegel_schema_t hegel_schema_i16         (void);
hegel_schema_t hegel_schema_i16_range   (int64_t lo, int64_t hi);
hegel_schema_t hegel_schema_i32         (void);
hegel_schema_t hegel_schema_i32_range   (int64_t lo, int64_t hi);
hegel_schema_t hegel_schema_i64         (void);
hegel_schema_t hegel_schema_i64_range   (int64_t lo, int64_t hi);
hegel_schema_t hegel_schema_int         (void);
hegel_schema_t hegel_schema_int_range   (int64_t lo, int64_t hi);
hegel_schema_t hegel_schema_long        (void);
hegel_schema_t hegel_schema_long_range  (int64_t lo, int64_t hi);
hegel_schema_t hegel_schema_u8          (void);
hegel_schema_t hegel_schema_u8_range    (uint64_t lo, uint64_t hi);
hegel_schema_t hegel_schema_u16         (void);
hegel_schema_t hegel_schema_u16_range   (uint64_t lo, uint64_t hi);
hegel_schema_t hegel_schema_u32         (void);
hegel_schema_t hegel_schema_u32_range   (uint64_t lo, uint64_t hi);
hegel_schema_t hegel_schema_u64         (void);
hegel_schema_t hegel_schema_u64_range   (uint64_t lo, uint64_t hi);

/* ================================================================
** Schema constructors — floats
** ================================================================ */

hegel_schema_t hegel_schema_float        (void);
hegel_schema_t hegel_schema_float_range  (double lo, double hi);
hegel_schema_t hegel_schema_double       (void);
hegel_schema_t hegel_schema_double_range (double lo, double hi);

/* ================================================================
** Schema constructors — text, optional, array, struct, self, variant
** ================================================================ */

hegel_schema_t hegel_schema_text (int min_len, int max_len);

hegel_schema_t hegel_schema_optional_ptr (hegel_schema_t inner);

/* Pure array constructor.  `len_offset` is the byte offset inside
** the parent struct where the element count will be written.  The
** array's pointer slot is determined by the enclosing binding
** (from HEGEL_ARRAY). */
hegel_schema_t hegel_schema_array (size_t len_offset,
                                   hegel_schema_t elem,
                                   int min_len, int max_len);

hegel_schema_t hegel_schema_array_inline (size_t len_offset,
                                          hegel_schema_t elem,
                                          size_t elem_size,
                                          int min_len, int max_len);

/* Internal: called by HEGEL_UNION / HEGEL_UNION_UNTAGGED macros.
** case_list is a NULL-terminated array of H_END_BINDING-terminated
** binding arrays (one per variant).  Each binding's offset is
** relative to the parent struct containing the union. */
hegel_schema_t hegel__union (size_t tag_offset, hegel_schema_kind kind,
                             hegel_binding_t ** case_list);

hegel_schema_t hegel_schema_variant_v (size_t tag_offset, size_t ptr_offset,
                                       hegel_schema_t * case_list);

/* Pick one of several STRUCT schemas, allocate it, and produce a
** pointer to it.  Unlike HEGEL_VARIANT, this does NOT write anything
** to a parent struct — it's a standalone pointer-producing schema,
** useful as an ARRAY element or inside HEGEL_OPTIONAL.
**
** Use case: "array of pointers to different-size structs" where the
** tag lives inside each pointed-to struct (not in a wrapper).  The
** generator picks a variant per element, allocates it, stores the
** raw pointer in the array slot. */
hegel_schema_t hegel_schema_one_of_struct_v (hegel_schema_t * case_list);

#define HEGEL_ONE_OF_STRUCT(...) \
  hegel_schema_one_of_struct_v ((hegel_schema_t[]){ __VA_ARGS__, H_END })

/* ================================================================
** Field-binding helper
** ================================================================
**
** Wraps a `{ offset, schema }` pair into a binding value.  This is
** what all HEGEL_* field macros expand to under the hood.  Users
** should not normally need to call this directly. */

hegel_binding_t hegel__bind (size_t offset, hegel_schema_t schema);

/* ================================================================
** Layout-entry → schema escape hatch
** ================================================================
**
** The HEGEL_UNION / HEGEL_UNION_UNTAGGED / HEGEL_VARIANT macros
** produce `hegel_layout_entry` values, which is what HEGEL_STRUCT
** wants.  But these composites are also useful as standalone
** schemas — e.g. as the element type of an ARRAY_INLINE.  Use
** `hegel_schema_of` to unwrap the underlying schema (the entry's
** slot sizes are discarded; those macros compute them for the
** enclosing struct).
**
** Example:
**   hegel_schema_t shape_union = hegel_schema_of (HEGEL_UNION (
**       HEGEL_CASE (HEGEL_DOUBLE (0.1, 100.0)),
**       HEGEL_CASE (HEGEL_DOUBLE (0.1, 100.0),
**                   HEGEL_DOUBLE (0.1, 100.0))));
**   hegel_schema_t gallery = HEGEL_STRUCT (Gallery,
**       HEGEL_ARRAY_INLINE (shape_union, sizeof (Shape), 1, 6)); */

static inline hegel_schema_t
hegel_schema_of (hegel_layout_entry e)
{
  return (hegel_schema_t){ e.schema };
}

/* ================================================================
** Functional combinators — map / filter / flat_map (for int source)
** ================================================================
**
** These take a source schema (must be INTEGER kind) and a callback.
** The result is itself a scalar schema — you place it in a struct
** via a HEGEL_MAP_INT / HEGEL_FILTER_INT / HEGEL_FLAT_MAP_INT binding.
**
** map:      draw source, apply fn, write result at binding offset
** filter:   draw source, call pred, assume(0) to discard if false
** flat_map: draw source, call fn to build a fresh schema, draw
**           from that, write result at binding offset.  The returned
**           schema must also be an INTEGER schema.  It's freed
**           after each draw, so the callback builds it fresh every
**           time.
**
** The source schema's reference is transferred to the combinator
** (normal ownership semantics).  The callback's ctx must outlive
** the combinator. */

hegel_schema_t hegel_schema_map_int (
    hegel_schema_t source,
    int (*fn)(int value, void *ctx),
    void *ctx);

hegel_schema_t hegel_schema_filter_int (
    hegel_schema_t source,
    int (*pred)(int value, void *ctx),
    void *ctx);

hegel_schema_t hegel_schema_flat_map_int (
    hegel_schema_t source,
    hegel_schema_t (*fn)(int value, void *ctx),
    void *ctx);

hegel_schema_t hegel_schema_map_i64 (
    hegel_schema_t source,
    int64_t (*fn)(int64_t value, void *ctx),
    void *ctx);

hegel_schema_t hegel_schema_filter_i64 (
    hegel_schema_t source,
    int (*pred)(int64_t value, void *ctx),
    void *ctx);

hegel_schema_t hegel_schema_flat_map_i64 (
    hegel_schema_t source,
    hegel_schema_t (*fn)(int64_t value, void *ctx),
    void *ctx);

hegel_schema_t hegel_schema_map_double (
    hegel_schema_t source,
    double (*fn)(double value, void *ctx),
    void *ctx);

hegel_schema_t hegel_schema_filter_double (
    hegel_schema_t source,
    int (*pred)(double value, void *ctx),
    void *ctx);

hegel_schema_t hegel_schema_flat_map_double (
    hegel_schema_t source,
    hegel_schema_t (*fn)(double value, void *ctx),
    void *ctx);

/* One-of for scalar schemas.  Picks one of several integer/float
** schemas and draws from it.  All cases must produce the same C
** type at the target field (user's responsibility).  Use when you
** want e.g. "a small int OR a huge int" (distributions that don't
** fit a single range). */
hegel_schema_t hegel_schema_one_of_scalar_v (hegel_schema_t * case_list);

/* Regex-generated text.  Produces a malloc'd string at a `char *`
** field, generated by hegel_draw_regex.
**
** WARNING: the underlying primitive uses hegeltest's "contains a
** match" semantics, not full-match.  For permissive patterns
** (matching the empty string), the generator returns ARBITRARY
** bytes including control characters, quotes, and non-ASCII.
** See TODO.md. */
hegel_schema_t hegel_schema_regex (const char * pattern, int capacity);

hegel_schema_t hegel_schema_self (void);

/* Variadic struct constructor — low-level form.  Users normally go
** through HEGEL_STRUCT (which computes offsets positionally from
** the struct type); this is the binding-taking form for advanced
** use cases. */
hegel_schema_t hegel_schema_struct_v (size_t size, hegel_binding_t * binding_list);

/* ================================================================
** Positional macro helpers
** ================================================================
**
** Each HEGEL_<kind> macro expands to a `hegel_layout_entry` compound
** literal.  The common case is "single-slot entry": a scalar schema
** plus its size and alignment.  HEGEL__LE_SIMPLE builds that. */

#define HEGEL__LE_SIMPLE(SCHEMA_EXPR, SIZE, ALIGN)  \
  ((hegel_layout_entry){                            \
      HEGEL_LAY_SIMPLE,                             \
      (SCHEMA_EXPR)._raw,                           \
      (SIZE), (ALIGN),                              \
      0, 0 })

/* Arg-count dispatch — picks HEGEL__<name>_0 / _1 / _2 / _3 / _4
** based on how many arguments the user passed.  Uses C23's
** __VA_OPT__; falls back to GCC's ##__VA_ARGS__ where needed. */

#define HEGEL__NARG(...) \
  HEGEL__NARG_(_0 __VA_OPT__(,) __VA_ARGS__, 5, 4, 3, 2, 1, 0)
#define HEGEL__NARG_(_0, _1, _2, _3, _4, _5, N, ...) N
#define HEGEL__GLUE(a, b)  HEGEL__GLUE_(a, b)
#define HEGEL__GLUE_(a, b) a##b
#define HEGEL__DISPATCH(name, ...) \
  HEGEL__GLUE(name##_, HEGEL__NARG(__VA_ARGS__))(__VA_ARGS__)

/* ================================================================
** Scalar field macros — positional
** ================================================================
**
** `HEGEL_INT()` → full int range.
** `HEGEL_INT(lo, hi)` → constrained int.
** Same shape for all integer/float widths. */

#define HEGEL__I8_0()       HEGEL__LE_SIMPLE (hegel_schema_i8 (), sizeof (int8_t), _Alignof (int8_t))
#define HEGEL__I8_2(lo, hi) HEGEL__LE_SIMPLE (hegel_schema_i8_range ((lo), (hi)), sizeof (int8_t), _Alignof (int8_t))
#define HEGEL_I8(...)       HEGEL__DISPATCH (HEGEL__I8, __VA_ARGS__)

#define HEGEL__I16_0()       HEGEL__LE_SIMPLE (hegel_schema_i16 (), sizeof (int16_t), _Alignof (int16_t))
#define HEGEL__I16_2(lo, hi) HEGEL__LE_SIMPLE (hegel_schema_i16_range ((lo), (hi)), sizeof (int16_t), _Alignof (int16_t))
#define HEGEL_I16(...)       HEGEL__DISPATCH (HEGEL__I16, __VA_ARGS__)

#define HEGEL__I32_0()       HEGEL__LE_SIMPLE (hegel_schema_i32 (), sizeof (int32_t), _Alignof (int32_t))
#define HEGEL__I32_2(lo, hi) HEGEL__LE_SIMPLE (hegel_schema_i32_range ((lo), (hi)), sizeof (int32_t), _Alignof (int32_t))
#define HEGEL_I32(...)       HEGEL__DISPATCH (HEGEL__I32, __VA_ARGS__)

#define HEGEL__I64_0()       HEGEL__LE_SIMPLE (hegel_schema_i64 (), sizeof (int64_t), _Alignof (int64_t))
#define HEGEL__I64_2(lo, hi) HEGEL__LE_SIMPLE (hegel_schema_i64_range ((lo), (hi)), sizeof (int64_t), _Alignof (int64_t))
#define HEGEL_I64(...)       HEGEL__DISPATCH (HEGEL__I64, __VA_ARGS__)

#define HEGEL__INT_0()       HEGEL__LE_SIMPLE (hegel_schema_int (), sizeof (int), _Alignof (int))
#define HEGEL__INT_2(lo, hi) HEGEL__LE_SIMPLE (hegel_schema_int_range ((lo), (hi)), sizeof (int), _Alignof (int))
#define HEGEL_INT(...)       HEGEL__DISPATCH (HEGEL__INT, __VA_ARGS__)

#define HEGEL__LONG_0()       HEGEL__LE_SIMPLE (hegel_schema_long (), sizeof (long), _Alignof (long))
#define HEGEL__LONG_2(lo, hi) HEGEL__LE_SIMPLE (hegel_schema_long_range ((lo), (hi)), sizeof (long), _Alignof (long))
#define HEGEL_LONG(...)       HEGEL__DISPATCH (HEGEL__LONG, __VA_ARGS__)

#define HEGEL__U8_0()       HEGEL__LE_SIMPLE (hegel_schema_u8 (), sizeof (uint8_t), _Alignof (uint8_t))
#define HEGEL__U8_2(lo, hi) HEGEL__LE_SIMPLE (hegel_schema_u8_range ((lo), (hi)), sizeof (uint8_t), _Alignof (uint8_t))
#define HEGEL_U8(...)       HEGEL__DISPATCH (HEGEL__U8, __VA_ARGS__)

#define HEGEL__U16_0()       HEGEL__LE_SIMPLE (hegel_schema_u16 (), sizeof (uint16_t), _Alignof (uint16_t))
#define HEGEL__U16_2(lo, hi) HEGEL__LE_SIMPLE (hegel_schema_u16_range ((lo), (hi)), sizeof (uint16_t), _Alignof (uint16_t))
#define HEGEL_U16(...)       HEGEL__DISPATCH (HEGEL__U16, __VA_ARGS__)

#define HEGEL__U32_0()       HEGEL__LE_SIMPLE (hegel_schema_u32 (), sizeof (uint32_t), _Alignof (uint32_t))
#define HEGEL__U32_2(lo, hi) HEGEL__LE_SIMPLE (hegel_schema_u32_range ((lo), (hi)), sizeof (uint32_t), _Alignof (uint32_t))
#define HEGEL_U32(...)       HEGEL__DISPATCH (HEGEL__U32, __VA_ARGS__)

#define HEGEL__U64_0()       HEGEL__LE_SIMPLE (hegel_schema_u64 (), sizeof (uint64_t), _Alignof (uint64_t))
#define HEGEL__U64_2(lo, hi) HEGEL__LE_SIMPLE (hegel_schema_u64_range ((lo), (hi)), sizeof (uint64_t), _Alignof (uint64_t))
#define HEGEL_U64(...)       HEGEL__DISPATCH (HEGEL__U64, __VA_ARGS__)

#define HEGEL__FLOAT_0()       HEGEL__LE_SIMPLE (hegel_schema_float (), sizeof (float), _Alignof (float))
#define HEGEL__FLOAT_2(lo, hi) HEGEL__LE_SIMPLE (hegel_schema_float_range ((lo), (hi)), sizeof (float), _Alignof (float))
#define HEGEL_FLOAT(...)       HEGEL__DISPATCH (HEGEL__FLOAT, __VA_ARGS__)

#define HEGEL__DOUBLE_0()       HEGEL__LE_SIMPLE (hegel_schema_double (), sizeof (double), _Alignof (double))
#define HEGEL__DOUBLE_2(lo, hi) HEGEL__LE_SIMPLE (hegel_schema_double_range ((lo), (hi)), sizeof (double), _Alignof (double))
#define HEGEL_DOUBLE(...)       HEGEL__DISPATCH (HEGEL__DOUBLE, __VA_ARGS__)

/* Boolean — a 1-byte unsigned int in [0, 1].  Field should be `bool` /
** `_Bool` from `stdbool.h` (1 byte). */
#define HEGEL_BOOL() \
  HEGEL__LE_SIMPLE (hegel_schema_u8_range (0, 1), sizeof (uint8_t), _Alignof (uint8_t))

/* Text: stores a `char *` pointer at the entry slot (malloc'd). */
#define HEGEL_TEXT(lo, hi) \
  HEGEL__LE_SIMPLE (hegel_schema_text ((lo), (hi)), sizeof (char *), _Alignof (char *))

/* Optional pointer: stores a pointer (possibly NULL) at the entry slot. */
#define HEGEL_OPTIONAL(inner) \
  HEGEL__LE_SIMPLE (hegel_schema_optional_ptr ((inner)), sizeof (void *), _Alignof (void *))

/* Self-recursive optional pointer.  The field must be a pointer to
** the enclosing struct type. */
#define HEGEL_SELF() \
  HEGEL__LE_SIMPLE (hegel_schema_optional_ptr (hegel_schema_self ()), sizeof (void *), _Alignof (void *))

/* Regex — pointer to malloc'd string. */
#define HEGEL_REGEX(pattern, capacity) \
  HEGEL__LE_SIMPLE (hegel_schema_regex ((pattern), (capacity)), sizeof (char *), _Alignof (char *))

/* Functional combinators — scalar-producing, single slot matching
** the combinator's target type. */
#define HEGEL_MAP_INT(source, fn, ctx) \
  HEGEL__LE_SIMPLE (hegel_schema_map_int ((source), (fn), (ctx)), sizeof (int), _Alignof (int))
#define HEGEL_FILTER_INT(source, pred, ctx) \
  HEGEL__LE_SIMPLE (hegel_schema_filter_int ((source), (pred), (ctx)), sizeof (int), _Alignof (int))
#define HEGEL_FLAT_MAP_INT(source, fn, ctx) \
  HEGEL__LE_SIMPLE (hegel_schema_flat_map_int ((source), (fn), (ctx)), sizeof (int), _Alignof (int))

#define HEGEL_MAP_I64(source, fn, ctx) \
  HEGEL__LE_SIMPLE (hegel_schema_map_i64 ((source), (fn), (ctx)), sizeof (int64_t), _Alignof (int64_t))
#define HEGEL_FILTER_I64(source, pred, ctx) \
  HEGEL__LE_SIMPLE (hegel_schema_filter_i64 ((source), (pred), (ctx)), sizeof (int64_t), _Alignof (int64_t))
#define HEGEL_FLAT_MAP_I64(source, fn, ctx) \
  HEGEL__LE_SIMPLE (hegel_schema_flat_map_i64 ((source), (fn), (ctx)), sizeof (int64_t), _Alignof (int64_t))

#define HEGEL_MAP_DOUBLE(source, fn, ctx) \
  HEGEL__LE_SIMPLE (hegel_schema_map_double ((source), (fn), (ctx)), sizeof (double), _Alignof (double))
#define HEGEL_FILTER_DOUBLE(source, pred, ctx) \
  HEGEL__LE_SIMPLE (hegel_schema_filter_double ((source), (pred), (ctx)), sizeof (double), _Alignof (double))
#define HEGEL_FLAT_MAP_DOUBLE(source, fn, ctx) \
  HEGEL__LE_SIMPLE (hegel_schema_flat_map_double ((source), (fn), (ctx)), sizeof (double), _Alignof (double))

/* One-of scalar — picks one of N scalar schemas.  Writes an int /
** i64 / double at the entry slot. */
#define HEGEL_ONE_OF_INT(...) \
  HEGEL__LE_SIMPLE (hegel_schema_one_of_scalar_v ( \
      (hegel_schema_t[]){ __VA_ARGS__, H_END }), sizeof (int), _Alignof (int))
#define HEGEL_ONE_OF_I64(...) \
  HEGEL__LE_SIMPLE (hegel_schema_one_of_scalar_v ( \
      (hegel_schema_t[]){ __VA_ARGS__, H_END }), sizeof (int64_t), _Alignof (int64_t))
#define HEGEL_ONE_OF_DOUBLE(...) \
  HEGEL__LE_SIMPLE (hegel_schema_one_of_scalar_v ( \
      (hegel_schema_t[]){ __VA_ARGS__, H_END }), sizeof (double), _Alignof (double))

/* Array: two sub-slots (void* pointer, int count).  The layout pass
** fills in `array_def.len_offset` after computing the second slot's
** offset. */
#define HEGEL_ARRAY(elem, lo, hi)                                     \
  ((hegel_layout_entry){                                              \
      HEGEL_LAY_ARRAY,                                                \
      hegel_schema_array (0, (elem), (lo), (hi))._raw,                \
      sizeof (void *), _Alignof (void *),                             \
      sizeof (int),    _Alignof (int) })

#define HEGEL_ARRAY_INLINE(elem, elem_sz, lo, hi)                     \
  ((hegel_layout_entry){                                              \
      HEGEL_LAY_ARRAY_INLINE,                                         \
      hegel_schema_array_inline (0, (elem), (elem_sz), (lo), (hi))._raw, \
      sizeof (void *), _Alignof (void *),                             \
      sizeof (int),    _Alignof (int) })

/* HEGEL_CASE wraps a variant case's fields in an H_END_BINDING-
** terminated binding array.  Each field inside a case is still a
** positional layout entry — HEGEL_CASE converts them to bindings
** with offsets relative to the union body (base 0).  The struct
** layout pass shifts those offsets by the union body's computed
** base when the enclosing HEGEL_UNION is placed. */
hegel_binding_t * hegel__case (hegel_layout_entry * entries,
                               size_t * out_body_size,
                               size_t * out_body_align);

#define HEGEL_CASE(...)                                               \
  ((hegel_layout_entry[]){ __VA_ARGS__, HEGEL_LAYOUT_END })

/* Union: tag + inline body.  Cases are computed at schema-build time
** from lists of layout entries.  Each case's fields are laid out
** relative to body base 0; the layout pass shifts them by the
** union body's offset inside the parent struct. */
hegel_schema_t hegel__union_positional (hegel_layout_kind kind,
                                        hegel_layout_entry ** case_list,
                                        size_t * out_body_size,
                                        size_t * out_body_align);

/* Computed at expansion time: two static state pointers shared
** between HEGEL_UNION and its children's layout.  Encoded as
** compound-literal locals of hegel__union_build. */
#define HEGEL_UNION(...)                                              \
  hegel__union_make (HEGEL_LAY_UNION,                                 \
      (hegel_layout_entry *[]){ __VA_ARGS__, NULL })

#define HEGEL_UNION_UNTAGGED(...)                                     \
  hegel__union_make (HEGEL_LAY_UNION_UNTAGGED,                        \
      (hegel_layout_entry *[]){ __VA_ARGS__, NULL })

hegel_layout_entry hegel__union_make (hegel_layout_kind kind,
                                      hegel_layout_entry ** case_list);

/* Variant: tag + pointer to separately allocated struct. */
#define HEGEL_VARIANT(...)                                            \
  hegel__variant_make ((hegel_schema_t[]){ __VA_ARGS__, H_END })

hegel_layout_entry hegel__variant_make (hegel_schema_t * case_list);

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

/* Offset-based struct-field accessor.  Linear scan over the struct
** schema's bindings, returning the shape corresponding to the field
** whose binding offset matches.  Returns NULL if no match (or if
** `s` is not a struct shape).
**
** Typical usage is through the HEGEL_SHAPE_GET macro:
**
**     Foo *f;
**     hegel_shape *sh = hegel_schema_draw(tc, foo_schema, (void **)&f);
**     hegel_shape *name_shape = HEGEL_SHAPE_GET(sh, Foo, name); */
hegel_shape * hegel_shape_get_offset (hegel_shape * s, size_t offset);

#define HEGEL_SHAPE_GET(sh, type, field) \
  hegel_shape_get_offset ((sh), offsetof (type, field))

#ifdef __cplusplus
}
#endif

#endif /* HEGEL_GEN_H */
