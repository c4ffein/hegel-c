/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* PT-Scotch distributed graph partitioning test.
**
** Combines MPI_Comm_spawn (no mpiexec) with PT-Scotch's distributed
** graph API.  The parent draws graph parameters, spawns workers, all
** ranks cooperatively build a distributed ring graph and partition it.
**
** This is a real-world integration test: hegel-c property-tests
** distributed HPC graph partitioning without requiring mpiexec.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mpi.h>
#include <ptscotch.h>

#include "hegel_c.h"

#define N_WORKERS   2
#define TOTAL_RANKS (1 + N_WORKERS)
#define MAX_VERT    60

/* MPI type matching SCOTCH_Num (which is int in this build). */
#define MPI_SCOTCH_NUM MPI_INT

static char self_path[4096];

/* ---- Distributed ring graph: each rank owns a slice ---- */

/* Builds the local portion of a ring graph on this rank.
** Caller must free verttab and edgetab AFTER SCOTCH_dgraphExit. */
static int
build_local_ring (
SCOTCH_Dgraph *             dgrafptr,
MPI_Comm                    comm,
int                         nvert_total,
SCOTCH_Num **               verttab_out,
SCOTCH_Num **               edgetab_out)
{
  int rank, size;
  int chunk, start, end, nvert_local;
  SCOTCH_Num * verttab;
  SCOTCH_Num * edgetab;
  int nedge_local;
  int i, rc;

  MPI_Comm_rank (comm, &rank);
  MPI_Comm_size (comm, &size);

  chunk = nvert_total / size;
  start = rank * chunk;
  end   = (rank == size - 1) ? nvert_total : start + chunk;
  nvert_local = end - start;

  *verttab_out = NULL;
  *edgetab_out = NULL;

  if (nvert_local <= 0)
    return -1;

  nedge_local = nvert_local * 2;
  verttab = (SCOTCH_Num *) malloc ((size_t)(nvert_local + 1) * sizeof (SCOTCH_Num));
  edgetab = (SCOTCH_Num *) malloc ((size_t) nedge_local * sizeof (SCOTCH_Num));
  if (!verttab || !edgetab) {
    free (verttab); free (edgetab);
    return -1;
  }

  for (i = 0; i < nvert_local; i++) {
    int global_id = start + i;
    verttab[i] = i * 2;
    edgetab[i * 2]     = (global_id + 1) % nvert_total;
    edgetab[i * 2 + 1] = (global_id + nvert_total - 1) % nvert_total;
  }
  verttab[nvert_local] = nedge_local;

  SCOTCH_dgraphInit (dgrafptr, comm);
  rc = SCOTCH_dgraphBuild (dgrafptr,
                           0,               /* baseval */
                           nvert_local,
                           nvert_local,      /* vertlocmax */
                           verttab,
                           verttab + 1,      /* vendloctab (compact) */
                           NULL, NULL,       /* veloloctab, vlblloctab */
                           nedge_local,
                           nedge_local,      /* edgelocsiz */
                           edgetab,
                           NULL, NULL);      /* edgegsttab, edloloctab */

  if (rc != 0) {
    SCOTCH_dgraphExit (dgrafptr);
    free (verttab); free (edgetab);
    return -1;
  }

  *verttab_out = verttab;
  *edgetab_out = edgetab;
  return 0;
}

/* ---- Common: partition and cleanup ---- */

