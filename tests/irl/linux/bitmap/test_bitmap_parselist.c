/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** PBT for the kernel's bitmap_parselist (lib/bitmap-str.c).
**
** Property A (round-trip via our reference printer):
**   For any bitmap of N bits with random subset set:
**     S = ref_print_list(bitmap)
**     B' = bitmap_parselist(S)
**     B == B'
**
** Property B (parselist accepts any well-formed list and produces the
** matching set):
**   Generate a random valid range list using hegel_draw_int (to choose
**   ranges) — never via raw text drawing (parselist's grammar is too
**   structured for shrink-friendly text generation).  Compare parselist
**   output against an oracle that sets the same bits directly.
**
** ----------------------------------------------------------------------
** Note on the C-side reference printer: kernel's bitmap_print_to_pagebuf
** uses scnprintf with the kernel-only "%*pbl" format extension, which
** libc's vsnprintf does not understand.  Compiling kernel lib/vsprintf.c
** would drag in 3700 lines + many headers.  We write our own simple
** printer (50 lines) instead.  This is a friction note, not a defeat —
** see report. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/types.h>

/* The harness's tools/include/linux/bitmap.h does NOT declare
** bitmap_parselist (it's a stripped-down userspace shim).  Declare it
** ourselves matching lib/bitmap-str.c. */
extern int bitmap_parselist(const char *buf, unsigned long *maskp, int nmaskbits);

#include "hegel_c.h"

#define NBITS 256
#define NLONGS ((NBITS + 63) / 64)
#define BUFCAP 4096        /* generous; max printed length for 256 bits well under this */

/* ------------------------------------------------------------------ *
**  Reference list-format printer                                      *
**                                                                    *
**  Output format matches kernel's "%*pbl": comma-separated decimal    *
**  numbers and ranges, e.g. "0,2-5,9".  Trailing newline is optional  *
**  (parselist accepts both).  Empty bitmap prints as "".              *
** ------------------------------------------------------------------ */
static int ref_print_list(char *buf, size_t cap, const unsigned long *bm, int nbits) {
  int written = 0;
  int i = 0;
  while (i < nbits) {
    /* find next set bit */
    while (i < nbits && !(bm[i / 64] & (1UL << (i & 63)))) i++;
    if (i >= nbits) break;
    int start = i;
    while (i < nbits && (bm[i / 64] & (1UL << (i & 63)))) i++;
    int end = i - 1;
    int n;
    if (written > 0) n = snprintf(buf + written, cap - written, ",");
    else             n = 0;
    if (n < 0 || (size_t)(written + n) >= cap) return -1;
    written += n;
    if (start == end) n = snprintf(buf + written, cap - written, "%d", start);
    else              n = snprintf(buf + written, cap - written, "%d-%d", start, end);
    if (n < 0 || (size_t)(written + n) >= cap) return -1;
    written += n;
  }
  buf[written] = '\0';
  return written;
}

static int bm_eq(const unsigned long *a, const unsigned long *b, int nbits) {
  /* Compare bit-by-bit, ignoring possibly-set tail bits. */
  for (int i = 0; i < nbits; i++) {
    int wa = (a[i / 64] >> (i & 63)) & 1UL;
    int wb = (b[i / 64] >> (i & 63)) & 1UL;
    if (wa != wb) return 0;
  }
  return 1;
}

/* ------------------------------------------------------------------ *
**  Property A: round-trip via reference printer                       *
** ------------------------------------------------------------------ */
static void prop_round_trip(hegel_testcase *tc) {
  unsigned long bm[NLONGS];
  memset(bm, 0, sizeof bm);

  /* Draw 0..NBITS bits to set, each at a random position.  Allowing
  ** repeats is fine — set_bit is idempotent on the same bit. */
  int k = hegel_draw_int(tc, 0, NBITS);
  for (int i = 0; i < k; i++) {
    int pos = hegel_draw_int(tc, 0, NBITS - 1);
    bm[pos / 64] |= 1UL << (pos & 63);
  }

  char out[BUFCAP];
  int n = ref_print_list(out, sizeof out, bm, NBITS);
  HEGEL_ASSERT(n >= 0, "ref_print_list overflow");

  hegel_note(tc, out);

  unsigned long bm2[NLONGS];
  int rc = bitmap_parselist(out, bm2, NBITS);
  HEGEL_ASSERT(rc == 0, "bitmap_parselist failed: rc=%d on input '%s'", rc, out);
  HEGEL_ASSERT(bm_eq(bm, bm2, NBITS),
               "round-trip mismatch on input '%s'", out);
}

/* ------------------------------------------------------------------ *
**  Property B: drawn-grammar test                                     *
**                                                                    *
**  Build a list of disjoint, increasing ranges [lo_i, hi_i] and write *
**  them as a string.  Compare parselist output against a hand-set     *
**  oracle bitmap.  Stresses the parser's range/pattern surface and    *
**  whitespace handling.                                               *
** ------------------------------------------------------------------ */
static void prop_drawn_grammar(hegel_testcase *tc) {
  unsigned long oracle[NLONGS];
  memset(oracle, 0, sizeof oracle);

  char out[BUFCAP];
  int written = 0;
  out[0] = '\0';

  int n_ranges = hegel_draw_int(tc, 0, 16);
  int cursor = 0;
  for (int r = 0; r < n_ranges; r++) {
    if (cursor >= NBITS) break;
    int gap = hegel_draw_int(tc, 0, 8);
    int lo = cursor + gap;
    if (lo >= NBITS) break;
    int span = hegel_draw_int(tc, 0, 16);
    int hi = lo + span;
    if (hi >= NBITS) hi = NBITS - 1;

    /* Set oracle bits */
    for (int b = lo; b <= hi; b++) oracle[b / 64] |= 1UL << (b & 63);

    /* Append the range to out */
    int n;
    if (written > 0) {
      /* draw a separator: comma, ", ", " ,", etc */
      int sep_kind = hegel_draw_int(tc, 0, 3);
      const char *sep =
        sep_kind == 0 ? "," :
        sep_kind == 1 ? ", " :
        sep_kind == 2 ? " ," :
                        " , ";
      n = snprintf(out + written, sizeof out - written, "%s", sep);
      if (n < 0 || (size_t)(written + n) >= sizeof out) return;
      written += n;
    }
    if (lo == hi) n = snprintf(out + written, sizeof out - written, "%d", lo);
    else          n = snprintf(out + written, sizeof out - written, "%d-%d", lo, hi);
    if (n < 0 || (size_t)(written + n) >= sizeof out) return;
    written += n;

    cursor = hi + 1;
  }

  hegel_note(tc, out);

  unsigned long bm[NLONGS];
  int rc = bitmap_parselist(out, bm, NBITS);
  HEGEL_ASSERT(rc == 0, "bitmap_parselist failed: rc=%d on input '%s'", rc, out);
  HEGEL_ASSERT(bm_eq(oracle, bm, NBITS),
               "drawn-grammar mismatch on input '%s'", out);
}

int main(void) {
  /* Property A: 500 cases via the round-trip oracle.  Single-shot — the
  ** memory note's plan-of-attack target. */
  hegel_run_test_n(prop_round_trip, 500);

  /* Property B: 500 cases on synthesized valid grammar. */
  hegel_run_test_n(prop_drawn_grammar, 500);

  fprintf(stderr, "[bitmap_parselist PBT] 500 round-trip + 500 drawn-grammar cases passed\n");
  return 0;
}
