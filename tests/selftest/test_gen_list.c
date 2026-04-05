/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_gen_draw_list_int produces correct-length lists with
** values in the element generator's range.
**
** Layer 1: sum_array() computes the sum of an int array.
** Layer 2: draw a list of int(0, 9) with length in [2, 10], then
**          assert sum_array(buf, len) is in [0, 9 * len].
**          This test should PASS.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdlib.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ----
** Returns the sum of the first len elements of arr. */

static
int
sum_array (
const int *                 arr,
int                         len)
{
  int                 s;
  int                 i;

  s = 0;
  for (i = 0; i < len; i ++)
    s += arr[i];

  return (s);
}

/* ---- Layer 2: hegel test ---- */

static
void
testListInt (
hegel_testcase *            tc)
{
  hegel_gen *          elem;
  int                  buf[16];
  int                  len;
  int                  total;

  elem = hegel_gen_int (0, 9);
  len = hegel_gen_draw_list_int (tc, elem, 2, 10, buf, 16);

  HEGEL_ASSERT (len >= 2 && len <= 10,
                "list length %d not in [2,10]", len);

  total = sum_array (buf, len);

  HEGEL_ASSERT (total >= 0 && total <= 9 * len,
                "sum_array = %d, expected [0, %d]", total, 9 * len);

  hegel_gen_free (elem);
}

/* ---- Layer 3: runner (see Makefile TESTS_PASS) ---- */

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
