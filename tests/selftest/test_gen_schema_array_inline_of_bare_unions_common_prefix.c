/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: inline array of bare unions using the C common-initial-sequence
** rule.
**
** Unlike test_gen_schema_array_inline_of_tagged_unions.c (which wraps
** the union in a struct with an outer tag field), this test uses a
** *bare* union where each variant starts with the same `int tag` field
** at offset 0.  The C standard's "common initial sequence" rule lets
** you read the tag through any variant regardless of which was last
** written — a trick used by real C interpreters (Lua TValue, Erlang
** VM terms, Scheme cell tagging) to pack a type tag into the payload
** without a separate wrapper struct.
**
** Memory layout: every slot in the array is `sizeof(BareShape)` bytes
** (= max of circle and rect, = 24 on 64-bit), and each slot
** independently holds one variant's data.  The tag lives at offset 0
** of every slot because both variants have `int tag` as their first
** field.
**
** We use HEGEL_UNION_UNTAGGED to express this: the schema doesn't
** write an "outer" tag (there's no outer struct), but each case's
** first field is a HEGEL_INT constant (min==max) that writes the
** correct tag value into offset 0 of the slot as part of its normal
** field draw.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

/* ---- Types ---- */

/* Bare union with common initial sequence (int tag at offset 0 in
** every variant).  No wrapping struct. */
typedef union {
  struct { int tag; double radius; }                circle;
  struct { int tag; double width; double height; }  rect;
  struct { int tag; int64_t millis; }                timestamp;
} BareShape;

typedef struct {
  BareShape *       shapes;
  int               n_shapes;
} Stream;

/* ---- Schema ---- */

static hegel_schema_t stream_schema;

static
void
init_schema (void)
{
  /* Each case's first field is a constant-tag HEGEL_INT — it writes
  ** the literal tag value (17, 29, 42) to offset 0 of the union
  ** (which is where .circle.tag, .rect.tag, .timestamp.tag all live
  ** by virtue of the common initial sequence).
  **
  ** Tag values are arbitrary here — picking weird numbers to prove
  ** they're not just "variant index". */
  hegel_schema_t bare_shape = HEGEL_UNION_UNTAGGED (
      HEGEL_CASE (HEGEL_INT    (BareShape, circle.tag, 17, 17),
                  HEGEL_DOUBLE (BareShape, circle.radius, 0.1, 100.0)),
      HEGEL_CASE (HEGEL_INT    (BareShape, rect.tag, 29, 29),
                  HEGEL_DOUBLE (BareShape, rect.width,  0.1, 100.0),
                  HEGEL_DOUBLE (BareShape, rect.height, 0.1, 100.0)),
      HEGEL_CASE (HEGEL_INT    (BareShape, timestamp.tag, 42, 42),
                  HEGEL_I64    (BareShape, timestamp.millis,
                                0, 1000000000)));

  stream_schema = hegel_schema_struct (sizeof (Stream),
      HEGEL_ARRAY_INLINE (Stream, shapes, n_shapes,
                          bare_shape, sizeof (BareShape), 1, 6));
}

/* ---- Test ---- */

static
void
test_stream (
hegel_testcase *            tc)
{
  Stream *          s;
  hegel_shape *     sh;
  int               i;
  int               n_circles = 0, n_rects = 0, n_timestamps = 0;

  sh = hegel_schema_draw (tc, stream_schema, (void **) &s);

  HEGEL_ASSERT (s->n_shapes >= 1 && s->n_shapes <= 6,
                "n_shapes=%d", s->n_shapes);

  for (i = 0; i < s->n_shapes; i ++) {
    BareShape * b = &s->shapes[i];

    /* Read the tag through ANY variant — they all share offset 0.
    ** Using circle.tag for convention; rect.tag and timestamp.tag
    ** would give the same value. */
    int tag = b->circle.tag;

    if (tag == 17) {
      HEGEL_ASSERT (b->circle.radius >= 0.1 && b->circle.radius <= 100.0,
                    "shapes[%d].circle.radius=%f", i, b->circle.radius);
      n_circles ++;
    } else if (tag == 29) {
      HEGEL_ASSERT (b->rect.width >= 0.1 && b->rect.width <= 100.0,
                    "shapes[%d].rect.width=%f", i, b->rect.width);
      HEGEL_ASSERT (b->rect.height >= 0.1 && b->rect.height <= 100.0,
                    "shapes[%d].rect.height=%f", i, b->rect.height);
      n_rects ++;
    } else if (tag == 42) {
      HEGEL_ASSERT (b->timestamp.millis >= 0
                    && b->timestamp.millis <= 1000000000,
                    "shapes[%d].timestamp.millis=%ld",
                    i, (long) b->timestamp.millis);
      n_timestamps ++;
    } else {
      hegel_fail ("unknown tag in bare-union array");
    }
  }

  HEGEL_ASSERT (n_circles + n_rects + n_timestamps == s->n_shapes,
                "c=%d r=%d t=%d total=%d",
                n_circles, n_rects, n_timestamps, s->n_shapes);

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
  printf ("Testing ARRAY_INLINE of bare unions (common initial sequence)...\n");
  hegel_run_test (test_stream);
  printf ("  PASSED\n");

  hegel_schema_free (stream_schema);
  return (0);
}
