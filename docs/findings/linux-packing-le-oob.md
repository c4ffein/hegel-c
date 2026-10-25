<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Finding: heap OOB in lib/packing.c pack()/unpack() (LITTLE_ENDIAN, non-mult-of-4 buffer)

Found 2026-07-12 by the **round-trip PBT pilot** — the first fresh, oracle-rich
target (`unpack(pack(x)) == x`) run after the blind-rediscovery session. ASAN
caught it; hegel shrank it to a 5-draw minimal case. This is *not* a
rediscovery — packing.c has no injected bug; this is a live defect in the
current vendored source.

- **File:** `lib/packing.c`, `calculate_box_addr()` (consumed by `__pack`/`__unpack`)
- **Class:** heap-buffer-overflow (read + write) via unsigned underflow
- **Confidence bug is real:** high (ASAN-confirmed, deterministic). **Reachability: low today** (no in-tree caller hits the combo — see below), but it violates the documented contract.

## Mechanism

`calculate_box_addr(box, len, quirks)` maps a logical octet index to a physical
buffer offset, treating the buffer as groups of 4 bytes. For the non-`LSW32_IS_FIRST`
path it computes:

```c
offset_of_group = len - ((this_group + 1) * 4);   /* size_t — underflows */
group_size      = min(4, len - offset_of_group);
```

When `len` is **not** a multiple of 4, the most-significant group is incomplete
and `(this_group + 1) * 4 > len`, so `offset_of_group` underflows to a huge
`size_t`. The **big-endian** branch accidentally survives this: its
`offset_in_group = group_size - (box - this_group*4) - 1` term, combined with the
wrapped `group_size`, brings `offset_of_group + offset_in_group` back into range
via modular arithmetic. The **little-endian** branch uses
`offset_in_group = box - this_group*4`, which does **not** compensate — so the
returned `box_addr` stays at ~`2^64-3`, and `((u8 *)pbuf)[box_addr]` in `__pack`
(line ~155) / `__unpack` reads and writes wildly out of bounds.

## Exact failing surface (measured)

Fires iff: **`QUIRK_LITTLE_ENDIAN` set AND `QUIRK_LSW32_IS_FIRST` NOT set AND
`len % 4 ∈ {1, 2}`**, when the accessed field touches the incomplete
most-significant group. (`len % 4 == 3` does not OOB but appears to mis-address —
see caveat. `LSW32_IS_FIRST` set → `offset_of_group = this_group*4`, no underflow,
safe. BE path → safe.)

```
len\quirks  q0  q1  q2  q3  q4  q5  q6  q7      q2=LE  q3=LE|MSBR
 1          ok  ok  XX  XX  ok  ok  ok  ok      XX = heap OOB
 2          ok  ok  XX  XX  ok  ok  ok  ok
 5          ok  ok  XX  XX  ok  ok  ok  ok
 6          ok  ok  XX  XX  ok  ok  ok  ok
 (3,4,7,8: all ok)
```

## Minimal repro

```c
uint8_t *buf = calloc(1, 1);
pack(buf, 0, /*startbit*/0, /*endbit*/0, /*pbuflen*/1, QUIRK_LITTLE_ENDIAN);
/* -> AddressSanitizer: heap-buffer-overflow in __pack (calculate_box_addr
      returns ~2^64-3). Valid per pack()'s own arg checks. */
```

## Why the contract says this is a bug (not invalid input)

`Documentation/core-api/packing.rst` §"Note on buffer lengths not multiple of 4"
explicitly supports non-mult-of-4 buffers: the API "effectively logically
shorten[s] the most significant group of 4 octets to the number of octets
actually available." `pack()`'s own validation only rejects
`startbit < endbit` or `startbit >= 8*pbuflen` — a 1-byte buffer with a
single-bit field passes. So this is a documented-valid input that OOBs.

## Candidate fix (verified here)

Compute `group_size` from the group's own base and derive `offset_of_group`
without underflow:

```c
group_size = min(4, len - this_group * 4);            /* octets actually present */
if (quirks & QUIRK_LSW32_IS_FIRST)
        offset_of_group = this_group * 4;
else
        offset_of_group = len - this_group * 4 - group_size;   /* no underflow */
```

Verified in the pilot: the OOB disappears across the entire 8×8 quirk×length grid
(all `ok`), and the round-trip PBT (`unpack(pack(x))==x`) passes 2000 cases with
no regression on the previously-passing combos.

**Caveat — do not send as-is without maintainer review.** The round-trip PBT
cannot detect a *symmetric* mis-address (both `pack` and `unpack` call
`calculate_box_addr`, so a wrong-but-consistent physical byte still round-trips).
For `len % 4 == 3` + LE the original code did **not** OOB but the physical offset
it produced differs from what this fix produces; I derived the "correct" layout
from the doc, not from the round-trip. The clear, defensible part is the OOB; the
exact intended physical layout for partial LE groups should be confirmed with the
packing maintainer (Vladimir Oltean).

## Reachability

- Kernel selftest (`lib/packing_test.c`) exercises `QUIRK_LITTLE_ENDIAN` 17×, but
  only with multiple-of-4 buffers (8-byte `test_fields`) — never hits it.
- The main in-tree LE caller, `drivers/net/ethernet/intel/ice/ice_common.c`, uses
  `QUIRK_LITTLE_ENDIAN | QUIRK_LSW32_IS_FIRST` → the safe path.
- So there's likely **no currently-reachable trigger** in-tree. Still, it's a
  documented-contract OOB reachable from the public `pack`/`unpack` API — a
  legitimate hardening/correctness fix, stronger than "latent with no doc claim."

## Pilot artifacts

`tests/irl/linux/packing_roundtrip/`: `test_roundtrip.c` (the PBT), local
`compat.h`, `Makefile`; it compiles the full upstream lib/packing.c, copied in
at build time from the pinned kernel. Pin: `tests/irl/linux/pins/packing_roundtrip.conf`
(buggy = v7.1, fixed = v7.1 + `pins/packing_roundtrip.patch`, the candidate fix);
run with `make -C tests/irl/linux test-pin PIN=packing_roundtrip STATE=buggy|fixed`.
Self-contained — no radix-tree harness, no urcu.
