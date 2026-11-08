<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Pair 2, crash: `name_len (id) > 0` for `id` in [0, 100]

`name_of` returns NULL for id >= 42 and `name_len` dereferences it: a SIGSEGV ([sut.h](sut.h)).

| file | code lines | of which boilerplate | what it does |
|---|---:|---:|---|
| [lite_crash.c](lite_crash.c) | 49 | 31 | forks for the call under test; a signal or non-zero exit is a failure; prints `FAILED: id = 42` |
| [raw_crash.c](raw_crash.c) | 71 | 56 | the careful program **without** a fork, written the natural way |
| [raw_crash_fork.c](raw_crash_fork.c) | 86 | 68 | the careful program with its own fork, the signal named in the origin |
| [hc_crash.c](hc_crash.c) | 14 | 5 | hegel-c fork mode: no code for the crash at all |
| [hc_crash_iso.c](hc_crash_iso.c) | 16 | 5 | hegel-c nofork with `HEGEL_ISOLATED` around the call |

Measured over 20 seeds (see [README.md](README.md)):

| program | result | executions | ms |
|---|---|---:|---:|
| lite (own fork) | id = 42, every seed | 58 | 17 |
| careful, no fork | **killed by SIGSEGV**: exit 139 after 2 to 5 cases, no input, no blob | 2 | 3 |
| careful + own fork | id = 42 | 59 | 19 |
| hegel-c fork | id = 42, with the signal as origin | 58 | 31 |
| hegel-c nofork, no block | killed by SIGSEGV, like the careful no-fork program | 2 | 3 |
| hegel-c nofork + `HEGEL_ISOLATED` | id = 42 | 58 | 19 |

## Why "lite" has to fork

Without isolation the first crashing input kills the run, and the engine never shrinks or reports anything. So the shortest *correct* program needs the fork: draw in the engine's process, run the call in a child, turn a signal into `INTERESTING`. That costs 6 lines and adds nothing to the time: 17 ms against 19 ms for hegel-c's isolated block.

## lite vs careful + own fork (49 vs 86 lines)

Careful names the signal in the origin (`"crash: Segmentation fault"`), so a SIGSEGV and a SIGABRT shrink as different bugs. Lite counts any crash or failed check as one failure. Careful also has the usual checks, frees and blob ([minimal.md](minimal.md)).

What neither has is a channel back from the child: an assertion message, a note or an assume inside the forked code can't reach the report. The child can only exit or die.

## What hegel-c adds

- In fork mode (`hc_crash.c`) every test case is already a child, so the crash needs no code and its signal becomes the origin. The cost is the per-draw pipe round trip (31 ms here).
- In nofork mode, `HEGEL_ISOLATED (tc) { ... }` gives one block the same isolation, at the same cost as a hand-written fork. The block's child can send back an assert's message, notes and a verdict, which a hand-rolled fork can't. A crash and a failed assert inside the block share one origin.
