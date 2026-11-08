<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Pair 3, graph: `adj_build` must store 2 entries per edge

`nvert` in [1, 20], edges from the collection protocol (at most 40), endpoints < `nvert`, a span per graph and per edge. `adj_build` keeps only 3 neighbours per vertex and silently drops the 4th ([sut.h](sut.h)).

| file | code lines | of which boilerplate | what it does |
|---|---:|---:|---|
| [lite_graph.c](lite_graph.c) | 66 | 31 | edges in a fixed array, span labels as constants, every engine call in the generator checked |
| [raw_graph.c](raw_graph.c) | 106 | 61 | edges `realloc`ed and freed on both exit paths, labels from `hegel_label_from_name`, the usual checks and report |
| [hc_graph.c](hc_graph.c) | 38 | 5 | engine calls without return codes, edges from `hegel_realloc` (freed with the case), the shape printed with `hegel_note` |

Measured over 20 seeds (see [README.md](README.md)): all three shrink to `nvert=1 nedge=2 edges=[(0,0) (0,0)]` on every seed. The three generators make the same choices: lite runs exactly as many executions as hegel-c on each of the 20 seeds (2388, median), careful exactly one more (its blob replay), and careful's and hegel-c's blobs are byte-identical (lite prints none). 74 ms for both libhegel versions, 76 ms for hegel-c nofork, 1850 ms in fork mode.

## Every engine call can end the case

The generator makes 3 + 4 per edge engine calls. Any of them can return `HEGEL_E_STOP_TEST` (the case is out of budget), and then the case must end as an overrun, without evaluating the property on a half-built graph. So lite can't drop these checks; it chains them with `||`. That's why the property code is 35 lines in lite against 33 in hegel-c, where the runtime handles the stop.

## lite vs careful (66 vs 106 lines)

- **Memory.** Lite uses a stack array of `MAX_EDGE` edges, valid because the collection's maximum bounds the count. Careful grows the array with `realloc`. Then every early return must free it: careful frees on 2 paths, and the leak check (ASan + LSan) passes only because it does.
- **Labels.** Careful derives span labels from names, as upstream does; lite uses the constants 1 and 2. Labels only have to be consistent within the program.
- The usual checks, origin, blob and frees ([minimal.md](minimal.md)).

## What hegel-c adds (38 lines)

The generator reads like the specification, with no return codes. A stop anywhere unwinds the case. `hegel_realloc` memory is freed with the case on every exit path, so variable-size structures need no cleanup code. `hegel_note` takes a string, so printing a structure takes the same 5-line `fmemopen` dance as in lite.

The cost is fork mode: 1850 ms, 25 times the libhegel time, because each of the ~100 engine calls per case crosses a pipe. Nofork runs at libhegel speed.
