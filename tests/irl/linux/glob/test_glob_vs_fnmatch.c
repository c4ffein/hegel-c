/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Differential test: kernel lib/glob.c's glob_match() against libc's
** POSIX fnmatch().  Property:
**
**     glob_match(pat, str) == (fnmatch(pat, str, 0) == 0)
**
** Generators use the new hegel_draw_bytes primitive — single logical
** span per array, NOT a per-byte for-loop of hegel_draw_int.
**
** Both pattern and string are random byte arrays.  The pattern syntax
** is forgiving (an unmatched `[` is matched literally per glob_match's
** docstring) so most random byte buffers ARE valid patterns.  We then
** null-terminate them in a stack buffer for the C-string APIs.
**
** NOTE on byte 0x00 in random bytes: hegel_draw_bytes produces uniform
** [0, 255] bytes, possibly including 0x00.  Both glob_match and
** fnmatch treat the FIRST 0x00 they see as terminator.  To keep the
** test honest (we want the *full* drawn buffer to be the input, not a
** truncation), we replace embedded 0x00s with a benign filler ('x')
** before null-terminating.  This is a deliberate decision — testing
** "what happens with embedded NULs" requires a different oracle since
** the C-string ABI itself can't represent embedded NULs.
**
** EXPECTED DIVERGENCE NOTES (from man fnmatch + glob.c docstring):
**
**   1. Backslash: in glob_match, '\' always escapes the next char and
**      the escaped char is matched literally.  In fnmatch with flags=0,
**      '\' also escapes — same semantics.  No divergence expected.
**
**   2. Unmatched '[': glob_match's docstring says "An opening bracket
**      without a matching close is matched literally."  POSIX fnmatch
**      with flags=0 says the behaviour is undefined for malformed
**      patterns.  Glibc's fnmatch is documented to also match it
**      literally in this case.  Likely no divergence.
**
**   3. Bracket complement: glob_match uses '!' (per glob(7)).  POSIX
**      fnmatch accepts BOTH '!' and '^' as complement markers (POSIX.2
**      requires '!', glibc additionally accepts '^').  We restrict
**      ourselves to '!' so this should align — but raw random bytes
**      will occasionally produce `[^...]` and could disagree.
**
**   4. Trailing backslash: glob_match's '\\' branch reads `*pat++`
**      unconditionally — if pat is empty after the '\\', it advances
**      past the NUL and treats whatever was past it as the literal.
**      Since we null-terminate, this *might* read uninitialized memory.
**      Actually no — we look at pat after `pat++` so we read the NUL,
**      then on the next loop iteration we read past it.  Worth a look
**      under ASAN.
*/

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>
#include "hegel_c.h"

extern bool glob_match (char const *pat, char const *str);

#define MAX_PAT 32
#define MAX_STR 64

/* Sanitize a drawn byte buffer for use as a C-string: replace 0x00 with
** 'x' (an arbitrary non-special non-zero filler).  We can't pass
** embedded NULs to C-string APIs.  Modifies buf in place. */
static void
strip_nuls (uint8_t * buf, int len)
{
  for (int i = 0; i < len; i++)
    if (buf[i] == 0)
      buf[i] = 'x';
}

/* Returns true if the pattern lies in well-defined POSIX territory.
** False if the pattern hits one of the documented divergence points
** between kernel glob_match and libc fnmatch — see header comment.
**
**   1. Trailing unescaped '\': POSIX undefined.  glob_match treats it
**      as matching a literal NUL (so an empty string passes).  glibc
**      fnmatch returns FNM_NOMATCH.  Real divergence — finding #1.
**   2. Unmatched '[' (no closing ']'): POSIX undefined.  glob_match
**      docstring promises literal-match.  glibc fnmatch returns
**      FNM_NOMATCH.  Real divergence — finding #2.
**   3. '[^...]' bracket: glibc fnmatch accepts both '!' and '^' as
**      complement markers; POSIX requires only '!'; glob_match
**      ONLY accepts '!'.  Real divergence on glibc — finding #3. */
static bool
pattern_is_posix_well_defined (const char *p)
{
  for (const char *q = p; *q; q++) {
    if (*q == '\\') {
      if (q[1] == '\0') return false;       /* finding #1 */
      q++;                                  /* skip escaped char */
      continue;
    }
    if (*q == '[') {
      const char *r = q + 1;
      if (*r == '!' || *r == '^') r++;      /* skip complement marker */
      if (*r == ']') r++;                   /* first char can be ] */
      while (*r && *r != ']') {
        if (*r == '\\' && r[1]) r++;        /* escaped */
        r++;
      }
      if (*r == '\0') return false;         /* finding #2: unmatched [ */
      /* Skip the bracket contents; jump q to the closing ']'. */
      q = r;
      continue;
    }
  }
  return true;
}

