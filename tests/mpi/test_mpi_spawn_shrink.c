/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* MPI spawn + shrink test: fork mode, no mpiexec.
** Draws 3 independent values (one per rank), scatters them, each rank
** squares its value, parent gathers and sums.  Fails if sum > 20.
**
** With 3 ranks, sum = v0^2 + v1^2 + v2^2.
** Hegel should shrink each value independently to the minimal set
** where the sum still exceeds 20.  Expected: something like [3,3,3]
** (9+9+9=27>20) or [5,0,0] (25>20) — hegel minimizes the byte stream,
** so it'll find the lexicographically smallest combination.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mpi.h>

#include "hegel_c.h"

#define N_WORKERS 2
#define TOTAL     (1 + N_WORKERS)

static char self_path[4096];

static void
run_worker (void)
{
  MPI_Comm parent, merged;
  int my_val, my_result;

  MPI_Comm_get_parent (&parent);
  if (parent == MPI_COMM_NULL) return;

  MPI_Intercomm_merge (parent, 1, &merged);
  MPI_Scatter (NULL, 1, MPI_INT, &my_val, 1, MPI_INT, 0, merged);
  my_result = my_val * my_val;
  MPI_Gather (&my_result, 1, MPI_INT, NULL, 0, MPI_INT, 0, merged);

  MPI_Comm_free (&merged);
  MPI_Comm_free (&parent);
}

static void
test_sum_small (hegel_testcase * tc)
{
  MPI_Comm intercomm, merged;
  int errcodes[N_WORKERS];
  int vals[TOTAL];
  int i, sum = 0, rc;

  /* Draw all inputs before spawning. */
  for (i = 0; i < TOTAL; i++)
    vals[i] = hegel_draw_int (tc, 0, 100);

  MPI_Init (NULL, NULL);
  rc = MPI_Comm_spawn (self_path, MPI_ARGV_NULL, N_WORKERS,
                       MPI_INFO_NULL, 0, MPI_COMM_SELF,
                       &intercomm, errcodes);
  HEGEL_ASSERT (rc == MPI_SUCCESS,
                "MPI_Comm_spawn failed (rc=%d)", rc);

  MPI_Intercomm_merge (intercomm, 0, &merged);

  /* Scatter, square, gather. */
  int my_val;
  MPI_Scatter (vals, 1, MPI_INT, &my_val, 1, MPI_INT, 0, merged);
  int my_result = my_val * my_val;
  int * results = (int *) malloc ((size_t) TOTAL * sizeof (int));
  MPI_Gather (&my_result, 1, MPI_INT, results, 1, MPI_INT, 0, merged);

  for (i = 0; i < TOTAL; i++)
    sum += results[i];
  free (results);

  {
    char buf[128];
    snprintf (buf, sizeof (buf), "vals=[%d,%d,%d] sum_of_squares=%d",
              vals[0], vals[1], vals[2], sum);
    hegel_note (tc, buf);
  }

  HEGEL_ASSERT (sum <= 20,
                "vals=[%d,%d,%d] sum_of_squares=%d > 20",
                vals[0], vals[1], vals[2], sum);

  MPI_Comm_free (&merged);
  MPI_Comm_free (&intercomm);
  MPI_Finalize ();
}

static int
is_spawned_worker (void)
{
  return (getenv ("OMPI_COMM_WORLD_SIZE") != NULL);
}

int
main (int argc, char ** argv)
{
  if (argv[0][0] == '/')
    snprintf (self_path, sizeof (self_path), "%s", argv[0]);
  else {
    char cwd[2048];
    if (getcwd (cwd, sizeof (cwd)))
      snprintf (self_path, sizeof (self_path), "%s/%s", cwd, argv[0]);
    else
      snprintf (self_path, sizeof (self_path), "%s", argv[0]);
  }

  if (is_spawned_worker ()) {
    MPI_Init (&argc, &argv);
    run_worker ();
    MPI_Finalize ();
    return (0);
  }

  hegel_run_test (test_sum_small);
  return (0);
}
