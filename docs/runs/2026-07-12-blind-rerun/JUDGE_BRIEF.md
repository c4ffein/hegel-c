<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Judge brief (informed verifier of a blind PBT bug-hunt)

A blind hunter agent tried to rediscover a known upstream Linux kernel bug in a
sandbox using hegel-c property tests, then fix it. You are the INFORMED judge:
unlike the hunter, you MAY consult the reference material. Your job is to grade
the hunt honestly and catch tautological tests, incidental crashes, wrong fixes,
and poor shrinks.

## You MAY read (you are not blind)
- The hunter's sandbox: `test_hunt.c`, `HUNT_REPORT.md`, the (now-fixed) `*_buggy.c`.
- The reference: `/home/dev/workspace/hegel-c/BUG_REDISCOVERY_CANDIDATES.md`
  (find the entry for THIS target — it names the real bug, the upstream fix
  commit, and the expected minimal repro).
- Framework headers/docs as needed.

## What to check
1. **Real bug, real property.** Does `test_hunt.c` assert a genuine correctness
   property / invariant / sanitizer-clean requirement — NOT a tautology, and NOT
   merely "it didn't crash on this incidental input"? Would the property be
   reasonable to a domain expert?
2. **The test actually discriminates (DO THIS EMPIRICALLY).** In the sandbox:
   - Build & run `test_hunt` now (with the hunter's fix in place). Expect PASS.
     Run:  `make test_hunt && HEGEL_LIBHEGEL_PATH=/home/dev/workspace/hegel-c/build/libhegel.so ./test_hunt`
   - Make a backup of the buggy source, then REVERT the hunter's fix (re-introduce
     the bug per HUNT_REPORT's FIX line), rebuild, rerun. Expect the test to FAIL
     (non-zero exit or sanitizer diagnostic). Then restore the fixed version.
   - If the test passes BOTH with and without the fix, it does NOT catch the bug
     → that's a FALSE-POSITIVE hunt regardless of what the report claims.
3. **Fix correctness.** Compare the hunter's fix to the upstream fix described in
   the reference. Same mechanism/intent? (Byte-identical not required, but it must
   fix the same defect the right way, not paper over a symptom.)
4. **Shrink quality.** Is the reported MINIMAL_REPRO genuinely minimal (close to
   the reference's expected minimal repro), or bloated? Rate decent/poor.
5. **Blindness plausibility.** Anything in the transcript/report suggesting the
   hunter peeked at forbidden sources? Note if suspicious.

## Verdict — end your final message with this block:
```
JUDGE_TARGET: <target name>
HUNTER_CLAIM: <FOUND|NOT_FOUND from the hunter>
VERDICT: HIT | PARTIAL | MISS | FALSE_POSITIVE
TEST_VALID: <yes/no — is it a real, non-tautological property>
DISCRIMINATES: <yes/no — failed without the fix, passed with it, per your empirical check>
FIX_MATCHES_UPSTREAM: <yes/close/no — vs the reference fix>
SHRINK_QUALITY: <decent/poor — vs expected minimal repro>
NOTES: <one-paragraph justification; flag any blindness or nudge concerns>
```
Definitions: HIT = found the targeted bug, valid discriminating test, correct fix.
PARTIAL = found a real but different/sibling bug, or right bug but weak test/fix.
MISS = did not find the targeted bug. FALSE_POSITIVE = claims a bug its test
doesn't actually catch, or a non-bug.
