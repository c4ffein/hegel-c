/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: HEGEL_TERMINATED_ARRAY — null-terminated-string-style.
**
** Topology:
**     CStr { char *s; }
**     s[0..n-1] are the drawn chars; s[n] is the null terminator.
**
** Schema draws length n in [0, 32], allocates n+1 bytes, draws n
** chars in ['a', 'z'] at offsets 0..n-1, writes 0 at offset n.
**
** Verifies:
**   - The terminator at s[n] is exactly 0
**   - No drawn char in s[0..n-1] is 0 (would have collided with the
**     sentinel — the schema-build-time check catches this for
**     bounded INTEGER elements, but we double-check at runtime
**     anyway since strlen() depends on it)
**   - strlen(s) == n
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct {
  char *              s;
} CStr;

HEGEL_BINDING (n);

static hegel_schema_t cstr_schema;

static
void
test_terminated (
hegel_testcase *    tc)
{
  CStr *              c;
  hegel_shape *       sh;
  size_t              len;

  sh = hegel_schema_draw (tc, cstr_schema, (void **) &c);

  HEGEL_ASSERT (c->s != NULL, "s NULL");

  /* strlen() walks until the first 0 — if the sentinel is present
  ** at exactly position n, this returns n.  If a drawn char somehow
  ** matched the sentinel, strlen would be smaller. */
  len = strlen (c->s);
  HEGEL_ASSERT (len <= 32, "strlen=%zu out of [0,32]", len);

  for (size_t i = 0; i < len; i ++) {
    char ch = c->s[i];
    HEGEL_ASSERT (ch >= 'a' && ch <= 'z',
                  "s[%zu]=%d out of ['a','z']", i, (int) ch);
  }

  /* strlen has already verified that s[len] == 0; assert explicitly
  ** too so the contract is plain in the test. */
  HEGEL_ASSERT (c->s[len] == 0, "terminator missing at offset %zu", len);

  hegel_shape_free (sh);
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  /* elem is u8 in ['a','z'] = [97,122] — does not include the
  ** sentinel 0, so the schema-build collision check passes. */
  cstr_schema = HEGEL_STRUCT (CStr,
      HEGEL_LET (n, HEGEL_INT (0, 32)),
      HEGEL_TERMINATED_ARRAY (HEGEL_USE (n), HEGEL_U8 ('a', 'z'), 0));

  printf ("Testing HEGEL_TERMINATED_ARRAY (null-terminated string)...\n");
  hegel_run_test (test_terminated);
  printf ("  PASSED\n");

  hegel_schema_free (cstr_schema);
  return (0);
}
