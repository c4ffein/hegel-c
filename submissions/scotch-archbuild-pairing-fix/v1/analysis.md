# `archDecoArchBuild()`: terminal vertices paired with the wrong domains

Analysis of `src/libscotch/arch_build.c`, line 309 (v7.0.12 numbering), the
follow-up commit e6c094fc, and a proposed alternative fix — with a regression
test for Scotch's check suite.

Analysis and tests by Claude (Anthropic, model Fable 5), 2026-07-04, in a
working session run and reviewed by c4ffein; the full transcript is available
as `conversation.txt`.

## 1. Summary

`archDecoArchBuild()` builds a `deco 0` architecture by recursive bipartition
of the target graph, then fills the terminal vertex array which associates,
for each target vertex, its label and weight with the terminal number (`num`)
of the domain that contains it in the bipartition tree.

Two successive versions of the domain lookup are both incorrect:

* **v7.0.12 and earlier**: `mapDomain (&mappdat, tgtvertnum - baseval)`
  subtracts the base twice — out-of-bounds heap read and wrong-vertex lookups
  for any graph with `baseval != 0`.
* **e6c094fc** (2026-07-04): `&mappdat.domntab[termdomnum]` assumes that
  domain index *i* is the domain of terminal vertex *i*. This does not hold:
  domain indices are assigned in job-splitting order, not vertex order. The
  labels get attached to the wrong leaves of the decomposition tree, at *any*
  base value — including base 0, which the previous code handled correctly.

Proposed fix: `mapDomain (&mappdat, tgtvertnum)` — keep the per-vertex
lookup, drop only the spurious rebasing (`mappdat.parttax` is a based array).

## 2. The original defect (v7.0.12)

`mapDomain(m, i)` expands to `&m->domntab[m->parttax[i]]` (`mapping.h:162`).
In `archDecoArchBuild()`:

* `mappdat.parttax` is explicitly rebased at initialization
  (`arch_build.c:186`: `mappdat.parttax -= tgtgrafptr->baseval;`), so its
  valid indices are `[baseval, baseval + vertnbr)`.
* All writes during the bipartition loop use based indices:
  `arch_build.c:267` writes `parttax[actvertnum]` with `actvertnum` running
  from `baseval`; `arch_build.c:259` writes `parttax[vnumtax[actvertnum]]`,
  and induced-graph `vnumtax` entries are based original-graph vertex numbers
  (`graph_induce.c:184`, composed through nested inductions at
  `graph_induce.c:355`).
* Every other `mapDomain()` call site in libscotch passes based vertex
  numbers directly (`kgraph_map_rb.c:630`, `kgraph_map_rb.c:651`,
  `kgraph_map_fm.c:670`).

Line 309 alone passes `tgtvertnum - baseval`, i.e. reads
`parttax_orig[tgtvertnum - 2*baseval]`. With `baseval = 1`:

* for the first vertex, `parttax_orig[-1]` — an out-of-bounds read of the
  word before the allocated block, whose garbage value then indexes
  `domntab[]` (a second, unbounded read);
* for every other vertex, the domain of *the previous vertex*.

Observable effect (reproduced by the attached test): at base 1 the root
domain of the built architecture holds `n - 1` terminals instead of `n`; one
processor disappears from the architecture, subtree sizes and weights are
wrong, and the garbage `num` can corrupt `termdommax` and the arrays sized
from it in `archDecoArchBuild2()`. With `baseval = 0` the two conventions
coincide, which is why base-0 usage never showed the problem.

This is presumably also why the
`test_scotch_arch_deco data/m4x4_b100000.grf` check is commented out in
`src/check/Makefile`: with base 100000, the lookup reads ~100000 elements
before the array. That check should become enableable again once the lookup
is fixed.

## 3. Why e6c094fc does not fix it

The patch replaces the lookup with `&mappdat.domntab[termdomnum]`, asserting
the invariant: *the domain at index `i` of `domntab` is the terminal domain
of vertex `i`*. The job loop does not maintain that invariant:

* New domain indices are handed out in **split-creation order**
  (`arch_build.c:272`: `jobsubptr->domnum = mappdat.domnnbr ++`), and jobs
  are processed from a **LIFO list** (`arch_build.c:278-285`, sub-job pushed
  first, original job pushed last, hence popped first).
* At each split, the original job keeps the part containing its first
  vertex; the new domain index goes to the *other* part.

As soon as one branch of the recursion is processed in depth before another
(which happens whenever both halves of a split need further splitting), the
domain-index order diverges from the vertex order.

**Minimal counterexample.** A 4-vertex path `0-1-2-3` with edge loads
`{169, 199, 169}` (all within a factor of 2, middle edge strictly largest).
After inversion by `graphIelo()`, the cheapest cuts force the bipartition
sequence:

