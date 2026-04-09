/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/* RUST_SOURCE: tests/test_integers.rs::test_i8
** RUST_SOURCE: tests/test_integers.rs::test_i16
** RUST_SOURCE: tests/test_integers.rs::test_i32
** RUST_SOURCE: tests/test_integers.rs::test_i64
** RUST_SOURCE: tests/test_integers.rs::test_u8
** RUST_SOURCE: tests/test_integers.rs::test_u16
** RUST_SOURCE: tests/test_integers.rs::test_u32
** RUST_SOURCE: tests/test_integers.rs::test_u64
**
** Rust pattern (same for every integer type T):
**   assert_all_examples(integers::<T>(), |&n| n >= T::MIN && n <= T::MAX);
**   find_any(integers::<T>(), |&n| n < T::MIN / 2);   // signed only
**   find_any(integers::<T>(), |&n| n > T::MAX / 2);
**   find_any(integers::<T>(), |&n| n == T::MIN);
**   find_any(integers::<T>(), |&n| n == T::MAX);
**
** C port: HEGEL_FIND expresses the positive condition; hegel's
** failure-finding engine actively searches for a matching value.
** find_any() interprets "test failed" as "found one."
*/
#include <stdio.h>
#include <stdint.h>
#include <limits.h>

#include "hegel_c.h"

/* ---- helpers ---- */

/* "Find a case where cond is true."  Internally triggers a hegel
** failure when cond holds, so hegel's search engine targets it. */
#define HEGEL_FIND(cond, ...) \
  do { \
    if (cond) { \
      char _hegel_buf[512]; \
      snprintf (_hegel_buf, sizeof (_hegel_buf), __VA_ARGS__); \
      hegel_fail (_hegel_buf); \
    } \
  } while (0)

/* Run test_fn for up to n cases.  test_fn should use HEGEL_FIND to
** express the condition of interest.  Returns 0 if found, 1 if not. */
static int find_any (void (*fn)(hegel_testcase *), uint64_t n) {
  return hegel_run_test_result_n (fn, n) == 1 ? 0 : 1;
}

/* ---- signed integer tests ---- */

#define SIGNED_TESTS(pfx, draw_fn, T, TMIN, TMAX, fmt)                  \
  static void pfx##_bounds (hegel_testcase * tc) {                       \
    T n = (T) draw_fn (tc, TMIN, TMAX);                                  \
    HEGEL_ASSERT (n >= TMIN && n <= TMAX, #pfx " n=" fmt, n);            \
  }                                                                      \
  static void pfx##_find_lower (hegel_testcase * tc) {                   \
    T n = (T) draw_fn (tc, TMIN, TMAX);                                  \
    HEGEL_FIND (n < TMIN / 2, #pfx " n=" fmt, n);                        \
  }                                                                      \
  static void pfx##_find_upper (hegel_testcase * tc) {                   \
    T n = (T) draw_fn (tc, TMIN, TMAX);                                  \
    HEGEL_FIND (n > TMAX / 2, #pfx " n=" fmt, n);                        \
  }                                                                      \
  static void pfx##_find_min (hegel_testcase * tc) {                     \
    T n = (T) draw_fn (tc, TMIN, TMAX);                                  \
    HEGEL_FIND (n == TMIN, #pfx " n=" fmt, n);                            \
  }                                                                      \
  static void pfx##_find_max (hegel_testcase * tc) {                     \
    T n = (T) draw_fn (tc, TMIN, TMAX);                                  \
    HEGEL_FIND (n == TMAX, #pfx " n=" fmt, n);                            \
  }

