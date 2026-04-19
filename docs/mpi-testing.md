<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Property-testing MPI code with hegel-c

This document covers how to write hegel-c property tests for MPI code,
the patterns that work, the pitfalls we found, and why.

All findings are based on OpenMPI 5.0.7 on Linux.  Only OpenMPI has
been tested — MPICH and other implementations (Intel MPI, Cray MPI)
have not been verified.  The env vars (`OMPI_MCA_btl`, worker detection
via `OMPI_COMM_WORLD_SIZE`) are OpenMPI-specific.  Contributions
testing other implementations are welcome.

## Recommended pattern: MPI_Comm_spawn (no mpiexec)

The cleanest approach.  No `mpiexec` required — the test binary launches
its own MPI workers.

```
./test_mpi_spawn    (single process, no special launcher)

hegel parent (no MPI)
  |
  +-- fork() --> child:
        1. hegel_draw_int(tc, ...) x N   <-- through pipes to parent
        2. MPI_Init (singleton mode)
        3. MPI_Comm_spawn(self, N_WORKERS)
        4. MPI_Intercomm_merge --> intracomm
        5. MPI_Scatter(vals)              <-- through MPI to workers
        6. MPI_Gather(results)
        7. HEGEL_ASSERT(...)
        8. MPI_Comm_free + MPI_Finalize
```

Two independent communication channels:
- **Pipes**: forked child <-> hegel parent (draw requests/responses)
- **MPI**: child <-> spawned workers (collectives)

They never interfere.  The binary serves two roles: test runner (when
launched directly) and worker (when spawned by `MPI_Comm_spawn`).  The
worker path is detected via `OMPI_COMM_WORLD_SIZE` env var (OpenMPI-
specific — would need adaptation for other MPI implementations).

See `tests/mpi/test_mpi_spawn.c` for the full working example.

### Key rules

**Draw all parameters BEFORE spawning.**  All `hegel_draw_*` calls
should happen before `MPI_Init` and `MPI_Comm_spawn`:

<!-- /ignore mpi-pattern: distilled illustration of "draw before spawn" rule; real example is tests/mpi/test_mpi_spawn.c -->
```c
static void test_fn(hegel_testcase *tc) {
    // GOOD: draw first, spawn second
    int v0 = hegel_draw_int(tc, 0, 100);
    int v1 = hegel_draw_int(tc, 0, 100);
    int v2 = hegel_draw_int(tc, 0, 100);

    MPI_Init(NULL, NULL);
    MPI_Comm_spawn(...);
    // distribute v0, v1, v2 to workers
}
```

Why:
- Hegel controls the draw sequence for shrinking.  Interleaving draws
  with MPI operations makes the byte stream depend on MPI timing.
- If an early draw triggers `hegel_assume` (discard), you skip the
  expensive spawn entirely.
- Clearer separation: "what to test" (draws) vs "how to test" (MPI).

**Draw one value per rank** for heterogeneous inputs.  If you draw a
single value and multiply by rank, you're testing arithmetic, not MPI.
Draw N independent values and scatter them:

<!-- /ignore mpi-pattern: distilled per-rank draw idiom; real example is tests/mpi/test_mpi_spawn.c -->
```c
int vals[N_RANKS];
for (int i = 0; i < N_RANKS; i++)
    vals[i] = hegel_draw_int(tc, 0, 100);

// scatter vals -- each rank gets genuinely different data
MPI_Scatter(vals, 1, MPI_INT, &my_val, 1, MPI_INT, 0, merged);
```

Hegel can then independently shrink each rank's input.  In our shrink
test, hegel found `vals=[0, 0, 5]` — it figured out only rank 2's
input matters and zeroed the others.

**Use `MPI_Intercomm_merge` after spawn.**  `MPI_Comm_spawn` returns an
intercommunicator.  OpenMPI 5.x has bugs with collectives (`MPI_Bcast`,
`MPI_Scatter`, `MPI_Gather`) on intercommunicators in singleton spawn
mode — they hang or exit with code 13.  Merging into an intracommunicator
fixes it:

