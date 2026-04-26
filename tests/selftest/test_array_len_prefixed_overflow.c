/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Negative test: HEGEL_LEN_PREFIXED_ARRAY draws n through the LET
** schema and writes it into slot 0 in elem's bit-width.  If n
** exceeds elem's representable range, the prefix would silently
** truncate — so the runtime check aborts instead.
**
** Setup: LET range [256, 1000] guarantees overflow into a u8
** elem (max 255) on the very first draw, regardless of shrinker
** bias.  Deterministic abort, no flakiness from exploration order.
**
** The abort fires inside the forked child during draw, so this
** is a true fork-mode CRASH (distinct from the schema-build
** aborts in test_arr_of_raw_int_abort.c which crash before fork).
**
** Expected: EXIT non-zero (classified as CRASH).
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct { uint8_t * data; } PStr;

HEGEL_BINDING (n);

static hegel_schema_t pstr_schema;

static
void
test_overflow (
hegel_testcase *    tc)
{
  PStr *              p;
  hegel_shape *       sh;

  /* Every draw of n is in [256, 1000] — exceeds u8 max (255).
  ** The runtime check inside HEGEL_LEN_PREFIXED_ARRAY draw fires
  ** on the very first call and aborts the child. */
  sh = hegel_schema_draw (tc, pstr_schema, (void **) &p);

  /* Unreachable — the draw above must abort. */
  hegel_shape_free (sh);
  HEGEL_ASSERT (0, "BUG: draw returned despite overflow");
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  pstr_schema = HEGEL_STRUCT (PStr,
      HEGEL_LET (n, HEGEL_INT (256, 1000)),
      HEGEL_LEN_PREFIXED_ARRAY (HEGEL_USE (n), HEGEL_U8 ('a', 'z')));

  printf ("Testing HEGEL_LEN_PREFIXED_ARRAY runtime overflow abort...\n");
  hegel_run_test (test_overflow);

  printf ("BUG: hegel_run_test returned despite overflow abort\n");
  hegel_schema_free (pstr_schema);
  return (1);
}
