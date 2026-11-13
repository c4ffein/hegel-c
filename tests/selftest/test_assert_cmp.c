/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */
/*
** Test: the comparison asserts (HEGEL_ASSERT_EQ / _NE / _LT / _LE / _GT /
** _GE, HEGEL_ASSERT_STR_EQ, HEGEL_ASSERT_BYTES_EQ) fail when they should,
** print both sides, and compare integers by value.
**
** Layer 1: count_bits() counts the set bits of a 32-bit word but its loop
**          stops one bit short, so the top bit is never counted.
** Layer 2: a property asserts count_bits (x) == popcount (x) with
**          HEGEL_ASSERT_EQ; run with stderr captured, it must fail, shrink
**          to x = 0x80000000 and report "... failed: 0 vs 1 (x=0x80000000)".
**          Then one single-case property per macro form, each with the
**          verdict and the report text it must produce.
**
** Expected: EXIT 0.
*/
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hegel_c.h"

/* ---- Layer 1: function under test ---- */

/* Number of set bits in x.  Bug: bit 31 is never looked at. */
static unsigned count_bits (uint32_t x)
{
  unsigned n = 0;
  for (int i = 0; i < 31; i++)
    n += (x >> i) & 1u;
  return n;
}

/* ---- Layer 2: the property, and one check per macro form ---- */

static void test_count_bits (hegel_testcase *tc)
{
  uint32_t x = (uint32_t) hegel_draw_u64 (tc, 0, UINT32_MAX);
  HEGEL_ASSERT_EQ (count_bits (x), (unsigned) __builtin_popcount (x), "x=%#x", x);
}

static int evaluations;
static int next_value (void) { return ++evaluations; }

/* Everything here holds: none of these may fail. */
static void test_holds (hegel_testcase *tc)
{
  size_t        len = 0;
  int           minus_one = -1;
  unsigned      big = UINT_MAX;
  unsigned char byte = 200;
  const char *  none = NULL;
  double        half = 0.5;
  char          a[4] = { 1, 2, 3, 4 }, b[4] = { 1, 2, 3, 4 };

  (void) tc;
  HEGEL_ASSERT_EQ (len, 0);                 /* size_t against a literal: no warning */
  HEGEL_ASSERT_NE (minus_one, big);         /* by value: -1 is not UINT_MAX ... */
  HEGEL_ASSERT_LT (minus_one, big);         /* ... it is below it */
  HEGEL_ASSERT_GT (byte, -1);
  HEGEL_ASSERT_GE (INT64_MIN, INT64_MIN);
  HEGEL_ASSERT_LE (UINT64_MAX - 1, UINT64_MAX);
  HEGEL_ASSERT_LT (half, 1);                /* double against int */
  HEGEL_ASSERT_EQ (none, NULL);
  HEGEL_ASSERT_NE (a, b);                   /* two arrays: two addresses */
  HEGEL_ASSERT_STR_EQ ("abc", "abc");
  HEGEL_ASSERT_STR_EQ (none, NULL);
  HEGEL_ASSERT_BYTES_EQ (a, b, sizeof a);
  HEGEL_ASSERT_BYTES_EQ (a, "x", 0);        /* nothing to compare */

  /* each operand once; the message's arguments only on failure */
  evaluations = 0;
  HEGEL_ASSERT_EQ (next_value (), 1, "never formatted: %d", next_value ());
  HEGEL_ASSERT_EQ (evaluations, 1);
}

static void test_eq_int (hegel_testcase *tc)
{
  int got = 3, want = 4;
  (void) tc;
  HEGEL_ASSERT_EQ (got, want);
}

static void test_eq_mixed_sign (hegel_testcase *tc)
{
  int minus_one = -1;
  unsigned big = UINT_MAX;
  (void) tc;
  HEGEL_ASSERT_EQ (minus_one, big);         /* C's == says equal; by value they are not */
}

static void test_le_context (hegel_testcase *tc)
{
  size_t used = 9, cap = 8;
  (void) tc;
  HEGEL_ASSERT_LE (used, cap, "after %d pushes", 9);
}

static void test_ne_pointer (hegel_testcase *tc)
{
  const char *p = NULL;
  (void) tc;
  HEGEL_ASSERT_NE (p, NULL);
}

static void test_gt_double (hegel_testcase *tc)
{
  double x = 0.1;
  (void) tc;
  HEGEL_ASSERT_GT (x, 0.25);
}