<!-- /ignore mpi-pattern: intercomm merge illustration with parent/worker annotations; real example is tests/mpi/test_mpi_spawn.c -->
```c
MPI_Comm intercomm, merged;
MPI_Comm_spawn(self_path, MPI_ARGV_NULL, N_WORKERS,
               MPI_INFO_NULL, 0, MPI_COMM_SELF, &intercomm, errcodes);

// Parent side: high=0 (gets rank 0)
MPI_Intercomm_merge(intercomm, 0, &merged);

// Worker side: high=1 (gets ranks 1..N)
MPI_Intercomm_merge(parent, 1, &merged);

// Now use merged for all collectives
MPI_Scatter(..., merged);
MPI_Gather(..., merged);
```

### Shared memory exhaustion (/dev/shm)

Each `MPI_Comm_spawn` allocates ~16MB in `/dev/shm` for shared-memory
transport.  Hegel runs ~100 test cases during generation plus more
during shrinking — that's hundreds of spawns.  On systems with small
`/dev/shm` (e.g., Docker containers with 64MB default), this fills up:

```
It appears as if there is not enough space for /dev/shm/sm_segment...
```

OpenMPI doesn't abort — it falls back to slower transport.  But the
warnings are noisy and the fallback may be unreliable under extreme
pressure.

**Fix: disable shared-memory transport for spawn tests.**  Set
`OMPI_MCA_btl=tcp,self` to force TCP transport:

```bash
OMPI_MCA_btl=tcp,self ./test_mpi_spawn
```

The `tests/mpi/Makefile` does this automatically for spawn tests.  The
performance difference (TCP vs shm) is negligible for property tests —
the bottleneck is process creation, not communication latency.

To manually clean up leaked segments:

```bash
rm -f /dev/shm/sm_segment.*
```

## Alternative pattern: mpiexec (multi-rank, fork mode)

```bash
mpiexec -n 3 ./test_mpi_collective
```

Each rank runs an independent hegel instance.  The forked child calls
`MPI_Init` (no `MPI_Init` in `main()`), uses `MPI_COMM_WORLD` for
collectives.

This works for **PASS tests** — all ranks run roughly the same number
of test cases and their forked children call `MPI_Init` at similar
times, so collectives succeed.

**Does NOT work for FAIL/SHRINK tests.**  During shrinking, each rank's
hegel replays different test cases at different times.  Collectives
require all ranks to participate simultaneously — when rank 0 is on
test case 42 and rank 1 is on test case 17, `MPI_Allreduce` deadlocks.

Use this pattern when you need to test code that uses `MPI_COMM_WORLD`
directly and only need to verify it doesn't crash (PASS tests).

## What we tried and why it doesn't work

**`MPI_Init` in main + `MPI_Init` in forked child**: illegal.
`MPI_Finalize` + `MPI_Init` is forbidden by the MPI standard.  The
fork inherits the finalized state, and the child's `MPI_Init` fails
with "already finalized."

**Collectives on raw intercommunicators from singleton spawn**: hangs or
exits with code 13.  OpenMPI 5.x bug specific to singleton spawn mode.
Fixed by `MPI_Intercomm_merge`.

**`MPI_Comm_spawn` inside hegel fork mode child (without merge)**: the
spawn works, but collective calls on the intercommunicator hang.  Same
root cause — use `MPI_Intercomm_merge`.

**`fork() + execvp("mpiexec")` in the child**: works in principle (we
tested it), but awkward to integrate with hegel's pipe IPC.  The child
would need to pass draw results to mpiexec-launched processes somehow.
`MPI_Comm_spawn` is simpler because the child IS rank 0 of the spawned
group.

## Summary

| Pattern | mpiexec? | Fork mode? | Shrinking? | Use case |
|---------|----------|------------|------------|----------|
| `MPI_Comm_spawn` + merge | No | Yes | Yes | Recommended for all MPI tests |
| `mpiexec -n N` | Yes | Yes | PASS only | Testing `MPI_COMM_WORLD` code |
| `fork + execvp mpiexec` | Internally | Yes | Possible | Not recommended (complex) |

*Tested with OpenMPI 5.0.7 only.  Results with other MPI implementations
are unverified.*
