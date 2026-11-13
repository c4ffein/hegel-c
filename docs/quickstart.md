<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 c4ffein
     Part of hegel-c — see hegel/LICENSE for terms. -->

# Quickstart: a first property test in five minutes

You need GCC, `make` and `curl` on Linux (amd64 or arm64). Nothing else: no
Rust, no Python.

## 1. Build the library and fetch the engine

```sh
git clone https://github.com/c4ffein/hegel-c && cd hegel-c
make lib                    # build/libhegel_c.a, a few seconds
make libhegel PREBUILT=1    # build/libhegel.so, downloaded and checksum-verified
```

`libhegel_c.a` is what your tests link. `libhegel.so` is Hegel's engine,
loaded at run time; it does the generating and the shrinking.

## 2. Write a function and a property

The example is [`tests/selftest/test_quickstart.c`](../tests/selftest/test_quickstart.c).
The code under test removes adjacent duplicates from an array:

<!-- /include tests/selftest/test_quickstart.c:19-27 -->
```c
/* Remove adjacent duplicates in place; return the new length. */
static int dedup (int *a, int n)
{
  int w = 1;
  for (int r = 1; r < n; r++)
    if (a[r] != a[r - 1])
      a[w++] = a[r];
  return w;
}
```
<!-- /endinclude -->

A property test doesn't list inputs. It says what must hold for *every*
input, and draws the input from the engine:

<!-- /include tests/selftest/test_quickstart.c:31-43 -->
```c
static void test_dedup (hegel_testcase *tc)
{
  int  n = hegel_draw_int (tc, 0, 20);
  int *a = hegel_alloc (tc, (size_t) (n + 1) * sizeof *a);
  for (int i = 0; i < n; i++)
    a[i] = hegel_draw_int (tc, 0, 3);

  int m = dedup (a, n);

  HEGEL_ASSERT_LE (m, n);
  for (int i = 1; i < m; i++)
    HEGEL_ASSERT_NE (a[i], a[i - 1], "at index %d", i);
}
```
<!-- /endinclude -->

- `hegel_draw_int (tc, lo, hi)` draws a value. Later draws can depend on
  earlier ones: the array has the length that was just drawn.
- `hegel_alloc` is memory owned by the test case, freed when the case ends,
  however it ends.
- `HEGEL_ASSERT_LE` / `HEGEL_ASSERT_NE` fail the case and print both sides.
  `HEGEL_ASSERT (cond, fmt, ...)` is the general form.

`main` hands the property to a runner:

<!-- /include tests/selftest/test_quickstart.c:47-51 -->
```c
int main (void)
{
  hegel_run_test (test_dedup);
  return 0;
}
```
<!-- /endinclude -->

## 3. Build and run it

```sh
gcc -O2 -I. -o test_quickstart tests/selftest/test_quickstart.c \
    -Lbuild -lhegel_c -lpthread -lm -ldl
./test_quickstart
```

Run it from the repository root, where the runtime finds
`./build/libhegel.so`; from anywhere else, set `HEGEL_LIBHEGEL_PATH` to the
file.

```text
Draw 1: 0
Property test failed: m <= n failed: 1 vs 0

=== hegel-c: property test FAILED ===
origin: HEGEL_ASSERT_LE at tests/selftest/test_quickstart.c:40
message: m <= n failed: 1 vs 0
reproduce: HEGEL_C_REPRODUCE='1:AAEAAAAACgEAAAAA'
```

The engine tried random arrays until one broke the property, then *shrank*
it: the report is the smallest failing input, here a single draw, `n = 0`.
`dedup` returns 1 for an empty array. The exit status is 1.

## 4. Replay, fix, rerun

The last line replays exactly that case, which is what you want under a
debugger:

```sh
HEGEL_C_REPRODUCE='1:AAEAAAAACgEAAAAA' ./test_quickstart
```

Fix the function (start with `if (n == 0) return 0;`), rebuild, and the run
prints nothing and exits 0: 100 random cases passed.

## Where next

- **Structured inputs**: a generator is a plain C function that fills a
  value from draws. The conventions are in the README's API section and
  worked through on a real graph library in [shrinking.md](shrinking.md).
- **Code that crashes**: by default each case runs in a forked child, so a
  segfault or a hang is a failure that shrinks like any other. To generate
  in-process and fork only the code under test, see `HEGEL_ISOLATED` in
  [`hegel_c.h`](../hegel_c.h).
- **More cases, a fixed seed, several tests in one binary**:
  `hegel_run_test_n`, `hegel_run_options`, the suite API, all in
  [`hegel_c.h`](../hegel_c.h).
- **Stateful testing** (random sequences of operations against a model):
  `tests/selftest/test_stateful_*.c`.
- **How the suite tests itself**: [testing.md](testing.md).
