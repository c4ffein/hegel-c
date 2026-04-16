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
**   hegel_schema_t s = HEGEL_STRUCT (Tree,
**       HEGEL_INT      (-1000, 1000),
**       HEGEL_OPTIONAL (hegel_schema_text (0, 8)),
**       HEGEL_SELF     (),
**       HEGEL_SELF     ());
**
** === Positional field entries ===
**
** The HEGEL_STRUCT(T, ...) macro takes a positional list of field
** generators in declaration order — one entry per struct field, in
** the same order the C compiler lays them out.  Every HEGEL_<kind>
** macro expands to a hegel_schema_t.  HEGEL_STRUCT walks the list at
** runtime, derives each entry's size/alignment from its schema kind
** via hegel__schema_slot_info, computes offsets the same way the C
** compiler does, and asserts `sizeof(T) == computed_total`.  A
** mismatched field order or type fires the assert immediately at
** schema-build time.
**
** Some entries occupy more than one slot — HEGEL_ARRAY / _INLINE
** take a pointer + count.  HEGEL_UNION / _UNTAGGED / _VARIANT
** occupy one cluster slot whose internal offsets live on the schema.
**
** === Schema reuse at multiple positions ===
**
** A schema describes WHAT to generate (an int in [0,100], a text
** of length 0..8, a struct with these fields).  A schema is a pure
** value — it has no position, no "where does this write to."  That
** means the same schema can appear at multiple positions in a
** parent; bump its refcount via hegel_schema_ref() before each
** extra use.
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

/* ================================================================
** Layout entries — the positional API
** ================================================================
**
** Every HEGEL_<kind> macro produces a `hegel_schema_t` — a reference
** to a heap-allocated schema node.  `HEGEL_STRUCT(T, ...)` walks the
** list at runtime, uses `hegel__schema_slot_info` (kind dispatch) to
** derive each entry's size and alignment, computes offsets using the
** same rules the C compiler uses for struct layout, fixes up
** kind-specific sub-offsets inside each schema (e.g.
** `array_def.len_offset`), and builds the struct schema's children
** + offsets arrays.
**
** A runtime assertion checks that the computed total == `sizeof(T)`.
** If they disagree, the user's struct fields don't match the macro
** list in order or type — abort with a diagnostic.
**
** Some entries occupy **two slots** in the parent.  These are
** detected by schema kind at layout time:
**   - HEGEL_ARRAY / HEGEL_ARRAY_INLINE (kind = HEGEL_SCH_ARRAY /
**     HEGEL_SCH_ARRAY_INLINE): pointer slot + int count slot.
**   - HEGEL_UNION (kind = HEGEL_SCH_UNION): int tag + body inside
**     one cluster slot, handled as a single slot by the parent pass
**     (the cluster's internal offsets are in the schema itself).
**   - HEGEL_VARIANT (kind = HEGEL_SCH_VARIANT): int tag + void*
**     inside one cluster slot. */

/* Driver: compute offsets, fix up array len_offsets, build children
** + offsets arrays, assert `sizeof(T) == computed_total`, return
** the finished struct schema. */
hegel_schema_t hegel__struct_build (size_t declared_size,
                                    size_t declared_align,
                                    hegel_schema_t * entries);

#define HEGEL_STRUCT(T, ...) \
  hegel__struct_build (sizeof (T), _Alignof (T), \
      (hegel_schema_t[]){ __VA_ARGS__, H_END })

/* HEGEL_INLINE / HEGEL_INLINE_REF — inline-by-value sub-struct as a
** positional field.  Both are now regular struct schemas; the parent
** HEGEL_STRUCT layout pass reads `struct_def.size` and
** `struct_def.align` via `hegel__schema_slot_info`.
**
**   HEGEL_INLINE (T, entries...) — builds a fresh sub-schema each call.
**     The nested `hegel__struct_build` asserts `sizeof(T) == total`.
**
**   HEGEL_INLINE_REF (T, schema) — plugs in a pre-built struct schema.
**     Useful when you want to share the same sub-schema across
**     multiple parent fields (pair with hegel_schema_ref to add
**     extra refs).  Asserts at build time that `schema` is a
**     HEGEL_STRUCT and that `schema->struct_def.size == sizeof(T)`. */
hegel_schema_t hegel__inline_ref_check (size_t declared_size,
                                        hegel_schema_t sch);

