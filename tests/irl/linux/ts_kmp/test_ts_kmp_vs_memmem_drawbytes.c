/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms.
**
** Third A/B/C variant of the ts_kmp differential test.  Same property
** as test_ts_kmp_vs_memmem.c (primitives) and the schema variant (now in
** experiments/schema-with-let/irl/linux/ts_kmp/), but using the
** hegel_draw_bytes primitive.
**
** Compare the random_bytes test body against:
**   - primitives:  per-byte for-loop of hegel_draw_int(tc, 0, 255).
**                  N draws per array, signal fragmented in byte stream.
**   - schema:      HEGEL_STRUCT(Bytes, HEGEL_LET(n, ...), HEGEL_ARR_OF(...))
**                  Setup-time schema build, runtime alloc + free, no
**                  per-draw trace lines on failure.
**   - draw_bytes:  one call: hegel_draw_bytes(tc, min, max, buf, cap).
**                  Single logical span, stack-allocated buf, no setup.
**
** Biased cases (alphabet 'a'/'b' or {0x00,'a','b'}) still need
** per-byte loops because hegel_draw_bytes uses a fixed [0,255]
** alphabet.  An alphabet-aware variant (hegel_draw_bytes_alphabet)
** is on the Phase-3 backlog. */

#define _GNU_SOURCE     /* memmem */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "hegel_c.h"

extern unsigned int ts_kmp_search (const void *pattern, unsigned int plen,
                                   const void *data, unsigned int dlen);

#define MAX_HAY 1024
#define MAX_NDL  32

static unsigned int
oracle_memmem (const unsigned char *hay, size_t hlen,
               const unsigned char *ndl, size_t nlen)
{
  if (nlen == 0) return 0;
  if (nlen > hlen) return UINT_MAX;
  const unsigned char *p = memmem (hay, hlen, ndl, nlen);
  if (!p) return UINT_MAX;
  return (unsigned int) (p - hay);
}

static void
test_random_bytes_drawbytes (hegel_testcase * tc)
{
  uint8_t hay[MAX_HAY];
  uint8_t ndl[MAX_NDL];

  int hlen = hegel_draw_bytes (tc, 1, MAX_HAY, hay, sizeof hay);
  int nlen = hegel_draw_bytes (tc, 1, MAX_NDL, ndl, sizeof ndl);

  unsigned int kmp_off = ts_kmp_search (ndl, nlen, hay, hlen);
  unsigned int mem_off = oracle_memmem (hay, hlen, ndl, nlen);

  HEGEL_ASSERT (kmp_off == mem_off,
                "kmp_off=%u mem_off=%u hlen=%d nlen=%d",
                kmp_off, mem_off, hlen, nlen);
}

int
main (void)
{
  hegel_run_test_n (test_random_bytes_drawbytes, 200);
  return 0;
}