static void test_eq_char (hegel_testcase *tc)
{
  char c = 'a';
  (void) tc;
  HEGEL_ASSERT_EQ (c, 'b');                 /* 'b' is an int in C */
}

static void test_str (hegel_testcase *tc)
{
  const char *got = "ab\ncd";
  (void) tc;
  HEGEL_ASSERT_STR_EQ (got, "ab\nce", "line %d", 2);
}

static void test_str_null (hegel_testcase *tc)
{
  const char *got = NULL;
  (void) tc;
  HEGEL_ASSERT_STR_EQ (got, "x");
}

static void test_bytes (hegel_testcase *tc)
{
  unsigned char out[4] = { 1, 2, 0, 4 }, in[4] = { 1, 2, 255, 4 };
  (void) tc;
  HEGEL_ASSERT_BYTES_EQ (out, in, sizeof in);
}

/* Run fn with stderr captured.  The run must end with verdict `want_rc`
** (0 pass, 1 fail); a failing one must have `text` in its report and, for
** the origin, the name of the macro. */
static int check (const char *what, void (*fn) (hegel_testcase *), int cases, int want_rc,
                  const char *text, const char *origin)
{
  hegel_run_options o = { .test_cases = (uint64_t) cases, .has_seed = 1, .seed = 11 };
  FILE *            tmp = tmpfile ();
  int               saved = dup (2);
  static char       out[65536];
  size_t            n;

  fflush (stderr);
  dup2 (fileno (tmp), 2);
  int rc = hegel_run_test_nofork_with (fn, &o);
  fflush (stderr);
  dup2 (saved, 2);
  close (saved);
  rewind (tmp);
  n = fread (out, 1, sizeof out - 1, tmp);
  out[n] = 0;
  fclose (tmp);

  if (rc != want_rc) {
    fprintf (stderr, "  FAIL: %s: verdict %d, expected %d\n%s", what, rc, want_rc, out);
    return 1;
  }
  if (text && !strstr (out, text)) {
    fprintf (stderr, "  FAIL: %s: report lacks \"%s\"\n%s", what, text, out);
    return 1;
  }
  if (origin && !strstr (out, origin)) {
    fprintf (stderr, "  FAIL: %s: report lacks origin \"%s\"\n%s", what, origin, out);
    return 1;
  }
  printf ("  %-22s ok\n", what);
  return 0;
}

/* ---- Layer 3: runner (see Makefile TESTS_PASS) ---- */

int main (void)
{
  int errors = 0;

  printf ("Testing comparison asserts...\n");
  unsetenv ("HEGEL_C_REPRODUCE");
  errors += check ("count_bits property", test_count_bits, 2000, 1,
                   "count_bits (x) == (unsigned) __builtin_popcount (x) failed: "
                   "0 vs 1 (x=0x80000000)", "HEGEL_ASSERT_EQ at ");
  errors += check ("everything that holds", test_holds, 1, 0, NULL, NULL);
  errors += check ("EQ, ints", test_eq_int, 1, 1, "got == want failed: 3 vs 4", NULL);
  errors += check ("EQ, mixed signs", test_eq_mixed_sign, 1, 1,
                   "minus_one == big failed: -1 vs 4294967295", NULL);
  errors += check ("LE with a message", test_le_context, 1, 1,
                   "used <= cap failed: 9 vs 8 (after 9 pushes)", "HEGEL_ASSERT_LE at ");
  errors += check ("NE, pointers", test_ne_pointer, 1, 1, "p != NULL failed: NULL vs NULL",
                   NULL);
  errors += check ("GT, doubles", test_gt_double, 1, 1, "x > 0.25 failed: 0.1 vs 0.25", NULL);
  errors += check ("EQ, a char", test_eq_char, 1, 1, "c == 'b' failed: 'a' (97) vs 98", NULL);
  errors += check ("STR_EQ", test_str, 1, 1,
                   "got == \"ab\\nce\" (strings) failed: \"ab\\x0acd\" vs \"ab\\x0ace\", "
                   "first difference at byte 4 (line 2)", "HEGEL_ASSERT_STR_EQ at ");
  errors += check ("STR_EQ, NULL", test_str_null, 1, 1,
                   "got == \"x\" (strings) failed: NULL vs \"x\"", NULL);
  errors += check ("BYTES_EQ", test_bytes, 1, 1,
                   "out == in (4 bytes) failed: first difference at byte 2: 0x00 vs 0xff",
                   "HEGEL_ASSERT_BYTES_EQ at ");

  if (errors) return 1;
  printf ("OK\n");
  return 0;
}