#define HEGEL_INLINE(T, ...)                                               \
  hegel__struct_build (sizeof (T), _Alignof (T),                           \
      (hegel_schema_t[]){ __VA_ARGS__, H_END })

#define HEGEL_INLINE_REF(T, sch) \
  hegel__inline_ref_check (sizeof (T), (sch))

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
      size_t            align;         /* _Alignof(T); set by hegel__struct_build */
      int               n_children;
      struct hegel_schema ** children; /* flat array, n_children entries */
      size_t *          offsets;       /* parallel to children, n_children entries */
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
      size_t            cluster_size;   /* total bytes of tag + body */
      size_t            cluster_align;  /* max(alignof(int)?, body_align) */
      int               n_cases;
      /* Each case is a full hegel_schema of kind STRUCT whose
      ** struct_def.children / .offsets describe the case body.  This
      ** parallels variant_def.cases above — the two composite kinds
      ** have the same shape now. */
      struct hegel_schema ** cases;
    }                                                  union_def;
    struct {
      size_t            tag_offset;
      size_t            ptr_offset;
      size_t            cluster_size;   /* total bytes of tag + ptr slot */
      size_t            cluster_align;  /* max(alignof(int), alignof(void*)) */
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
** === The H_END sentinel ===
**
** Variadic macros build compound-literal schema arrays terminated
** by a zeroed sentinel.  Users never write trailing NULL — the
** macros append the sentinel automatically.
*/

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
  /* Backpointer to the schema that produced this shape.  Raw pointer,
  ** no refcount bump — the schema is guaranteed to outlive every shape
  ** it produces (enforced structurally in fork mode because the parent
  ** owns the schema and is blocked in parent_serve while the child
  ** holds shape pointers; enforced by convention in nofork mode).  May
  ** be NULL for synthetic shape nodes that don't correspond to a
  ** single schema node — currently only the variant-body struct shape
  ** built inside HEGEL_SCH_UNION / _UNTAGGED drawing.  See PLAN.md. */
  struct hegel_schema *   schema;
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

/* ================================================================
** Positional macro helpers
** ================================================================
**
** Every HEGEL_<kind> macro produces a `hegel_schema_t`.  Primitives
** are thin wrappers around schema-constructor functions; composites
** (ARRAY, UNION, VARIANT, INLINE) produce schemas whose kind and
** metadata let `hegel__struct_build` derive layout info via
** `hegel__schema_slot_info`. */

/* Arg-count dispatch — picks HEGEL__<name>_0 / _1 / _2 / _3 / _4
** based on how many arguments the user passed.  Uses C23's
** __VA_OPT__. */

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

#define HEGEL__I8_0()       hegel_schema_i8 ()
#define HEGEL__I8_2(lo, hi) hegel_schema_i8_range ((lo), (hi))
#define HEGEL_I8(...)       HEGEL__DISPATCH (HEGEL__I8, __VA_ARGS__)

#define HEGEL__I16_0()       hegel_schema_i16 ()
#define HEGEL__I16_2(lo, hi) hegel_schema_i16_range ((lo), (hi))
#define HEGEL_I16(...)       HEGEL__DISPATCH (HEGEL__I16, __VA_ARGS__)

#define HEGEL__I32_0()       hegel_schema_i32 ()
#define HEGEL__I32_2(lo, hi) hegel_schema_i32_range ((lo), (hi))
#define HEGEL_I32(...)       HEGEL__DISPATCH (HEGEL__I32, __VA_ARGS__)

#define HEGEL__I64_0()       hegel_schema_i64 ()
#define HEGEL__I64_2(lo, hi) hegel_schema_i64_range ((lo), (hi))
#define HEGEL_I64(...)       HEGEL__DISPATCH (HEGEL__I64, __VA_ARGS__)

#define HEGEL__INT_0()       hegel_schema_int ()
#define HEGEL__INT_2(lo, hi) hegel_schema_int_range ((lo), (hi))
#define HEGEL_INT(...)       HEGEL__DISPATCH (HEGEL__INT, __VA_ARGS__)

#define HEGEL__LONG_0()       hegel_schema_long ()
#define HEGEL__LONG_2(lo, hi) hegel_schema_long_range ((lo), (hi))
#define HEGEL_LONG(...)       HEGEL__DISPATCH (HEGEL__LONG, __VA_ARGS__)

#define HEGEL__U8_0()       hegel_schema_u8 ()
#define HEGEL__U8_2(lo, hi) hegel_schema_u8_range ((lo), (hi))
#define HEGEL_U8(...)       HEGEL__DISPATCH (HEGEL__U8, __VA_ARGS__)

#define HEGEL__U16_0()       hegel_schema_u16 ()
#define HEGEL__U16_2(lo, hi) hegel_schema_u16_range ((lo), (hi))
#define HEGEL_U16(...)       HEGEL__DISPATCH (HEGEL__U16, __VA_ARGS__)

#define HEGEL__U32_0()       hegel_schema_u32 ()
#define HEGEL__U32_2(lo, hi) hegel_schema_u32_range ((lo), (hi))
#define HEGEL_U32(...)       HEGEL__DISPATCH (HEGEL__U32, __VA_ARGS__)

#define HEGEL__U64_0()       hegel_schema_u64 ()
#define HEGEL__U64_2(lo, hi) hegel_schema_u64_range ((lo), (hi))
#define HEGEL_U64(...)       HEGEL__DISPATCH (HEGEL__U64, __VA_ARGS__)

#define HEGEL__FLOAT_0()       hegel_schema_float ()
#define HEGEL__FLOAT_2(lo, hi) hegel_schema_float_range ((lo), (hi))
#define HEGEL_FLOAT(...)       HEGEL__DISPATCH (HEGEL__FLOAT, __VA_ARGS__)

#define HEGEL__DOUBLE_0()       hegel_schema_double ()
#define HEGEL__DOUBLE_2(lo, hi) hegel_schema_double_range ((lo), (hi))
#define HEGEL_DOUBLE(...)       HEGEL__DISPATCH (HEGEL__DOUBLE, __VA_ARGS__)

/* Boolean — a 1-byte unsigned int in [0, 1].  Field should be `bool` /
** `_Bool` from `stdbool.h` (1 byte). */
#define HEGEL_BOOL() \
  hegel_schema_u8_range (0, 1)

/* Text: stores a `char *` pointer at the entry slot (malloc'd). */
#define HEGEL_TEXT(lo, hi) \
  hegel_schema_text ((lo), (hi))

/* Optional pointer: stores a pointer (possibly NULL) at the entry slot. */
#define HEGEL_OPTIONAL(inner) \
  hegel_schema_optional_ptr ((inner))

/* Self-recursive optional pointer.  The field must be a pointer to
** the enclosing struct type. */
#define HEGEL_SELF() \
  hegel_schema_optional_ptr (hegel_schema_self ())

/* Regex — pointer to malloc'd string. */
#define HEGEL_REGEX(pattern, capacity) \
  hegel_schema_regex ((pattern), (capacity))

/* Functional combinators — scalar-producing. */
#define HEGEL_MAP_INT(source, fn, ctx) \
  hegel_schema_map_int ((source), (fn), (ctx))
#define HEGEL_FILTER_INT(source, pred, ctx) \
  hegel_schema_filter_int ((source), (pred), (ctx))
#define HEGEL_FLAT_MAP_INT(source, fn, ctx) \
  hegel_schema_flat_map_int ((source), (fn), (ctx))

#define HEGEL_MAP_I64(source, fn, ctx) \
  hegel_schema_map_i64 ((source), (fn), (ctx))
#define HEGEL_FILTER_I64(source, pred, ctx) \
  hegel_schema_filter_i64 ((source), (pred), (ctx))
#define HEGEL_FLAT_MAP_I64(source, fn, ctx) \
  hegel_schema_flat_map_i64 ((source), (fn), (ctx))

#define HEGEL_MAP_DOUBLE(source, fn, ctx) \
  hegel_schema_map_double ((source), (fn), (ctx))
#define HEGEL_FILTER_DOUBLE(source, pred, ctx) \
  hegel_schema_filter_double ((source), (pred), (ctx))
#define HEGEL_FLAT_MAP_DOUBLE(source, fn, ctx) \
  hegel_schema_flat_map_double ((source), (fn), (ctx))

/* One-of scalar — picks one of N scalar schemas.  Size and alignment
** are inferred from the first case's kind at layout time (all cases
** must share the same scalar width, which is already the intended
** usage — the output slot is sized for one scalar, not a union). */
#define HEGEL_ONE_OF(...) \
  hegel_schema_one_of_scalar_v ((hegel_schema_t[]){ __VA_ARGS__, H_END })

/* Array: two sub-slots (void* pointer, int count) in the parent.
** hegel__struct_build detects HEGEL_SCH_ARRAY by kind and hardcodes
** the second slot to {sizeof(int), _Alignof(int)}, writing the
** computed length-slot offset back into array_def.len_offset. */
#define HEGEL_ARRAY(elem, lo, hi) \
  hegel_schema_array (0, (elem), (lo), (hi))

#define HEGEL_ARRAY_INLINE(elem, elem_sz, lo, hi) \
  hegel_schema_array_inline (0, (elem), (elem_sz), (lo), (hi))

/* HEGEL_CASE packages a union case's fields — a list of schemas —
** into a compound-literal array that HEGEL_UNION collects.  The
** contained schemas are laid out relative to body base 0 by
** hegel__lay_case at union-build time. */
#define HEGEL_CASE(...) \
  ((hegel_schema_t[]){ __VA_ARGS__, H_END })

/* Union: tag + inline body. */
#define HEGEL_UNION(...) \
  hegel__union_make (HEGEL_SCH_UNION, \
      (hegel_schema_t *[]){ __VA_ARGS__, NULL })

#define HEGEL_UNION_UNTAGGED(...) \
  hegel__union_make (HEGEL_SCH_UNION_UNTAGGED, \
      (hegel_schema_t *[]){ __VA_ARGS__, NULL })

hegel_schema_t hegel__union_make (hegel_schema_kind kind,
                                  hegel_schema_t ** case_list);

/* Variant: tag + pointer to separately allocated struct. */
#define HEGEL_VARIANT(...) \
  hegel__variant_make ((hegel_schema_t[]){ __VA_ARGS__, H_END })

hegel_schema_t hegel__variant_make (hegel_schema_t * case_list);

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

/* Offset-based struct-field accessor.  Walks the struct shape's
** bindings looking for the one that matches `offset`, and returns
** the LEAF shape at that offset — not an enclosing wrapper.  For
** nested HEGEL_INLINE sub-structs, descends recursively: if the
** requested offset matches an inline sub-struct binding, the
** accessor keeps walking into that sub-struct until it reaches a
** non-struct shape.  Returns NULL if no binding matches (or if
** `s` is not a struct shape).
**
** Consequence: when several field paths share the same byte offset
** (e.g. `Doll.b`, `Doll.b.g`, `Doll.b.g.karat` all at offset 0),
** HEGEL_SHAPE_GET always returns the deepest leaf — you cannot
** reach an inline wrapper shape via this accessor.  Use
** hegel_shape_field() directly to walk wrappers by index.
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
