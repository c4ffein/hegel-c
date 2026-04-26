/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_LEN_PREFIXED_ARRAY — Pascal-string-style buffer.
**
** Topology:
**     PStr { uint8_t *data; }
**     data[0] is the length n; data[1..n] are the drawn bytes.
**
** Schema draws a length n in [0, 32], allocates n+1 bytes total,
** writes n at byte 0, draws n bytes in ['a', 'z'] at offsets 1..n.
**
** Verifies:
**   - data[0] equals the drawn length n (not garbage)
**   - data[1..n] are all in the constrained byte range
**   - Buffer is freed cleanly via hegel_shape_free (no leak under ASan)
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  uint8_t *           data;
} PStr;

HEGEL_BINDING (n);

static hegel_schema_t pstr_schema;

static
void
test_len_prefixed (
hegel_testcase *    tc)
{
  PStr *              p;
  hegel_shape *       sh;
  uint8_t             length;
  int                 i;

  sh = hegel_schema_draw (tc, pstr_schema, (void **) &p);

  HEGEL_ASSERT (p->data != NULL, "data NULL");

  length = p->data[0];
  HEGEL_ASSERT (length <= 32,
                "length=%u out of [0,32]", (unsigned) length);

  for (i = 1; i <= (int) length; i ++) {
    uint8_t c = p->data[i];
    HEGEL_ASSERT (c >= 'a' && c <= 'z',
                  "data[%d]=%u out of ['a','z']", i, (unsigned) c);
  }

  hegel_shape_free (sh);
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  pstr_schema = HEGEL_STRUCT (PStr,
      HEGEL_LET (n, HEGEL_INT (0, 32)),
      HEGEL_LEN_PREFIXED_ARRAY (HEGEL_USE (n), HEGEL_U8 ('a', 'z')));

  printf ("Testing HEGEL_LEN_PREFIXED_ARRAY (Pascal-string)...\n");
  hegel_run_test (test_len_prefixed);
  printf ("  PASSED\n");

  hegel_schema_free (pstr_schema);
  return (0);
}