#define RUN_SIGNED(pfx, errors)                                          \
  do {                                                                   \
    if (hegel_run_test_result (pfx##_bounds) != 0) {                     \
      fprintf (stderr, "ERROR: " #pfx " bounds\n"); (errors)++;         \
    }                                                                    \
    if (find_any (pfx##_find_lower, 1000) != 0) {                       \
      fprintf (stderr, "ERROR: " #pfx " find_lower\n"); (errors)++;     \
    }                                                                    \
    if (find_any (pfx##_find_upper, 1000) != 0) {                       \
      fprintf (stderr, "ERROR: " #pfx " find_upper\n"); (errors)++;     \
    }                                                                    \
    if (find_any (pfx##_find_min, 1000) != 0) {                         \
      fprintf (stderr, "ERROR: " #pfx " find_min\n"); (errors)++;       \
    }                                                                    \
    if (find_any (pfx##_find_max, 1000) != 0) {                         \
      fprintf (stderr, "ERROR: " #pfx " find_max\n"); (errors)++;       \
    }                                                                    \
  } while (0)

/* ---- unsigned integer tests ---- */

#define UNSIGNED_TESTS(pfx, draw_fn, T, TMAX, fmt)                       \
  static void pfx##_bounds (hegel_testcase * tc) {                       \
    T n = (T) draw_fn (tc, 0, TMAX);                                     \
    HEGEL_ASSERT (n <= TMAX, #pfx " n=" fmt, n);                         \
  }                                                                      \
  static void pfx##_find_upper (hegel_testcase * tc) {                   \
    T n = (T) draw_fn (tc, 0, TMAX);                                     \
    HEGEL_FIND (n > TMAX / 2, #pfx " n=" fmt, n);                        \
  }                                                                      \
  static void pfx##_find_min (hegel_testcase * tc) {                     \
    T n = (T) draw_fn (tc, 0, TMAX);                                     \
    HEGEL_FIND (n == 0, #pfx " n=" fmt, n);                               \
  }                                                                      \
  static void pfx##_find_max (hegel_testcase * tc) {                     \
    T n = (T) draw_fn (tc, 0, TMAX);                                     \
    HEGEL_FIND (n == (T) TMAX, #pfx " n=" fmt, n);                        \
  }

#define RUN_UNSIGNED(pfx, errors)                                        \
  do {                                                                   \
    if (hegel_run_test_result (pfx##_bounds) != 0) {                     \
      fprintf (stderr, "ERROR: " #pfx " bounds\n"); (errors)++;         \
    }                                                                    \
    if (find_any (pfx##_find_upper, 1000) != 0) {                       \
      fprintf (stderr, "ERROR: " #pfx " find_upper\n"); (errors)++;     \
    }                                                                    \
    if (find_any (pfx##_find_min, 1000) != 0) {                         \
      fprintf (stderr, "ERROR: " #pfx " find_min\n"); (errors)++;       \
    }                                                                    \
    if (find_any (pfx##_find_max, 1000) != 0) {                         \
      fprintf (stderr, "ERROR: " #pfx " find_max\n"); (errors)++;       \
    }                                                                    \
  } while (0)

/* ---- code from rust ---- */

SIGNED_TESTS (i8,  hegel_draw_int, int,      INT8_MIN,  INT8_MAX,  "%d")
SIGNED_TESTS (i16, hegel_draw_int, int,      INT16_MIN, INT16_MAX, "%d")
SIGNED_TESTS (i32, hegel_draw_int, int,      INT_MIN,   INT_MAX,   "%d")
SIGNED_TESTS (i64, hegel_draw_i64, int64_t,  INT64_MIN, INT64_MAX, "%ld")

UNSIGNED_TESTS (u8,  hegel_draw_int, int,      UINT8_MAX,  "%d")
UNSIGNED_TESTS (u16, hegel_draw_int, int,      UINT16_MAX, "%d")
UNSIGNED_TESTS (u32, hegel_draw_u64, uint64_t, UINT32_MAX, "%lu")
UNSIGNED_TESTS (u64, hegel_draw_u64, uint64_t, UINT64_MAX, "%lu")

/* ---- runner ---- */

int main (void) {
  int errors = 0;

  RUN_SIGNED (i8,  errors);
  RUN_SIGNED (i16, errors);
  RUN_SIGNED (i32, errors);
  RUN_SIGNED (i64, errors);

  RUN_UNSIGNED (u8,  errors);
  RUN_UNSIGNED (u16, errors);
  RUN_UNSIGNED (u32, errors);
  RUN_UNSIGNED (u64, errors);

  if (errors > 0)
    fprintf (stderr, "%d integer check(s) failed\n", errors);
  return (errors > 0 ? 1 : 0);
}
