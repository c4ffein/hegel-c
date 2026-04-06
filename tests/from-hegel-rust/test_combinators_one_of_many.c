/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_combinators.rs::test_one_of_many
** Rust test does:
**   create 10 generators: integers(i*100..(i+1)*100) for i in 0..10
**   draw from one_of, assert value is in [0, 1000]
*/
#include <stdio.h>

#include "hegel_c.h"

#define N_GENS 10

static void test_one_of_many (hegel_testcase * tc) {
  hegel_gen * gens[N_GENS];
  int i;

  for (i = 0; i < N_GENS; i++)
    gens[i] = hegel_gen_int (i * 100, (i + 1) * 100);

  hegel_gen * g = hegel_gen_one_of (gens, N_GENS);
  int val = hegel_gen_draw_int (tc, g);
  hegel_gen_free (g);

  HEGEL_ASSERT (val >= 0 && val <= 1000,
                "val=%d not in [0, 1000]", val);
}

int main (void) {
  hegel_run_test (test_one_of_many);
  return (0);
}
