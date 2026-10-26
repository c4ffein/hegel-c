<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# v3 send checklist (2026-07-06)

Supersedes INSTRUCTIONS.md (v1-era) and the V2_HANDOFF files for send
mechanics. State: series BUILT on branch `scratch-v3` (base v7.2-rc1),
checkpatch-clean; validation battery + [TBD] number fill in progress.

## What's where

- Branch: `inspiration/targets/linux` `scratch-v3` — 5 commits on
  `v7.2-rc1`. Backups: `scratch-v2`, `scratch-v2-bak`, `scratch-v2-final`.
- Drafts: `mail_v3_0.md` (reply to akpm, threads under HIS 2026-06-28
  mail), `mail_v3_cover.md` (v3 0/5; In-Reply-To the v2 cover msgid).
- Userspace evidence: `tests/irl/linux/sg_split/` — `make contract-v3`
  (both semantics green), `make contract` (v2 red control, finds bug 4).

## Needs YOU before send (cannot be done by the model)

1. **akpm's 2026-06-28 Message-ID** from your Gmail raw view → replace
   the `GET-FROM-RAW-MAIL` placeholder in `mail_v3_0.md`.
2. **Sign off the Assisted-by choice**: new commits (4/5, 5/5) carry
   `Claude:claude-fable-5`; patch 3/5 carries BOTH opus (wrote it) and
   fable (edited it this round). Confirm or simplify.
3. Review pass, in this order (prose-claims-first):
   a. commit messages + cover: re-derive every claim (bug traces,
      caller survey, the three cited hashes, the nents>=1 argument)
   b. patch 4 diff (12 lines) + kdoc wording
   c. patch 5 test expectations (three zero positions)
   d. interdiff v2→v3 read-through
   e. the two mails as-akpm-in-2-minutes
4. You press send. Not the model.

## Send (order matters)

```sh
# 1. reply FIRST (mail_v3_0.md), in-thread under akpm's June-28 mail
# 2. then the series, threaded under the V2 cover:
cd inspiration/targets/linux
git format-patch -v3 --cover-letter -5 \
    --in-reply-to='178113124323.90620.6403136846887207198@gmail.com'
# paste mail_v3_cover.md prose into the 0/5 (fill [TBD] numbers first!)
# send via sm send-patches (threading fix verified 2026-06-10) or git send-email
```

To: akpm. Cc: egorenar-dev, robert.jarzmik, t-pratham, David Gow
(KUnit), lkml. Jens Axboe stays dropped.

**Ritual (has a save on record):** dry-run to self → download raw .eml
→ `git am` onto v7.2-rc1 → tree must be byte-identical to scratch-v3.
Never copy-paste patches.

## After sending

Watch https://lore.kernel.org/all/?q=sg_split&o=-1 (browser; Anubis
blocks curl). If quiet ~2 weeks, ping in-thread — do not let it sit
like v2 did.
