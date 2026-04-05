/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_gen_draw_list_int produces correct-length lists with
** values in the element generator's range.
**
** Property: list of int(0,9) with length in [2,10] has:
**   - length in [2,10]
**   - all elements in [0,9]
** This test should PASS.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"

static
void
testListInt (
hegel_testcase *            tc)
{
  hegel_gen *          elem;
  int                  buf[16];
  int                  len;
  int                  i;

  elem = hegel_gen_int (0, 9);
  len = hegel_gen_draw_list_int (tc, elem, 2, 10, buf, 16);

  HEGEL_ASSERT (len >= 2 && len <= 10,
                "list length %d not in [2,10]", len);

  for (i = 0; i < len; i ++) {
    HEGEL_ASSERT (buf[i] >= 0 && buf[i] <= 9,
                  "list[%d] = %d, expected [0,9]", i, buf[i]);
  }

  hegel_gen_free (elem);
}

int
main (
int                 argc,
char *              argv[])
{
  (void) argc;
  (void) argv;

  printf ("Testing gen_draw_list_int...\n");
  hegel_run_test (testListInt);
  printf ("PASSED\n");

  return (0);
}
