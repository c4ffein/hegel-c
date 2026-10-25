<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# `ida_find_first_range()` fails to advance past the first bitmap block

Surfaced in passing by a blind hunter subagent during the 2026-07-12
rediscovery re-run, while it hunted another (injected) bug; checked by an
informed judge subagent and re-verified by hand against the in-tree source.
**Re-checked 2026-10-22: still present**, byte-identical in v7.3 and
linux-next. Not reported upstream. Status and history: `docs/findings.toml`.

- **File:** `lib/idr.c`, `ida_find_first_range()`
- **Class:** logic bug — single-shot search, missing loop over IDA bitmap blocks
- **Confidence bug is real:** high. **Reachability: low (no known triggering caller).**

## Mechanism

`ida_find_first_range(ida, min, max)` is meant to return the lowest allocated ID
in `[min, max]`. It computes `index = min / IDA_BITMAP_BITS` and
`offset = min % IDA_BITMAP_BITS`, then does a **single**
`xa_find(&ida->xa, &index, max/IDA_BITMAP_BITS, XA_PRESENT)` to locate the first
present bitmap block, and a **single** `find_next_bit(addr, size, offset)` within
that block. If that block is present but has no set bit at or after `offset`, the
code returns `-ENOENT` — it never advances to the *next* present block.

## Repro

```c
DEFINE_IDA(ida);
ida_alloc_range(&ida, 0, 0, GFP_KERNEL);        /* id 0    -> block 0, bit 0   */
ida_alloc_range(&ida, 1024, 1024, GFP_KERNEL);  /* id 1024 -> block 1, bit 0   */
int r = ida_find_first_range(&ida, 1, 2999);    /* returns -ENOENT             */
/* correct answer is 1024 */
```

Trace: `index=0, offset=1`. `xa_find` stops at the **non-empty** block 0 (holds id
0 at bit 0). `find_next_bit(addr, size, 1)` finds nothing ≥ bit 1, returns `size`.
The `if (bit == size || ...)` guard fires → `-ENOENT`. Block 1 (id 1024) is never
inspected. Only manifests when `min`'s block is present, has no bit ≥ `offset`,
and the answer lives in a *later* block within `[min, max]`.

## Suggested fix

Loop: when the current block yields no bit, advance `index` to the next present
block (`xa_find_after`), reset `offset = 0`, and retry until the block index
exceeds `max / IDA_BITMAP_BITS`.

## Assessment

Real logic bug, and it reproduces in the current mainline copy. **But** the only
in-tree external caller — `net/bluetooth/l2cap_core.c:4815`,
`ida_find_first_range(&conn->tx_ida, id, id)` — passes `min == max`, which
confines the search to a single block and therefore *cannot* trigger it. The
kernel's own `lib/test_ida.c` never exercises a cross-block query with a nonzero
`offset`. So this is a latent correctness gap with **no currently-reachable
trigger**; a report would be "defensive correctness," likely low priority for
maintainers. Verify there is no out-of-tree caller (e.g. RDMA, newer drivers)
passing a wide `[min, max]` before deciding it's worth sending.
