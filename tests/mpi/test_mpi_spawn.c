/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* MPI_Comm_spawn + fork mode: property-test MPI code WITHOUT mpiexec.
**
** Each test case runs in a forked child (crash isolation + shrinking).
** The child draws one value per rank BEFORE spawning, then spawns
** workers via MPI_Comm_spawn, scatters the values, each rank squares
** its value, parent gathers and verifies.
**
** Two independent communication channels coexist:
** - Pipes: forked child <-> hegel parent (draw requests)
** - MPI:   child <-> spawned workers (collectives)
**
** The MPI_Intercomm_merge step is required: OpenMPI 5.x has issues
** with collectives on raw intercommunicators from spawn in singleton
** mode, but merged intracommunicators work perfectly.
**
** Alternative approach (not used here): fork() + execvp("mpiexec", ...)
** also works for launching multi-rank MPI jobs from the forked child.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mpi.h>

#include "hegel_c.h"

#define N_WORKERS 2
#define TOTAL     (1 + N_WORKERS)  /* parent + workers */

/* ---- Worker entry point ---- */

static void
run_worker (void)
{
  MPI_Comm parent, merged;
  int my_val, my_result;

  MPI_Comm_get_parent (&parent);
  if (parent == MPI_COMM_NULL)
    return;

  MPI_Intercomm_merge (parent, 1, &merged);

  /* Receive my value from parent via scatter. */
  MPI_Scatter (NULL, 1, MPI_INT,
               &my_val, 1, MPI_INT, 0, merged);

  /* Each worker squares its value. */
  my_result = my_val * my_val;

  /* Send result back to parent. */
  MPI_Gather (&my_result, 1, MPI_INT,
              NULL, 0, MPI_INT, 0, merged);

  MPI_Comm_free (&merged);
  MPI_Comm_free (&parent);
}

/* ---- Layer 1: function under test ---- */

/* Scatter vals to all ranks, each squares its value, gather results.
** Returns the sum of squares. */
static int
distributed_sum_of_squares (MPI_Comm merged, int * vals, int n)
{
  int my_val, my_result;
  int * results = (int *) malloc ((size_t) n * sizeof (int));
  int i, sum = 0;

  MPI_Scatter (vals, 1, MPI_INT,
               &my_val, 1, MPI_INT, 0, merged);

  my_result = my_val * my_val;

  MPI_Gather (&my_result, 1, MPI_INT,
              results, 1, MPI_INT, 0, merged);

  for (i = 0; i < n; i++)
    sum += results[i];

  free (results);
  return sum;
}

/* ---- Layer 2: hegel test ---- */

static char self_path[4096];

static void
test_spawn (hegel_testcase * tc)
{
  MPI_Comm intercomm, merged;
  int errcodes[N_WORKERS];
  int vals[TOTAL];
  int sum, expected, rc, i;

  /* Draw ALL inputs BEFORE spawning.  Keeps hegel draws independent
  ** of MPI, avoids wasting a spawn if hegel discards this case. */
  for (i = 0; i < TOTAL; i++)
    vals[i] = hegel_draw_int (tc, 0, 50);

  MPI_Init (NULL, NULL);

  rc = MPI_Comm_spawn (self_path, MPI_ARGV_NULL, N_WORKERS,
                       MPI_INFO_NULL, 0, MPI_COMM_SELF,
                       &intercomm, errcodes);
  HEGEL_ASSERT (rc == MPI_SUCCESS,
                "MPI_Comm_spawn failed (rc=%d)", rc);

  MPI_Intercomm_merge (intercomm, 0, &merged);

  sum = distributed_sum_of_squares (merged, vals, TOTAL);

  expected = 0;
  for (i = 0; i < TOTAL; i++)
    expected += vals[i] * vals[i];

  {
    char buf[128];
    snprintf (buf, sizeof (buf),
              "vals=[%d,%d,%d] sum=%d expected=%d",
              vals[0], vals[1], vals[2], sum, expected);
    hegel_note (tc, buf);
  }

  HEGEL_ASSERT (sum == expected,
                "vals=[%d,%d,%d] sum=%d expected=%d",
                vals[0], vals[1], vals[2], sum, expected);

  MPI_Comm_free (&merged);
  MPI_Comm_free (&intercomm);
  MPI_Finalize ();
}

/* ---- Layer 3: runner ---- */

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

  /* Main process: no MPI here. Hegel forks children, each child
  ** does MPI_Init/Spawn/Finalize independently. */
  hegel_run_test (test_spawn);
  return (0);
}
