/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Test hegel_run_test_result(): verify it returns 0/1 instead of exiting.
** Runs two tests in one binary — one that passes, one that fails —
** and checks both return values. */

#include "hegel_c.h"
#include <stdio.h>

/* Layer 1: trivial functions */

int identity(int x) { return x; }
int always_negative(int x) { (void)x; return -1; }

/* Layer 2: property tests */

void test_identity_positive(hegel_testcase * tc) {
  int x = hegel_draw_int(tc, 0, 100);
  int r = identity(x);
  HEGEL_ASSERT(r >= 0, "identity(%d) = %d, expected >= 0", x, r);
}

void test_always_negative_positive(hegel_testcase * tc) {
  int x = hegel_draw_int(tc, 0, 100);
  int r = always_negative(x);
  HEGEL_ASSERT(r >= 0, "always_negative(%d) = %d, expected >= 0", x, r);
}

/* Layer 3: runner using _result API */

int main(void) {
  int rc1 = hegel_run_test_result(test_identity_positive);
  int rc2 = hegel_run_test_result(test_always_negative_positive);

  printf("test_identity_positive:          %s (rc=%d)\n",
         rc1 == 0 ? "PASS" : "FAIL", rc1);
  printf("test_always_negative_positive:   %s (rc=%d)\n",
         rc2 == 0 ? "PASS" : "FAIL", rc2);

  if (rc1 != 0) {
    fprintf(stderr, "ERROR: test_identity_positive should have passed\n");
    return 1;
  }
  if (rc2 != 1) {
    fprintf(stderr, "ERROR: test_always_negative_positive should have failed\n");
    return 1;
  }

  printf("hegel_run_test_result API works correctly\n");
  return 0;
}
