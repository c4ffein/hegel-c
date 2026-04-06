/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_collections.rs::test_vec_with_mapped_elements
** Rust test does:
**   draw vec of integers mapped by x*2 (max 10 elements)
**   assert all elements are even
*/
#include <stdio.h>

#include "hegel_c.h"

#define BUF_CAP 16

static int double_it (int val, void * ctx) {
  (void) ctx;
  return val * 2;
}

static void test_mapped_list (hegel_testcase * tc) {
  hegel_gen * g = hegel_gen_map_int (
    hegel_gen_int (-50000, 50000), double_it, NULL);
  int buf[BUF_CAP];
  int len = hegel_gen_draw_list_int (tc, g, 0, 10, buf, BUF_CAP);
  hegel_gen_free (g);

  int i;
  for (i = 0; i < len; i++) {
    HEGEL_ASSERT (buf[i] % 2 == 0,
                  "buf[%d]=%d is not even", i, buf[i]);
  }
}

int main (void) {
  hegel_run_test (test_mapped_list);
  return (0);
}
