/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: hegel_target steers generation toward a bug random draws miss.
**
** Layer 1: frame_length() — sums chunk sizes into a 16-bit signed
**          length field, so it wraps once the total passes 32767.
**          With 8 chunks of 0..5000 that takes an average chunk above
**          4096: deep in the tail for random generation.
** Layer 2: the property "frame_length == true total", with
**          hegel_target (tc, total, "total") on every case.
** Layer 3: seeded runs, each observed from the body:
**   - targeted (default phases): every seed finds the wrap;
**   - HEGEL_SKIP_TARGET, same seeds and budget: none does — so it is
**     the targeting phase, not luck, that finds it;
**   - fork mode: the TARGET op crosses the pipe and still steers;
**   - with shrinking on, the failure shrinks to total = 32768, the
**     first value that wraps.
**
** The default budget (100 cases) ends before the target phase gets
** going; these runs use 1000.
**
** Expected: EXIT 0.
*/
#include <stdio.h>
#include <stdint.h>

#include "hegel_c.h"

#define N_CHUNKS      8
#define MAX_CHUNK     5000
#define N_SEEDS       3
#define N_CASES       1000

/* ---- Layer 1: function under test ---- */

static
int
frame_length (
const int *         sizes,
int                 n)
{
  int16_t             len = 0;              /* bug: too narrow */
  int                 i;

  for (i = 0; i < n; i ++)
    len = (int16_t) (len + sizes[i]);
  return (len);
}

/* ---- Layer 2: hegel test ---- */

static long           last_failing;

static
void
test_frame (
hegel_testcase *    tc)
{
  int                 sizes[N_CHUNKS];
  long                total = 0;
  int                 i;

  for (i = 0; i < N_CHUNKS; i ++) {
    sizes[i] = hegel_draw_int (tc, 0, MAX_CHUNK);
    total += sizes[i];
  }
  hegel_target (tc, (double) total, "total");
  if (frame_length (sizes, N_CHUNKS) != total)
    last_failing = total;
  HEGEL_ASSERT (frame_length (sizes, N_CHUNKS) == total, "total=%ld", total);
}

/* ---- Layer 3: runner ---- */

static int            failures;

static
void
check (
int                 ok,
const char *        what)
{
  printf ("  %-58s %s\n", what, ok ? "ok" : "FAILED");
  if (!ok)
    failures ++;
}

/* Number of seeds 1..N_SEEDS whose run fails (finds the wrap). */
static
int
seeds_finding (
unsigned            skip)
{
  int                 seed;
  int                 found = 0;

  for (seed = 1; seed <= N_SEEDS; seed ++) {
    hegel_run_options o = { .test_cases = N_CASES, .has_seed = 1, .seed = seed,
                            .skip_phases = HEGEL_SKIP_SHRINK | skip };
    if (hegel_run_test_nofork_with (test_frame, &o) != 0)
      found ++;
  }
  return (found);
}

int
main (void)
{
  printf ("Targeting...\n");

  check (seeds_finding (0) == N_SEEDS, "targeted: every seed finds the wrap");
  check (seeds_finding (HEGEL_SKIP_TARGET) == 0, "HEGEL_SKIP_TARGET: no seed finds it");

  {
    hegel_run_options o = { .test_cases = N_CASES, .has_seed = 1, .seed = 1,
                            .skip_phases = HEGEL_SKIP_SHRINK };
    check (hegel_run_test_with (test_frame, &o) != 0, "fork mode: targeting steers across the pipe");
  }

  {
    hegel_run_options o = { .test_cases = N_CASES, .has_seed = 1, .seed = 1 };
    last_failing = 0;
    hegel_run_test_nofork_with (test_frame, &o);
    check (last_failing == 32768, "shrunk to total = 32768");
  }

  if (failures) {
    fprintf (stderr, "%d check(s) failed\n", failures);
    return (1);
  }
  return (0);
}
