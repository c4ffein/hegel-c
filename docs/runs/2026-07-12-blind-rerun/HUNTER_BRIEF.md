<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Blind PBT bug-hunt brief (hunter agent)

You are running a **blind** property-based-testing bug hunt. A single C source
file in your sandbox contains **exactly one injected bug** (a real upstream
Linux kernel bug whose fix was reverted). Your job is to find it from first
principles with hegel-c property tests, then fix it.

## Step 0 — load the skills
Invoke BOTH skills before doing anything else:
- `hegel-bughunt` (the audit/find-bugs methodology)
- `hegel` (how to write hegel-c property tests)
Follow their guidance.

## BLINDNESS RULES (hard constraints — violating these invalidates the run)
You MAY read: the files inside your own sandbox dir; the framework headers
`/home/dev/workspace/hegel-c/hegel_c.h` and `hegel_gen.h`; `docs/schema-api.md`,
`docs/patterns.md`; and examples under `/home/dev/workspace/hegel-c/tests/selftest/`.

You MUST NOT (these reveal the answer):
- Run any `git log`/`git show`/`git blame` (in this repo OR the kernel checkout).
- Open the upstream copy of your target file or its subsystem anywhere under
  `/home/dev/workspace/hegel-c/inspiration/targets/linux/` (headers get pulled in
  by the Makefile automatically — that's fine; do not *read the fixed source*).
- Read anything under `/home/dev/workspace/hegel-c/tests/irl/` outside your sandbox.
- Read `BUG_REDISCOVERY_CANDIDATES.md`, `NEXT_*.md`, `BONUS_*.md`, or
  `docs/saved_sessions_for_history/`.
- Search the web for the bug or the fix.
Work only from the behavior of the code in your sandbox.

## Method
1. Read your target `*_buggy.c`. Identify its public API surface.
2. Build a **reference model / oracle** in your test (what the function *should*
   do) and/or assert invariants that must always hold. Exercise the API broadly
   with hegel-c draws — scalars, structs (schema API), and stateful op sequences
   (`hegel_state_machine_*`) as appropriate. Sanitizers (ASAN+UBSAN) are already
   on in CFLAGS, so UB / OOB / leaks surface automatically.
3. When a property fails, let hegel shrink it and record the **minimal**
   reproducing input/sequence.

## Build & run (from your sandbox dir)
- Write your harness as `test_hunt.c` in the sandbox.
- Build:  `make test_hunt`
- Run:    `HEGEL_LIBHEGEL_PATH=/home/dev/workspace/hegel-c/build/libhegel.so ./test_hunt`
  (a failing property should make it exit non-zero, or a sanitizer diagnostic prints).
- The Makefile's pattern rule compiles any `test_<name>.c` against the buggy
  object + harness. A `test_smoke.c` is present and already builds — model your
  build on it if needed.

## Then FIX it
Once your test reliably fails on the bug: edit the `*_buggy.c` to fix it (the
smallest correct change), rebuild, and confirm your test now **passes** and your
other properties still hold. Keep the fix minimal and principled.

## Deliverable — write `HUNT_REPORT.md` in your sandbox AND end your final
message with this exact structured block:

```
RESULT: FOUND | NOT_FOUND
BUG_LOCATION: <file:line and function, or "none">
BUG_CLASS: <e.g. off-by-one / signed-shift-UB / OOB-write / invariant-violation / leak / none>
PROPERTY: <the property/oracle that caught it, one sentence>
MINIMAL_REPRO: <the shrunk failing input or op-sequence, concise>
FIX: <the one-line/small diff you applied, or "none">
FIX_VERIFIED: <yes/no — did the test go green after the fix>
CONFIDENCE: <high/medium/low>
BLINDNESS_ATTEST: <"followed rules" — and note any file you were unsure about>
NOTES: <if NOT_FOUND: what you tried and why you believe no PBT-catchable bug exists / what boundary blocks it>
```

If after a genuine, broad effort you cannot find a bug, report NOT_FOUND
honestly with your reasoning — do NOT fabricate one. Some targets may hold bugs
that a single-threaded PBT harness structurally cannot construct (e.g. a
concurrency race needing two threads); if you conclude that, say so precisely.
