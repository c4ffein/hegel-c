<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Evidence for the v3-agent series (archived, not wired into make)

These are the property tests, mutation drivers and logs behind `../REPORT.md` §3, saved as they ran on 2026-10-23/24.
They were written in a throwaway respin sandbox, not in this repo's build. Their comments say `work/v3x/...` and
`make bin T=...` because that is the sandbox layout. No kernel source is stored here. The code under test is
`lib/sg_split.c` with `../v3-*.patch` applied.

## Files

| File | What it is |
|---|---|
| `sgcase.h` | v2-era generator, oracle model and checker (used by the differentials) |
| `sgcase_v3.h`, `test_props_v3.c`, `test_v3_new.c`, `test_diff_v3.c` | the blind agent's own suite |
| `sgcase_x.h`, `test_props_x.c`, `test_x_new.c`, `test_diff_x.c` | extended suite: up to 48 entries, 3 chain points, 16 splits, exact-size heap arrays, any-grouping DMA model over an IOVA buffer |
| `sgfake.h`, `test_fake.c` | fake page addresses: lengths up to `UINT_MAX`, totals past 4 GiB (P-HUGE); 64-bit vs i386 comparison (P-ABI32) |
| `exec32.c`, `build32.sh` | the i386 side of P-ABI32: static `-m32` build, needs an i386 libc sysroot (Debian `libc6-i386`, `libc6-dev-i386`, `lib32gcc-14-dev`, unpacked with `dpkg-deb -x`); the sysroot path in the script is the sandbox's |
| `test_kunit_v3.c`, `kunit-shim/` | runs `lib/tests/sg_split_kunit.c` (from 6/6) in userspace |
| `mut_*.c`, `mut_guard.h`, `mutate_x.py` | mutation drivers and the mutation tool (`tools/mutate.py` plus `--need` / `--out`) |
| `runall.sh` | runs the extended suite in plain / NEED / ASan / NEED+ASan |
| `logs/` | the runs that `../REPORT.md` quotes |
| `blind-replay-KEY.md` | the pre-registered answer key for the blind respin, and how the agent scored |

## Rerunning

1. You need a userspace build of `lib/sg_split.c`. That is the harness in `tests/irl/linux/sg_split/` (the
   shim, the stub headers, and `pin.mk` copying the kernel files from a pin worktree).
2. Apply `../v3-000[1-6]*.patch` to the kernel checkout.
3. Build each test as the sandbox Makefile did:

       gcc -O1 -g -w -D_LGPL_SOURCE [-DCONFIG_NEED_SG_DMA_LENGTH] [-fsanitize=address,undefined] \
           -I<hegel-c> -I<harness>/include -I<kernel>/tools/testing/shared -I<kernel>/tools/include \
           -I<kernel>/tools/arch/x86/include -o test test_x.c <kernel>/lib/sg_split.c \
           <harness>/scatterlist_shim.c -L<hegel-c>/build -lhegel_c -lpthread -lm -ldl

Making this a `make` target under `tests/irl/linux/sg_split/` is a TODO.
