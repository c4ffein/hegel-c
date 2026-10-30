/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* Regression demo for the fork-mode orphan bug fixed 2026-04-12.
**
** History: while building the first Scotch schema test, we observed
** that schemas using HEGEL_ARRAY_INLINE caused hegel_run_test_n to
** silently spawn ~10% extra fork children. They ended up reparented
** to init, ran the test body with garbage draw values (`c=0` despite
** schema range [2, 8]), and silently swallowed any assertion failures.
**
** Root cause: `tc.draw()` in `parent_serve` can panic with the
** `__HEGEL_STOP_TEST` sentinel when hegeltest's engine decides to
** discard a test case mid-generation. The original `run_forked` let
** that panic propagate up without ever reaching `waitpid`. The child
** stayed alive, blocked on a pipe read, and only unblocked when the
** main process exited and closed all FDs. The child-side draw paths
** then read EOF, returned 0 (because they ignored pipe_read_exact's
** return value), and ran the test body with all-zero draws.
**
** Fix lives in `rust-version/src/lib.rs`:
**   1. `run_forked` wraps `parent_serve` in catch_unwind, always
**      closes pipes + waitpids the child before resume_unwind.
**   2. Child-side `hegel_draw_*` functions now `_exit(0)` on
**      pipe_read_exact failure instead of returning garbage zeros.
**
** This file is kept as a hand-runnable regression demo. With the
** fix in place, the binary should produce exactly N (requested)
** SCH lines, all with `ppid != 1` and `c` in [2, 8].
**
** Verification:
**   ./test_array_inline_orphan_repro 2>out.txt
**   grep -c SCH out.txt              # should equal requested case count
**   grep -c 'ppid=1 ' out.txt        # should be 0
**   awk -F'c=' '/SCH/ {n=$2+0; if (n<2||n>8) print}' out.txt   # empty
*/
**
** Not in the default test run (Makefile `TESTS_PASS`) — this is a
** standalone investigation binary.  Run manually:
**   make test_array_inline_orphan_repro
**   ./test_array_inline_orphan_repro 2>out.txt
**   grep 'ppid=1 ' out.txt  # should be empty; currently isn't
**
** What I ruled out while bisecting:
**   - Not `hegel_set_case_setup` (removed, still orphans)
**   - Not Scotch itself (removed all Scotch calls, still orphans)
**   - Not `hegel_shape_free` (leak the shape, still orphans)
**   - Not draw volume (200 primitive draws + sleep: 0 orphans)
**   - Not wallclock time (5ms usleep with primitive draws: 0 orphans)
**   - Not spans (primitive draws wrapped in spans: 0 orphans)
**   - Not 3-int flat schema (no HEGEL_ARRAY_INLINE: 0 orphans)
**   - IS triggered by HEGEL_ARRAY_INLINE (adding it: orphans return)
**
** Next step (not done this session): instrument hegel_gen.c's
** ARRAY_INLINE draw path and hegeltest's Rust-side runner to see
** where the parent gives up before the child is done.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "hegel_c.h"
#include "hegel_gen.h"

typedef struct Elem { int x; int y; } Elem;
typedef struct Thing {
  int    a;
  Elem * items;
  int    n_items;
  int    c;
} Thing;
static hegel_schema_t thing_schema;

static
void
test_range (
hegel_testcase *            tc)
{
  Thing *             t;
  hegel_shape *       sh;

  sh = hegel_schema_draw (tc, thing_schema, (void **) &t);
  usleep (5000);
  fprintf (stderr, "SCH pid=%d ppid=%d a=%d n_items=%d c=%d\n",
           getpid (), getppid (), t->a, t->n_items, t->c);
  hegel_shape_free (sh);
}

int
main (void)
{
  hegel_schema_t elem = HEGEL_STRUCT (Elem,
      HEGEL_INT (0, 49),
      HEGEL_INT (0, 49));
  thing_schema = HEGEL_STRUCT (Thing,
      HEGEL_INT (3, 50),
      HEGEL_ARRAY_INLINE (elem, sizeof (Elem), 0, 200),
      HEGEL_INT (2, 8));
  hegel_run_test_n (test_range, 50);
  hegel_schema_free (thing_schema);
  return (0);
}