static void
do_partition (MPI_Comm merged, int nvert_total, int npart,
              SCOTCH_Num * all_parts)
{
  int rank, size, chunk, start, end, nvert_local, i;
  SCOTCH_Dgraph dgrafdat;
  SCOTCH_Strat  stradat;
  SCOTCH_Num *  verttab;
  SCOTCH_Num *  edgetab;
  SCOTCH_Num *  parttab_local;
  int * recvcounts = NULL;
  int * displs = NULL;

  MPI_Comm_rank (merged, &rank);
  MPI_Comm_size (merged, &size);

  chunk = nvert_total / size;
  start = rank * chunk;
  end   = (rank == size - 1) ? nvert_total : start + chunk;
  nvert_local = end - start;

  build_local_ring (&dgrafdat, merged, nvert_total, &verttab, &edgetab);

  parttab_local = (SCOTCH_Num *) calloc ((size_t) nvert_local, sizeof (SCOTCH_Num));

  SCOTCH_stratInit (&stradat);
  SCOTCH_dgraphPart (&dgrafdat, npart, &stradat, parttab_local);
  SCOTCH_stratExit (&stradat);
  SCOTCH_dgraphExit (&dgrafdat);
  free (verttab);
  free (edgetab);

  /* Gather partition labels to rank 0. */
  if (rank == 0) {
    recvcounts = (int *) malloc ((size_t) size * sizeof (int));
    displs = (int *) malloc ((size_t) size * sizeof (int));
  }
  MPI_Gather (&nvert_local, 1, MPI_INT, recvcounts, 1, MPI_INT, 0, merged);
  if (rank == 0) {
    displs[0] = 0;
    for (i = 1; i < size; i++)
      displs[i] = displs[i-1] + recvcounts[i-1];
  }
  MPI_Gatherv (parttab_local, nvert_local, MPI_SCOTCH_NUM,
               all_parts, recvcounts, displs, MPI_SCOTCH_NUM,
               0, merged);

  free (parttab_local);
  free (recvcounts);
  free (displs);
}

/* ---- Worker entry point ---- */

static void
run_worker (void)
{
  MPI_Comm parent, merged;
  int params[2];

  MPI_Comm_get_parent (&parent);
  if (parent == MPI_COMM_NULL) return;

  MPI_Intercomm_merge (parent, 1, &merged);

  MPI_Bcast (params, 2, MPI_INT, 0, merged);

  do_partition (merged, params[0], params[1], NULL);

  MPI_Comm_free (&merged);
  MPI_Comm_free (&parent);
}

/* ---- Layer 2: hegel test ---- */

static void
test_dgraph_part (hegel_testcase * tc)
{
  MPI_Comm intercomm, merged;
  int errcodes[N_WORKERS];
  int rc, i;

  /* Draw all parameters before spawning. */
  int nvert_total = hegel_draw_int (tc, TOTAL_RANKS * 2, MAX_VERT);
  int npart       = hegel_draw_int (tc, 2, 8);

  MPI_Init (NULL, NULL);

  rc = MPI_Comm_spawn (self_path, MPI_ARGV_NULL, N_WORKERS,
                       MPI_INFO_NULL, 0, MPI_COMM_SELF,
                       &intercomm, errcodes);
  HEGEL_ASSERT (rc == MPI_SUCCESS,
                "MPI_Comm_spawn failed (rc=%d)", rc);

  MPI_Intercomm_merge (intercomm, 0, &merged);

  int params[2] = { nvert_total, npart };
  MPI_Bcast (params, 2, MPI_INT, 0, merged);

  SCOTCH_Num * all_parts = (SCOTCH_Num *) calloc ((size_t) nvert_total, sizeof (SCOTCH_Num));

  do_partition (merged, nvert_total, npart, all_parts);

  /* Verify: every vertex assigned to a valid partition. */
  for (i = 0; i < nvert_total; i++) {
    HEGEL_ASSERT (all_parts[i] >= 0 && all_parts[i] < npart,
                  "vertex %d: partition %d not in [0, %d) — nvert=%d",
                  i, (int) all_parts[i], npart, nvert_total);
  }

  {
    char buf[128];
    snprintf (buf, sizeof (buf),
              "nvert=%d npart=%d — all partitions valid",
              nvert_total, npart);
    hegel_note (tc, buf);
  }

  free (all_parts);
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

  SCOTCH_randomSeed (42);
  hegel_run_test (test_dgraph_part);
  return (0);
}
