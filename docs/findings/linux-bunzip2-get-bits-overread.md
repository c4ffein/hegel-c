<!-- SPDX-License-Identifier: MIT AND GPL-2.0-only
     Copyright (c) 2026 c4ffein
     SPDX-FileCopyrightText: The Linux kernel authors (lib/ excerpts)
     Part of hegel-c — see hegel/LICENSE for terms. The quoted kernel code is
     GPL-2.0-only, as in its upstream files. -->

# `get_bits()` heap over-read after EOF on a truncated bzip2 stream

Surfaced in passing by a blind hunter subagent during the 2026-07-12
rediscovery re-run, while it hunted another (injected) bug; checked by an
informed judge subagent and re-verified by hand against the in-tree source.
**Re-checked 2026-10-22: still present**, byte-identical in v7.3 and
linux-next. Not reported upstream. Status and history: `docs/findings.toml`.

linux-next did fix a different bug nearby, a run-length overflow / off-by-one
in the same decompressor (Aamir Ahmed, Matt Turner); `get_bits()` is unchanged.

- **File:** `lib/decompress_bunzip2.c`, `get_bits()`
- **Class:** heap-buffer-overflow READ (out-of-bounds input read)
- **Confidence bug is real:** high (ASAN-confirmed). **Reachability: trusted-input path limits impact.**

## Mechanism

The refill / EOF handling and the `io_error` early-return are both nested inside
the `inbufPos == inbufCount` equality test:

```c
if (bd->inbufPos == bd->inbufCount) {
    if (bd->io_error)
        return 0;
    bd->inbufCount = bd->fill(bd->inbuf, BZIP2_IOBUF_SIZE);
    if (bd->inbufCount <= 0) {          /* fill/nofill returns -1 on EOF */
        bd->io_error = RETVAL_UNEXPECTED_INPUT_EOF;
        return 0;
    }
    bd->inbufPos = 0;
}
...
bd->inbufBits = (bd->inbufBits << 8) | bd->inbuf[bd->inbufPos++];  /* OOB read */
```

On the first EOF, `fill()` (or `nofill()`, when all input is provided up front)
returns `-1`, so `inbufCount` is set to `-1`, `io_error` is set, and the call
returns 0 cleanly. But `inbufCount` is now `-1` while `inbufPos` stays positive.
On the **next** `get_bits()` call, `inbufPos == inbufCount` is false (positive vs
`-1`), so the whole block — including the `if (bd->io_error) return 0;` guard — is
skipped, and control falls through to `bd->inbuf[bd->inbufPos++]`, reading past the
end of `inbuf`. The `io_error` guard is structurally unreachable once `inbufCount`
is corrupted.

## Repro

Feed a **truncated** (but otherwise valid-prefix) bzip2 stream into a heap-exact
input buffer; after the decoder exhausts the buffer it keeps requesting bits,
tripping ASAN: `heap-buffer-overflow READ of size 1` at `get_bits` (the
`bd->inbuf[bd->inbufPos++]` line), via `get_next_block → read_bunzip → bunzip2`.

## Suggested fix

Make the EOF/`io_error` check independent of the equality test, e.g. check
`if (bd->io_error) return 0;` before the `inbufPos == inbufCount` block, or guard
the fetch with `inbufPos >= inbufCount` rather than `==` so a corrupted
(negative) `inbufCount` can't be stepped past.

## Assessment

Genuine OOB read, ASAN-confirmed, and the code is verbatim busybox-derived so it
plausibly exists unpatched upstream. **But** the kernel's bzip2 decompressor runs
on *trusted, complete* input (compressed kernel image / initrd), so a truncated
stream is not normally attacker-supplied on this path — real-world severity is
low. Worth a defensive patch and possibly a note to the busybox lineage; not
urgent.
