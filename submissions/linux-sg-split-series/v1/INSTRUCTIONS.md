<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# How to send the sg_split fix to the kernel maintainers

This directory is a ready-to-send 2-patch series plus a cover letter,
targeting mainline `lib/sg_split.c`. Everything here is in `git
format-patch` form so it drops straight into `git send-email`.

## 0. TL;DR

```sh
# from a clean mainline checkout, on a throwaway branch:
git am /path/to/mail_for_maintainers/0001-*.patch \
       /path/to/mail_for_maintainers/0002-*.patch
# build + run the tests (see §3), confirm 16/16, then:
git send-email --cover-letter --to='...' --cc='...' \
       /path/to/mail_for_maintainers/00*.patch
```
**Verification status (done 2026-05-31):** the series was applied with
`git am` to mainline net-7.1-rc2 and the KUnit suite was run in a real
UML kernel build:
- with both patches: `Ran 16 tests: passed: 16`
- with patch 1 reverted (test kept): `pass:9 fail:7` — the 7 failures are
  exactly the mid-entry partial-coverage cases (t5,7,9,11,13,14,15),
  KUnit printing the sum-vs-requested mismatch for each.
`checkpatch.pl --strict`: patch 1 is 0/0/0 ("ready for submission");
patch 2 is 0 errors / 0 checks / 1 warning (the benign new-file
MAINTAINERS note). You can re-run §3/§4 to reproduce, but they have
passed here.

## 1. What's in here

| File | Role |
|------|------|
| `0000-cover-letter.patch` | The `[PATCH 0/2]` cover letter = the intro mail body. |
| `0001-...nb_splits-ove.patch` | **Patch 1/2** — the one-line fix (`nb_splits <= 0`). |
| `0002-...KUnit-tests...patch` | **Patch 2/2** — `lib/test_sg_split_kunit.c` + Kconfig + Makefile. |
| `INSTRUCTIONS.md` | This file (not sent). |
| `files.md` | Background analysis + manifest (not sent). |

## 2. Identity — edit before sending

The patches are authored as `Charles Pellegrini <c4ffein.work@gmail.com>`
— the address you monitor and have `git send-email`/SMTP configured for,
which is the one that matters since every review reply lands on the
`From:`. Both the name and this address become permanent, public,
archive-indexed kernel history. Make sure it matches your
`git config user.name` / `user.email` and your `git send-email` envelope;
the `Signed-off-by` must equal the `From` exactly (DCO requirement). If
you ever change it, edit the `From:` / `Signed-off-by:` lines in all
three files consistently (or re-`git am` and re-export).

## 3. Apply and verify (REQUIRED before sending)

```sh
# fresh mainline tree
git clone --depth=1 https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git
cd linux
git checkout -b sg-split-fix

git am /path/to/mail_for_maintainers/0001-*.patch /path/to/mail_for_maintainers/0002-*.patch

# host deps for the UML/KUnit build (Debian/Ubuntu):
sudo apt install bc libelf-dev            # plus the usual gcc/make/flex/bison/python3

# run the suite — expect: pass:16 fail:0
tools/testing/kunit/kunit.py run sg_split

# sanity: drop patch 1, keep the tests, expect 7 failures (the mid-entry cases)
git revert --no-edit HEAD~1               # revert the fix, keep the KUnit patch on top
tools/testing/kunit/kunit.py run sg_split # expect pass:9 fail:7
git reset --hard sg-split-fix             # restore
```
If `kunit.py` can't find the suite, confirm `CONFIG_SG_SPLIT_KUNIT_TEST`
got selected — patch 2 adds it with `default KUNIT_ALL_TESTS`, and
`select SG_SPLIT` so `lib/sg_split.c` actually builds (it only compiles
when something selects `CONFIG_SG_SPLIT`).

## 4. checkpatch

```sh
./scripts/checkpatch.pl /path/to/mail_for_maintainers/0001-*.patch \
                        /path/to/mail_for_maintainers/0002-*.patch
```
Patch 1 is checkpatch-clean (0 errors, 0 warnings). Patch 2 emits **one
expected warning** — `added, moved or deleted file(s), does MAINTAINERS
need updating?` — which is benign here: `lib/sg_split.c` has no
MAINTAINERS entry (it's orphaned LIBRARY CODE), so adding a test beside
it needs no MAINTAINERS change. Don't "fix" it by inventing an entry. (A
`ModuleNotFoundError: ply` line from checkpatch's optional parser is also
harmless.)

## 4b. AI-assistance disclosure (already in the patches)

