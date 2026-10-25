<!-- SPDX-License-Identifier: MIT AND GPL-2.0-only
     Copyright (c) 2026 c4ffein
     SPDX-FileCopyrightText: The Linux kernel authors (lib/ excerpts)
     Part of hegel-c — see hegel/LICENSE for terms. The quoted kernel code is
     GPL-2.0-only, as in its upstream files. -->

# `xa_store_range(_, ULONG_MAX, _)` loops forever on unsigned wrap

Surfaced in passing by a blind hunter subagent during the 2026-07-12
rediscovery re-run, while it hunted another (injected) bug; checked by an
informed judge subagent and re-verified by hand against the in-tree source.
**Re-checked 2026-10-22: still present**, byte-identical in v7.3 and
linux-next. Not reported upstream. Status and history: `docs/findings.toml`.

linux-next did fix a different bug nearby, `xas_find()` jumping backwards inside a
multi-index entry (f0f80e85841d); not this one.

- **File:** `lib/xarray.c`, `xa_store_range()`
- **Class:** non-terminating loop (hang / DoS) on a boundary input
- **Confidence bug is real:** high (empirically confirmed hang). **Reachability: unusual input.**

## Mechanism

The range-tiling inner loop advances `first` by the size of each stored chunk and
continues while `first <= last`:

```c
do {
    xas_set_range(&xas, first, last);
    xas_store(&xas, entry);
    if (xas_error(&xas))
        goto unlock;
    first += xas_size(&xas);
} while (first <= last);
```

The only bound checks are `if (last < first) return -EINVAL;` at the top. When
`last == ULONG_MAX`, the final chunk makes `first += xas_size` overflow
`ULONG_MAX + 1 → 0`, and `0 <= ULONG_MAX` is always true → the loop never
terminates.

## Repro

```c
DEFINE_XARRAY(xa);
xa_store_range(&xa, ULONG_MAX - 1, ULONG_MAX, xa_mk_value(2), GFP_KERNEL); /* hangs */
/* last == ULONG_MAX-1 and below terminate normally */
```

Empirically confirmed: the judge reverted to the plain `while (first <= last)` and
the call timed out (non-terminating); the hunter's guard terminates it.

## Suggested fix

Stop when `first` wraps to zero:

```c
} while (first && first <= last);
```

No-op on every non-wrapping range (a terminating chunk yields `first = last + 1`,
non-zero, `> last`), so behavior-preserving.

## Assessment

Real robustness bug. Severity hinges on whether any caller passes
`last == ULONG_MAX` (or a `last` an attacker controls up to it). Most callers use
bounded ranges, so likely not reachable in practice today — but an infinite loop
(rather than an `-EINVAL`) is a poor failure mode, and the fix is trivial and
safe. Reasonable as a hardening patch; check for a `ULONG_MAX`-passing caller to
justify it.
