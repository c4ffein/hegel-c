/* SPDX-License-Identifier: MIT
** Copyright (c) 2026 c4ffein
** Part of hegel-c — see hegel/LICENSE for terms. */

/* draw_bytes smoke test: hegel_draw_bytes yields length-bounded,
** BINARY-SAFE buffers.  Run PASSES (exit 0).
**
** Binary-safety is the whole point of draw_bytes vs draw_text (which
** sets min_codepoint=1 to keep NUL out so its result is a C string).
** Two distinct ways it can break, each guarded here:
**
**   - Generation: draw_bytes must actually produce 0x00 bytes.  Each
**     case draws 100 fixed-length buffers and asserts at least one
**     contains an embedded NUL.  This is robust, not lucky: the engine
**     biases byte values toward zero, so empirically every case has
**     10+ NUL-containing buffers out of 100 (measured min 14/100 over
**     200 cases).  A draw_text-like generator would produce zero.
**
**   - Transport: the binding must carry all the bytes, not truncate at
**     a NUL.  Each fixed-length draw of K bytes must return exactly K;
**     a strlen/strcpy-based (NUL-naive) copy would return short on any
**     buffer containing a zero.
**
** Plus a plain bounds check on a variable-length draw.
**
** Three layers:
**   1. Function under test  — hegel_draw_bytes (the primitive)
**   2. Hegel test           — bounds + generates-NUL + non-truncation
**   3. Makefile runner      — TESTS_PASS, must exit 0 */

#include "hegel_c.h"

#include <string.h>

#define BUF_CAP     256
#define MIN_LEN     1
#define MAX_LEN     200
#define FIXED_LEN   64     /* < BUF_CAP, so capacity never caps the result */
#define N_BIN_DRAWS 100    /* per case; measured min 14 contain a NUL */

static void prop_draw_bytes (hegel_testcase * tc)
{
  uint8_t buf[BUF_CAP];

  /* Bounds: a variable-length draw stays within the requested range. */
  int len = hegel_draw_bytes (tc, MIN_LEN, MAX_LEN, buf, BUF_CAP);
  HEGEL_ASSERT (len >= MIN_LEN && len <= MAX_LEN,
                "draw_bytes length %d outside [%d, %d]", len, MIN_LEN, MAX_LEN);

  /* Binary-safety: draw N fixed-length buffers.  Each must come back at
  ** exactly FIXED_LEN (no NUL truncation in the binding), and at least
  ** one across the batch must contain an embedded 0x00 (draw_bytes
  ** really emits binary data). */
  int with_nul = 0;
  for (int i = 0; i < N_BIN_DRAWS; i++) {
    int flen = hegel_draw_bytes (tc, FIXED_LEN, FIXED_LEN, buf, BUF_CAP);
    HEGEL_ASSERT (flen == FIXED_LEN,
                  "fixed draw_bytes returned %d, expected exactly %d "
                  "(NUL truncation?)", flen, FIXED_LEN);
    if (memchr (buf, 0, (size_t) flen) != NULL) with_nul++;
  }
  HEGEL_ASSERT (with_nul >= 1,
                "no embedded NUL in %d byte arrays of %d bytes — "
                "draw_bytes is not producing binary data", N_BIN_DRAWS, FIXED_LEN);
}

int main (void)
{
  hegel_run_test (prop_draw_bytes);
  return 0;
}