The kernel adopted an official policy in Dec 2025:
`Documentation/process/coding-assistants.rst`. Two rules apply here:

- **Human owns it.** AI must not add `Signed-off-by`; only a human
  certifies the DCO and takes full responsibility. The `Signed-off-by`
  here is yours — that's the accountability.
- **Disclose via an `Assisted-by:` trailer**, format
  `Assisted-by: AGENT_NAME:MODEL_VERSION [TOOL...]`. Both patches carry:

  ```
  Assisted-by: Claude:claude-opus-4-8 hegel-c
  ```

  (`hegel-c` is the property-based-testing harness that found the bug —
  it occupies the optional analysis-tool slot, same as the doc's
  `coccinelle sparse` example.) checkpatch accepts the trailer.

This is the sanctioned, permanent, in-history form of "AI helped, I'm
responsible" — no extra prose note in the cover letter is needed. If you
disagree with the model string or want to drop `hegel-c`, edit the
`Assisted-by:` line in both patch files (keep it immediately above your
`Signed-off-by:`).

## 5. Recipients

Derived from `scripts/get_maintainer.pl` on the patches, plus judgment
Ccs. `lib/sg_split.c` has **no MAINTAINERS entry** — it is routed
through Andrew Morton's `-mm` tree (as both the 2015 original and the
2025 offset fix were). Sending only to `linux-kernel@vger` with no
direct maintainer Cc is almost certainly why the 2021 attempt got zero
review — so address akpm directly this time.

**To:**
- `Andrew Morton <akpm@linux-foundation.org>` — maintainer: LIBRARY CODE; routes `lib/`.

**Cc:**
- `Alexander Egorenkov <egorenar-dev@posteo.net>` — **Reported-by**; first diagnosed this in 2021. Courtesy + lets him ack.
- `Robert Jarzmik <robert.jarzmik@free.fr>` — original author (`get_maintainer`: blamed_fixes 100%).
- `Jens Axboe <axboe@kernel.dk>` — `get_maintainer`: blamed_fixes 100% (routed earlier sg_split changes).
- `T Pratham <t-pratham@ti.com>` — most recent substantive contributor (2025 offset fix) and author of the DTHEv2 caller discussed in the changelog.
- `linux-kernel@vger.kernel.org` — open list.

**On `stable`:** intentionally **not** Cc'd. This is a real correctness
bug but no in-tree caller triggers it, so it does not meet the
stable-rules "must fix a real bug that bothers people" bar. If a
maintainer disagrees, they can add it — don't add it yourself.

## 6. Send

```sh
git send-email \
  --cover-letter \
  --to='Andrew Morton <akpm@linux-foundation.org>' \
  --cc='Alexander Egorenkov <egorenar-dev@posteo.net>' \
  --cc='Robert Jarzmik <robert.jarzmik@free.fr>' \
  --cc='Jens Axboe <axboe@kernel.dk>' \
  --cc='T Pratham <t-pratham@ti.com>' \
  --cc='linux-kernel@vger.kernel.org' \
  /path/to/mail_for_maintainers/0000-cover-letter.patch \
  /path/to/mail_for_maintainers/0001-*.patch \
  /path/to/mail_for_maintainers/0002-*.patch
```
`git send-email` needs an SMTP config (`sendemail.smtpServer` etc.) in
your `~/.gitconfig`. If you've never set it up, do a `--dry-run` first.
Send yourself a copy (`--cc` your own address) and check the raw source
threads correctly (patch 1 and 2 `In-Reply-To` the cover letter).

## 7. After sending

- Watch for a reply from akpm or a `Reviewed-by`/`Acked-by`. lib/ fixes
  typically land via the `mm-nonmm` branch, then to mainline.
- If it goes quiet for ~2 weeks, a polite ping on the thread is fair —
  this file is orphaned and that's the failure mode that killed the 2021
  attempt. A direct re-ping to akpm is appropriate.
- If you respin (v2), bump the subject prefix (`--subject-prefix='PATCH v2'`)
  and add a `## changelog` under the `---` scissors in each patch.

## 8. Provenance note (for your own records)

The fix and the 16-case suite were developed with the hegel-c
property-based-testing harness against an extracted copy of
`lib/sg_split.c`; `tests/irl/sg_split/` in this repo holds that harness.
The kernel-facing artifacts here carry GPL-2.0 (matching `lib/sg_split.c`
and kernel test convention), **not** the hegel-c project's MIT — don't
"fix" the license headers back to MIT.
