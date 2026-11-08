<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Pair 4, tree: `expr_simplify` must keep the value

A `+` / `-` expression tree from the recursion protocol (leaves 0..9, depth 50, 100 leaves, a span per node). `expr_simplify` rewrites `0 - b` to `b`, which should be `-b` ([sut.h](sut.h)).

| file | code lines | of which boilerplate | what it does |
|---|---:|---:|---|
| [lite_tree.c](lite_tree.c) | 80 | 30 | nodes from a fixed pool reset at each attempt, the retry loop written out |
| [raw_tree.c](raw_tree.c) | 136 | 74 | nodes in an arena freed on every path, the retry loop, the usual checks and report |
| [hc_tree.c](hc_tree.c) | 35 | 5 | a 13-line node function and `hegel_draw_recursive`: the helper runs the protocol |

Measured over 20 seeds (see [README.md](README.md)): all three shrink to `(0 - 1)` on every seed. Lite runs exactly as many executions as hegel-c on each of the 20 seeds (1992, median), careful exactly one more, and careful's and hegel-c's blobs are byte-identical: the same choices, whoever runs the protocol. 90 ms lite, 98 ms careful, 143 ms hegel-c nofork, 3856 ms in fork mode.

## The part lite can't shorten: the retry loop

The recursion protocol has two "start again" signals, and they need different handling:

- a **leaf over the budget** returns `HEGEL_E_RETRY` from `hegel_recursion_leaf`. You must call `hegel_recursion_retry`, then build again;
- a **tree the engine prices out** returns `HEGEL_E_RETRY` from `hegel_recursion_finish`. You build again **without** calling retry; `hegel.h` forbids the call here.

Mixing them up isn't caught by anything at compile time. The experiment's first draft of `raw_tree.c` did exactly that, and only a reread of `hegel.h` found it. Lite's loop is 7 lines and has to be exactly right; so does careful's.

## lite vs careful (80 vs 136 lines)

- **Memory.** An abandoned attempt leaves a half-built tree. Lite takes nodes from a static pool and resets it at each attempt, so there is nothing to unwind: 100 leaves means at most 199 nodes per attempt. Careful uses an arena freed on every path, and also frees the nodes `expr_simplify` unlinks; its first draft leaked those.
- The usual checks, origin, blob and frees ([minimal.md](minimal.md)).

## What hegel-c adds (35 lines)

`hegel_draw_recursive (tc, 50, 100, node_fn, ctx)` runs branch / leaf / retry / finish itself, so the user writes only "build one node". An over-budget attempt jumps back and its `hegel_alloc` nodes are freed, and each node gets its span. The choices are the same as lite's and careful's: same counterexample, same blob.

Fork mode is the most expensive case here: 3856 ms, 43 times libhegel, because a tree makes many engine calls per case. Nofork costs 1.5 times libhegel.