/* Returns true if pattern uses '^' as a complement marker (glibc
** extension).  glob_match treats '^' as a literal (since it only
** complements with '!') — finding #3. */
static bool
pattern_uses_caret_complement (const char *p)
{
  for (const char *q = p; *q; q++) {
    if (*q == '\\' && q[1]) { q++; continue; }
    if (*q == '[' && q[1] == '^') return true;
  }
  return false;
}

/* The core differential property over POSIX well-defined patterns. */
static void
test_glob_vs_fnmatch (hegel_testcase * tc)
{
  uint8_t pat_buf[MAX_PAT + 1];
  uint8_t str_buf[MAX_STR + 1];

  int plen = hegel_draw_bytes (tc, 0, MAX_PAT, pat_buf, MAX_PAT);
  int slen = hegel_draw_bytes (tc, 0, MAX_STR, str_buf, MAX_STR);

  strip_nuls (pat_buf, plen);
  strip_nuls (str_buf, slen);
  pat_buf[plen] = 0;
  str_buf[slen] = 0;

  /* Filter to POSIX well-defined territory.  Without this filter the
  ** test fires on the documented divergences (findings #1-#3) within
  ** ~5 cases; with it we exercise the matching engine itself. */
  hegel_assume (tc, pattern_is_posix_well_defined ((char *) pat_buf));
  hegel_assume (tc, !pattern_uses_caret_complement ((char *) pat_buf));

  bool g  = glob_match ((char *) pat_buf, (char *) str_buf);
  int  fn = fnmatch ((char *) pat_buf, (char *) str_buf, 0);
  bool f  = (fn == 0);

#ifdef HEGEL_GLOB_INJECT_BUG
  /* Negative control: deliberately flip the comparison.  Used to
  ** verify the property fires AND that the shrunk trace shows the
  ** drawn arrays as logical [bytes...] units (validating the
  ** hegel_draw_bytes span behaviour). */
  HEGEL_ASSERT (g != f,
                "INJECTED: glob=%d fnmatch=%d (rc=%d) pat=%.32s str=%.64s",
                g, f, fn, (char *) pat_buf, (char *) str_buf);
#else
  HEGEL_ASSERT (g == f,
                "glob=%d fnmatch=%d (rc=%d) pat=%.32s str=%.64s",
                g, f, fn, (char *) pat_buf, (char *) str_buf);
#endif
}

/* Demonstration test: trailing-backslash divergence is real.  We
** assert that glob_match returns true on ("\\", "") while fnmatch
** returns FNM_NOMATCH.  Doubles as a regression marker. */
static void
test_known_divergence_trailing_backslash (hegel_testcase * tc)
{
  (void) tc;
  /* No draws — this is a constant test.  Hegel will still run it for
  ** N cases but they all pass identically; effectively a unit test
  ** wrapped in PBT machinery so it lives in the same suite. */
  HEGEL_ASSERT (glob_match ("\\", "") == true,
                "expected glob_match(\"\\\\\", \"\") = true");
  HEGEL_ASSERT (fnmatch ("\\", "", 0) != 0,
                "expected fnmatch(\"\\\\\", \"\", 0) != 0");
}

int
main (void)
{
  hegel_suite *s = hegel_suite_new ();
  hegel_suite_add (s, "glob_vs_fnmatch_random_bytes",
                   test_glob_vs_fnmatch);
  hegel_suite_add (s, "known_divergence_trailing_backslash",
                   test_known_divergence_trailing_backslash);
  int rc = hegel_suite_run (s);
  hegel_suite_free (s);
  return rc;
}