1. `{0,1,2,3}` → `{0,1} | {2,3}`: new **dom1 = {2,3}**; LIFO holds
   [job0 = {0,1}, job1 = {2,3}].
2. Pop job0 = `{0,1}` → `{0} | {1}`: new **dom2 = {1}**.
3. Pop job1 = `{2,3}` → `{2} | {3}`: new **dom3 = {3}**, dom1 keeps `{2}`.

Final vertex → domain map: `0→dom0, 1→dom2, 2→dom1, 3→dom3`. The patch
pairs vertex *i* with domain *i*, i.e. swaps vertices 1 and 2. In terminal
numbers (vcmplt tree, root 1, sons `2n`/`2n+1`): dom0 ends at leaf 4,
dom2 at leaf 5, dom1 at leaf 6, dom3 at leaf 7. Correct pairing is
`v0↔4, v1↔5, v2↔6, v3↔7`; the patch produces `v0↔4, v1↔6, v2↔5, v3↔7`, so
the sibling leaves 4 and 5 carry labels 0 and 2 — vertices that the first
bipartition deliberately placed in *opposite* halves. This is exactly what
the attached test reports against e6c094fc:

```
ERROR: testArchCheck: sibling leaves 4 and 5 hold non-adjacent vertices 0 and 2 (base 0)
```

Note that this happens at **base 0**, where the pre-e6c094fc code was
correct: the patch trades an out-of-bounds bug at base ≥ 1 for a silent
mispairing at every base.

**Why nothing catches it.** The mispairing is permutation-consistent: each
leaf still carries a (label, weight) pair belonging to one real vertex, each
vertex appears exactly once, and the saved leaf-pair distances are the
distances between the labels they connect. `archDecoArchBuild2()`'s debug
checks all pass, and `test_scotch_arch_deco` (build/save/load round-trips)
cannot see it. The only broken relation is *tree structure ↔ labels*: the
decomposition tree no longer groups topologically close vertices, which is
the entire point of building a `deco` architecture from a graph. Every
mapping later computed onto such an architecture optimizes against a
scrambled hierarchy — valid output, silently degraded placement quality.

## 4. Proposed fix

```c
termverttab[termdomnum].num  = archDomNum (&archdat, mapDomain (&mappdat, tgtvertnum));
```

`tgtvertnum` is a based vertex number and `mappdat.parttax` is a based
array, consistent with the write sites and with every other `mapDomain()`
call site. (Patch 0001.)

## 5. The regression test

Patch 0002 adds `src/check/test_scotch_arch_build.c`. The difficulty is that
a mispaired architecture is internally self-consistent, so the test must know
the true bipartition. It forces it: weighted path graphs whose edge loads
(a) all lie within a factor of two — after `graphIelo()` inversion, cutting
any single path edge is cheaper than cutting two, so parts stay contiguous —
and (b) give the median edge of every interval the strictly largest load,
hence the strictly cheapest cut. Under a correct build, sibling singleton
leaves (domains `2k`/`2k+1`) are then the two ends of the last edge cut, so
their labels are adjacent path vertices.

Checks, for paths of size {3, 4, 6, 8, 12} × bases {0, 1, 100000}:

1. the root domain holds all `n` terminals;
2. the `n` singleton leaves carry each vertex label exactly once;
3. sibling singleton leaves carry adjacent labels;
4. the whole architecture is invariant under the base value (labels shifted
   by the base; sizes, weights and distances identical).

The test calls `SCOTCH_randomReset()` before each build so that check 4 is
meaningful (the random state is process-global, and the strategy may
otherwise tie-break differently on the second build).

Verified matrix (gcc/mpicc build of v7.0.12, `IDXSIZE64`):

| libscotch state              | result | first diagnostic |
|------------------------------|--------|------------------|
| v7.0.12 (pre-e6c094fc)       | FAIL   | `root domain does not hold all terminals (2 != 3, base 1)` |
| v7.0.12 + e6c094fc           | FAIL   | `sibling leaves 4 and 5 hold non-adjacent vertices 0 and 2 (base 0)` |
| v7.0.12 + proposed fix       | pass   | — |

## 6. Side notes

* The distance BFS at `arch_build.c:332-352` accumulates `tgtedlotax` values
  through a plain FIFO queue; on edge-loaded target graphs the first-visit
  distance is not the shortest-path distance (a Dijkstra-style queue would
  be needed). This is independent of the pairing bug and only affects the
  accuracy of the distance matrix; mentioned for completeness.
* `archDecoArchBuild()` (`arch_build.c:359`) ignores the return value of
  `archDecoArchBuild2()`, so an allocation failure there (which garbage
  `termdommax` values could trigger under the pre-e6c094fc code) is
  reported as success to the caller.
