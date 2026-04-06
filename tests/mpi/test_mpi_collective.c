/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* MPI collective test: verify that MPI_Allreduce works inside a forked
** hegel child.  Each rank draws a value, then all ranks sum them via
** MPI_Allreduce.  This proves:
** - MPI_Init works inside forked children
** - Cross-rank communication works
** - hegel draws + MPI collectives coexist in the same test function
**
** Run with: mpiexec -n N ./test_mpi_collective
** Each of the N parent processes runs hegel independently, forking
** children that collectively MPI_Allreduce together.
*/

#include <stdio.h>
#include <mpi.h>

#include "hegel_c.h"

/* Layer 1: function under test — distributed sum */

static int
distributed_sum (int local_val)
{
  int global_sum;
  MPI_Allreduce (&local_val, &global_sum, 1, MPI_INT,
                 MPI_SUM, MPI_COMM_WORLD);
  return global_sum;
}

/* Layer 2: hegel test */

static void
test_allreduce (hegel_testcase * tc)
{
  int argc = 0;
  char ** argv = NULL;
  int rank, size, val, total;

  MPI_Init (&argc, &argv);
  MPI_Comm_rank (MPI_COMM_WORLD, &rank);
  MPI_Comm_size (MPI_COMM_WORLD, &size);

  /* Every rank draws from the same range.  Hegel on each rank uses
  ** independent random streams, so values will differ per rank. */
  val = hegel_draw_int (tc, 1, 10);
  total = distributed_sum (val);

  /* Sum of N values each in [1,10] must be in [N, 10*N]. */
  HEGEL_ASSERT (total >= size && total <= 10 * size,
                "rank=%d val=%d total=%d size=%d", rank, val, total, size);

  MPI_Finalize ();
}

/* Layer 3: runner */

int
main (void)
{
  hegel_run_test (test_allreduce);
  return (0);
}
