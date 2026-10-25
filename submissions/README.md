<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Submissions

Everything we sent upstream, or are about to send, one folder per submission and version.
The index is `docs/findings.toml`: each `[[submission]]` there has `dir = "submissions/<id>"`, and each entry
in its `versions` list has a folder here named `v<N>`, or `v<N>-<label>` when two candidates share a number.

    submissions/<id>/
      v1/          the patches exactly as sent (git format-patch output), plus notes
      v2/
      v3-ours/     two unsent candidates for v3: the label tells them apart
      v3-agent/

A version folder holds:
- `*.patch`: the series, with the cover letter as `0/n`.
- Notes: `*.md` / `*.txt`, e.g. send steps, a report, the email text.
- Any subfolders (evidence, test sends).

## Checks

`make lint-findings` (also run in CI) fails when:
- a folder here is not in the ledger, or a version in the ledger has no folder;
- the number of patches in a folder differs from the version's `patches`;
- the subjects aren't `[PATCH vN i/n]` with N = the version and i = 1..n;
- a Linux series of more than one patch has no cover letter;
- a sent Linux patch has no `Signed-off-by`;
- a draft or changes-requested submission has no `next` (what we do next).

## Viewing

- `make findings-serve`: the page lists open submissions first, under "to do", each with its next step.
- Each submission shows its patches one version per tab, with colored diffs.
- A "changes" tab per pair of versions shows what changed from one version to the next, patch by patch.
- Unsent Linux patches without your `Signed-off-by` are flagged.

## Rules

- Nothing here is sent automatically. A person reviews, signs and sends.
- AI-assisted patches carry `Assisted-by:`, never an AI `Signed-off-by:`.
- Patch files against a project keep that project's license: Linux patches are GPL-2.0-only and Scotch patches
  CeCILL-C (`REUSE.toml`).
- This repo is public. A folder here discloses the bugs it fixes, so an unsent draft for a bug nobody has reported
  belongs in a private ledger until it's sent (see TODO.md, "private ledger overlay").
