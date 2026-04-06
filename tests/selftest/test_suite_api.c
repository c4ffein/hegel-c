/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Test hegel_suite API: run three tests in one binary via a suite.
** Two should pass, one should fail. Suite should return 1 (failure). */

#include "hegel_c.h"
#include <stdio.h>

static int identity(int x) { return x; }
static int always_negative(int x) { (void)x; return -1; }

static void test_identity(hegel_testcase * tc) {
  int x = hegel_draw_int(tc, 0, 100);
  int r = identity(x);
  HEGEL_ASSERT(r >= 0, "identity(%d) = %d", x, r);
}

static void test_bounds(hegel_testcase * tc) {
  int x = hegel_draw_int(tc, -50, 50);
  HEGEL_ASSERT(x >= -50 && x <= 50, "x=%d out of bounds", x);
}

static void test_always_neg(hegel_testcase * tc) {
  int x = hegel_draw_int(tc, 0, 100);
  int r = always_negative(x);
  HEGEL_ASSERT(r >= 0, "always_negative(%d) = %d", x, r);
}

int main(void) {
  hegel_suite * s = hegel_suite_new();
  hegel_suite_add(s, "test_identity", test_identity);
  hegel_suite_add(s, "test_bounds", test_bounds);
  hegel_suite_add(s, "test_always_neg", test_always_neg);

  int rc = hegel_suite_run(s);
  hegel_suite_free(s);

  /* We expect 2 pass, 1 fail => rc should be 1 */
  if (rc != 1) {
    fprintf(stderr, "ERROR: suite should return 1 (got %d)\n", rc);
    return 1;
  }

  /* Now test a suite where everything passes */
  s = hegel_suite_new();
  hegel_suite_add(s, "test_identity", test_identity);
  hegel_suite_add(s, "test_bounds", test_bounds);

  rc = hegel_suite_run(s);
  hegel_suite_free(s);

  if (rc != 0) {
    fprintf(stderr, "ERROR: all-pass suite should return 0 (got %d)\n", rc);
    return 1;
  }

  printf("hegel_suite API works correctly\n");
  return 0;
}
