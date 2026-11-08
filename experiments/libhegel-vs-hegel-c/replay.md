<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Pair 7, replay: run one recorded failure again

Pair 1's property, rerun from a blob instead of exploring. That's how you check a fix: the failure either comes back, or the blob "did not reproduce".

| file | code lines | of which boilerplate | what it does |
|---|---:|---:|---|
| [lite_replay.c](lite_replay.c) | 48 | 34 | `REPRO=<blob>` runs `hegel_run_start_blob` instead of `hegel_run_start` |
| [raw_replay.c](raw_replay.c) | 78 | 63 | the same in the careful program, which also re-prints the given blob (a replayed failure carries none) |
| [hc_minimal.c](hc_minimal.c) | 14 | 5 | nothing to add: `HEGEL_C_REPRODUCE='1:<blob>'` works on any hegel-c binary |

Measured (see [README.md](README.md)): all three replay the n = 5 blob to the same failure (exit 1) and say a passing blob (n = 4) "did not reproduce" (exit 0).

## Lite has the replay, but nothing to replay

Replaying costs lite 5 lines: read the variable, pick the start function, print "did not reproduce". But lite never prints a blob when a test fails ([minimal.md](minimal.md)), so a user has nothing to paste into `REPRO`. A usable replay needs the failure report to print the blob, which takes the careful program's result walk (`hegel_run_result_failure`, `hegel_failure_reproduction_blob`).

Careful prints the blob after every failure, and its replay path re-prints it, because a failure replayed from a blob carries no blob of its own.

## What hegel-c adds

Every failure report ends with the replay command, ready to copy:

```
reproduce: HEGEL_C_REPRODUCE='1:AAEAAAAACgEAAAAF'
```

The `1:` prefix selects one run in a binary that has several (a suite, or several `hegel_run_test` calls); the other runs are skipped. Replay needs no code in the test.
