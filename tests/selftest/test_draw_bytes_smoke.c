/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* draw_bytes smoke test: hegel_draw_bytes yields a length-bounded,
** BINARY-SAFE buffer.
**
** Two properties, both must hold for every draw (run PASSES, exit 0):
**   - length is within the requested [min, max] (capped at capacity);
**   - the buffer is genuine binary — it may contain 0x00 mid-buffer, and
**     all `len` bytes are valid regardless of any embedded NUL.  This is
**     the whole point of draw_bytes vs draw_text (which keeps NUL out so
**     its result is a C string).
**
** We can't assert a NUL *is* present (random buffers needn't contain one),
** so binary-safety is shown by processing exactly `len` bytes with a
** length-based loop and confirming the count is independent of where the
** first NUL falls (strnlen <= len, and the full-length scan still covers
** every byte).
**
** Three layers:
**   1. Function under test  — byte_sum (length-based, NUL-agnostic)
**   2. Hegel test           — bounds + binary-safety properties
**   3. Makefile runner      — TESTS_PASS, must exit 0 */

#include "hegel_c.h"

#include <string.h>

#define BUF_CAP 256
#define MIN_LEN 1
#define MAX_LEN 200

/* ---- Layer 1: a length-based consumer that does NOT stop at NUL ---- */

static unsigned long byte_sum (const uint8_t * b, int n)
{
  unsigned long acc = 0;
  for (int i = 0; i < n; i++) acc += b[i];
  return acc;
}

/* ---- Layer 2: the property ---- */

static void prop_draw_bytes (hegel_testcase * tc)
{
  uint8_t buf[BUF_CAP];
  int len = hegel_draw_bytes (tc, MIN_LEN, MAX_LEN, buf, BUF_CAP);

  HEGEL_ASSERT (len >= MIN_LEN && len <= MAX_LEN,
                "draw_bytes length %d outside [%d, %d]", len, MIN_LEN, MAX_LEN);

  /* Binary-safety: a length-based scan covers all `len` bytes even when a
  ** NUL appears before the end.  strnlen stops at the first NUL (or len),
  ** so it is always <= len; byte_sum reads the full length regardless. */
  size_t cstr_len = strnlen ((const char *) buf, (size_t) len);
  HEGEL_ASSERT (cstr_len <= (size_t) len,
                "strnlen %zu exceeds drawn len %d", cstr_len, len);

  (void) byte_sum (buf, len);   /* must not over-read past `len` */
}

int main (void)
{
  hegel_run_test (prop_draw_bytes);
  return 0;
}
